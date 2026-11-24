#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <vector>
#include <future>
#include <mutex>
#include <atomic>
#include <curl/curl.h>
#if __has_include(<jsoncpp/json/json.h>)
    #include <jsoncpp/json/json.h>
#else
    #include <json/json.h>
#endif
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

static std::string stringPage;
static std::map<long, std::vector<std::string>> publicationMap;
static std::map<std::string, std::string> journalsMap;
static std::vector<std::string> publicationsOnNIPNE;
static std::vector<std::string> articlesOnNIPNE;
static std::vector<std::string> idsOnNIPNE;
static std::vector<std::string> yearsOnNIPNE;
static std::map<std::string, std::string> journalIdCache; // NIPNE tag -> id_revista
static std::map<std::string, float> SRImap;
static std::string yr;

// Thread-safe callback: writes to the std::string* passed via CURLOPT_WRITEDATA
static size_t writeToString(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    if (userp) {
        static_cast<std::string*>(userp)->append(static_cast<char*>(contents), realsize);
    }
    return realsize;
}

// Single-threaded callback for functions that write to global stringPage
static size_t getString(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    stringPage.append(static_cast<char*>(contents), realsize);
    return realsize;
}

std::string urlEncode(CURL* curl, const std::string& value) {
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.length()));
    std::string result(encoded ? encoded : "");
    if (encoded) curl_free(encoded);
    return result;
}

void replacePart(std::string& in, std::string replacee, std::string replacer) {
    while (in.find(replacee) != std::string::npos) {
        in.replace(in.find(replacee), replacee.size(), replacer);
    }
}

std::string sanitizeTitleForNipne(std::string title) {
    replacePart(title, "\"", "");
    replacePart(title, "'", "");
    replacePart(title, "$", "");
    replacePart(title, "\\sqrt{s}", "sqrt(s)");
    replacePart(title, "\\sqrt", "sqrt");
    replacePart(title, "\\", "");
    replacePart(title, "{", "");
    replacePart(title, "}", "");
    return title;
}

bool parseJsonString(const std::string& rawJson, Json::Value& root) {
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream iss(rawJson);
    return Json::parseFromStream(builder, iss, &root, &errs);
}

void getInspirePublications(std::string author);
void readSRI(std::string file);
void login(std::string uname = "fake", std::string pass = "fake");
void getListOfPublications();
bool placePublication(const std::vector<std::string>& pub, const std::string& idRevista);
bool deletePublicationById(const std::string& id);
void deletePublicationByTitle(const std::string& targetTitle);
void deleteAllPublications(unsigned int numThreads = 8);
void uploadAllPublications(unsigned int numThreads = 8);
std::string getFullJournalName(const std::string& shortTitle);
std::string getNipneJournalId(std::string& tag);
void testJournalMappings();

namespace po = boost::program_options;

int main(int argc, char* argv[]) {
    static std::string author;
    static std::string uname = "";
    static std::string pass = "";
    static std::string SRIfileName = "";
    bool testrun = false;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("author,a", po::value<std::string>(), "author name")
        ("username,u", po::value<std::string>(), "username for NIPNE login")
        ("password,p", po::value<std::string>(), "password for NIPNE login")
        ("srifile,s", po::value<std::string>(), "SRI file name")
        ("year,y", po::value<std::string>(&yr), "choose the publication year (optional)")
        ("test,t", po::bool_switch(&testrun), "do a test run")
        ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return EXIT_FAILURE;
    }
    if (vm.count("author")) {
        std::cout << "Author: " << vm["author"].as<std::string>() << std::endl;
        author = vm["author"].as<std::string>();
        while (author.find(" ") != std::string::npos)
            author.replace(author.find(" "), 1, "%20");
    }
    else {
        std::cout << "Author not set!" << std::endl;
        return EXIT_FAILURE;
    }
    if (testrun) {
        std::cout << "Author query is: " << author << std::endl;
        std::cout << "TEST RUN SUCCESSFUL!" << std::endl;
        return EXIT_SUCCESS;
    }
    if (vm.count("srifile")) {
        std::cout << "File containing SRI: " << vm["srifile"].as<std::string>() << std::endl;
        SRIfileName = vm["srifile"].as<std::string>();
    }
    if (vm.count("username") && vm.count("password")) {
        std::cout << "Username for NIPNE login: " << vm["username"].as<std::string>() << std::endl;
        std::cout << "Password for NIPNE login set" << std::endl;
        uname = vm["username"].as<std::string>();
        pass = vm["password"].as<std::string>();
    }
    else {
        std::cout << "Username or password not set!" << std::endl;
        return EXIT_FAILURE;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    if (!SRIfileName.empty()) {
        readSRI(SRIfileName);
    }

    // 1. Fetch all published articles for the author from INSPIRE-HEP
    getInspirePublications(author);

    // 2. Log into NIPNE database
    login(uname, pass);

    // 3. Scrape existing NIPNE publications (to get their internal IDs for deletion)
    getListOfPublications();

    // 4. Delete existing publications (filtered by -y if provided, or all if omitted)
    deleteAllPublications(8);

    // 5. Re-upload publications from INSPIRE (filtered by -y if provided, or all if omitted)
    uploadAllPublications(8);

    curl_global_cleanup();
    return EXIT_SUCCESS;
}

std::string getFullJournalName(const std::string& shortTitle) {
    if (shortTitle.empty()) return "";

    static const std::map<std::string, std::string> overrides = {
        { "JHEP", "JOURNAL OF HIGH ENERGY PHYSICS" },
        { "Nucl.Instrum.Meth.A", "NUCLEAR INSTRUMENTS AND METHODS IN PHYSICS RESEARCH SECTION A-ACCELERATORS SPECTROMETERS DETECTORS AND ASSOCIATED EQUIPMENT" },
        { "PTEP", "PTEP" }
    };

    auto ovIt = overrides.find(shortTitle);
    if (ovIt != overrides.end()) {
        journalsMap[shortTitle] = ovIt->second;
        return ovIt->second;
    }

    auto it = journalsMap.find(shortTitle);
    if (it != journalsMap.end()) {
        return it->second;
    }

    CURL* curl = curl_easy_init();
    if (!curl) return shortTitle;

    std::string query = "short_title:\"" + shortTitle + "\"";
    std::string url = "https://inspirehep.net/api/journals?size=1&q=" + urlEncode(curl, query);

    std::string responseBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; NIPNE-Updater/1.0)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return shortTitle;
    }

    Json::Value root;
    //Json::Reader reader;
    std::string fullTitle = shortTitle;

    if (parseJsonString(responseBuffer, root)) {
        const Json::Value& hits = root["hits"]["hits"];
        if (hits.isArray() && !hits.empty()) {
            const Json::Value& meta = hits[0]["metadata"];
            if (meta.isMember("journal_title") && meta["journal_title"].isMember("title") && meta["journal_title"]["title"].isString()) {
                fullTitle = meta["journal_title"]["title"].asString();
                std::transform(fullTitle.begin(), fullTitle.end(), fullTitle.begin(), ::toupper);
            }
        }
    }

    journalsMap[shortTitle] = fullTitle;
    return fullTitle;
}

void getInspirePublications(std::string author) {
    unsigned int page = 1;
    int noArticles = 0;
    int duplicateArts = 0;
    Json::Reader reader;

    std::cout << "Probing INSPIRE-HEP DB for author: " << author << "..." << std::endl;

    while (true) {
        std::string queryLine = "https://inspirehep.net/api/literature?sort=mostrecent&size=250&page="
            + std::to_string(page)
            + "&fields=titles,dois,citation_count,citation_count_without_self_citations,document_type,collaborations,publication_info,author_count"
            + "&q=find%20a%20" + author + "%20and%20tc%20p";

        std::string responseBuffer;
        CURL* curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, queryLine.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; NIPNE-Updater/1.0)");
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                fprintf(stderr, "curl perform for inspire site failed: %s\n", curl_easy_strerror(res));
                curl_easy_cleanup(curl);
                break;
            }
            curl_easy_cleanup(curl);
        }

        Json::Value root;
        if (!parseJsonString(responseBuffer, root) || !root.isMember("hits") || !root["hits"].isMember("hits")) {
            std::cout << "Finished or error parsing INSPIRE JSON on page " << page << "." << std::endl;
            break;
        }

        const Json::Value& myArts = root["hits"]["hits"];
        if (!myArts.isArray() || myArts.empty()) {
            break;
        }

        std::cout << "Fetched page " << page << " (" << myArts.size() << " records)..." << std::endl;

        for (const auto& item : myArts) {
            long ID = 0;
            if (item.isMember("id")) {
                if (item["id"].isString()) {
                    ID = std::stol(item["id"].asString());
                }
                else if (item["id"].isIntegral()) {
                    ID = static_cast<long>(item["id"].asLargestInt());
                }
            }

            if (!item.isMember("metadata") || !item["metadata"].isObject()) continue;
            const Json::Value& meta = item["metadata"];

            std::string pub_year = "";
            std::string j_short = "";
            std::string j_volume = "";
            std::string j_issue = "";
            std::string page_start = "";

            if (meta.isMember("publication_info") && meta["publication_info"].isArray() && !meta["publication_info"].empty()) {
                const Json::Value& pubInfo = meta["publication_info"][0];
                if (pubInfo.isMember("year") && pubInfo["year"].isIntegral())
                    pub_year = std::to_string(pubInfo["year"].asInt());
                if (pubInfo.isMember("journal_title") && pubInfo["journal_title"].isString())
                    j_short = pubInfo["journal_title"].asString();
                if (pubInfo.isMember("journal_volume") && pubInfo["journal_volume"].isString())
                    j_volume = pubInfo["journal_volume"].asString();
                if (pubInfo.isMember("journal_issue") && pubInfo["journal_issue"].isString())
                    j_issue = pubInfo["journal_issue"].asString();
                if (pubInfo.isMember("page_start") && pubInfo["page_start"].isString())
                    page_start = pubInfo["page_start"].asString();
                else if (pubInfo.isMember("artid") && pubInfo["artid"].isString())
                    page_start = pubInfo["artid"].asString();
            }

            if (j_short.empty()) continue;

            std::string doc_type = "";
            if (meta.isMember("document_type") && meta["document_type"].isArray() && !meta["document_type"].empty()) {
                if (meta["document_type"][0].isString())
                    doc_type = meta["document_type"][0].asString();
            }
            if (doc_type != "article") continue;

            std::string title = "";
            if (meta.isMember("titles") && meta["titles"].isArray() && !meta["titles"].empty()) {
                if (meta["titles"][0].isMember("title") && meta["titles"][0]["title"].isString())
                    title = meta["titles"][0]["title"].asString();
            }
            replacePart(title, "\"", "");
            replacePart(title, "'", "");

            std::string dois = "";
            if (meta.isMember("dois") && meta["dois"].isArray() && !meta["dois"].empty()) {
                if (meta["dois"][0].isMember("value") && meta["dois"][0]["value"].isString())
                    dois = meta["dois"][0]["value"].asString();
            }

            std::string cit_no_self = "0";
            if (meta.isMember("citation_count_without_self_citations") && meta["citation_count_without_self_citations"].isIntegral()) {
                cit_no_self = std::to_string(meta["citation_count_without_self_citations"].asInt());
            }

            std::string href = "https://inspirehep.net/api/literature/" + std::to_string(ID);

            std::string coll = "";
            if (meta.isMember("collaborations") && meta["collaborations"].isArray() && !meta["collaborations"].empty()) {
                if (meta["collaborations"][0].isMember("value") && meta["collaborations"][0]["value"].isString())
                    coll = meta["collaborations"][0]["value"].asString();
            }

            size_t numAuthors = 1;
            if (meta.isMember("author_count") && meta["author_count"].isIntegral()) {
                numAuthors = meta["author_count"].asUInt();
            }

            std::string authorsStr = "Adrian Chitan et al.";
            if (!coll.empty() && coll.find("ATLAS") != std::string::npos) {
                authorsStr = "ATLAS Collaboration (George Aad, ..., Adrian Chitan, ...)";
            }
            std::string no_auths = std::to_string(numAuthors);

            std::string j_full = getFullJournalName(j_short);

            std::string ref_art = j_short + " " + j_volume + " (" + pub_year + ") ";
            if (!j_issue.empty()) {
                ref_art += j_issue + ", ";
            }
            ref_art += page_start;

            if (publicationMap.find(ID) == publicationMap.end()) {
                noArticles++;
            }
            else {
                duplicateArts++;
            }

            publicationMap[ID] = {
                title,       // 0
                dois,        // 1
                cit_no_self, // 2
                href,        // 3
                doc_type,    // 4
                authorsStr,  // 5
                no_auths,    // 6
                ref_art,     // 7
                pub_year,    // 8
                j_full       // 9
            };
        }
        page++;
    }
    std::cout << "FINISHED PROBING INSPIREHEP DB. FOUND " << publicationMap.size() << " UNIQUE ARTICLES!" << std::endl;
}

void readSRI(std::string file) {
    std::ifstream SRIfile(file, std::ios::in);
    std::string line;

    while (std::getline(SRIfile, line)) {
        for (auto& character : line)
            character = std::toupper(character);
        std::size_t idx = line.rfind('\t');
        if (idx == std::string::npos) continue;
        std::string sriStr = line.substr(idx + 1, line.length() - idx);
        if (sriStr.find(',') != std::string::npos) {
            sriStr.replace(sriStr.find(','), 1, ".");
        }
        idx = line.find('\t');
        std::string journalStr = line.substr(0, idx);
        SRImap[journalStr] = std::atof(sriStr.c_str());
    }
    SRIfile.close();
}

void login(std::string uname, std::string pass) {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    std::string loginPage = "https://dbpub.nipne.ro/login.php";
    std::string postData = "utilizator=" + urlEncode(curl, uname) +
        "&parola=" + urlEncode(curl, pass);

    stringPage.clear();
    curl_easy_setopt(curl, CURLOPT_URL, loginPage.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getString);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "logon.txt");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_REFERER, "https://dbpub.nipne.ro/index.php");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "login failed: " << curl_easy_strerror(res) << std::endl;
    }
    else {
        if (stringPage.find("logout.php") != std::string::npos) {
            std::cout << "Login SUCCESSFUL (session active)!" << std::endl;
        }
        else {
            std::cout << "Login did NOT authenticate. Server returned:\n"
                << stringPage << "\n--- END LOGIN PAGE ---" << std::endl;
        }
    }
    curl_easy_cleanup(curl);
}

void getListOfPublications() {
    stringPage.clear();
    publicationsOnNIPNE.clear();
    articlesOnNIPNE.clear();
    idsOnNIPNE.clear();
    yearsOnNIPNE.clear();

    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://dbpub.nipne.ro/vizualizare_lucrari.php");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getString);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "logon.txt");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl perform in getListOfPublications failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
    }

    unsigned long pubNo = 1;
    size_t pubLoc = 0;
    const std::string delPrefix = "stergere_lucrare.php?id=";

    while (stringPage.find(std::to_string(pubNo) + "&nbsp;<i>", pubLoc) != std::string::npos) {
        pubLoc = stringPage.find(std::to_string(pubNo) + "&nbsp;<i>", pubLoc);
        pubLoc = stringPage.find("<i>", pubLoc) + 3;

        std::string title = stringPage.substr(pubLoc, stringPage.find("</i>", pubLoc) - pubLoc);

        size_t codeStart = stringPage.find("<code>", pubLoc) + 6;
        std::string artRef = stringPage.substr(codeStart, stringPage.find("&nbsp;", codeStart) - codeStart);

        // Extract 4-digit year from "(YYYY)" in artRef
        std::string itemYear = "";
        size_t openParen = artRef.find('(');
        if (openParen != std::string::npos && openParen + 5 < artRef.size() && artRef[openParen + 5] == ')') {
            itemYear = artRef.substr(openParen + 1, 4);
        }

        std::string pubId = "";
        size_t delLoc = stringPage.find(delPrefix, pubLoc);
        if (delLoc != std::string::npos) {
            delLoc += delPrefix.length();
            pubId = stringPage.substr(delLoc, stringPage.find('"', delLoc) - delLoc);
        }

        publicationsOnNIPNE.push_back(title);
        articlesOnNIPNE.push_back(artRef);
        idsOnNIPNE.push_back(pubId);
        yearsOnNIPNE.push_back(itemYear);
        pubNo++;
    }
    std::cout << "Found " << idsOnNIPNE.size() << " publications currently on NIPNE." << std::endl;
    stringPage.clear();
}

std::string getNipneJournalId(std::string& tag) {
    if (tag.empty()) return "";

    auto it = journalIdCache.find(tag);
    if (it != journalIdCache.end()) {
        return it->second;
    }

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    auto queryGetId = [&](const std::string& candidateTag) -> std::string {
        std::string getidUrl = "https://dbpub.nipne.ro/autoc/getid.php?q=" + urlEncode(curl, candidateTag);
        std::string responseBuffer;
        curl_easy_setopt(curl, CURLOPT_URL, getidUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "logon.txt");
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) return "";

        size_t valPos = responseBuffer.find("value=\"");
        if (valPos != std::string::npos) {
            valPos += 7;
            return responseBuffer.substr(valPos, responseBuffer.find('"', valPos) - valPos);
        }
        return "";
        };

    std::string idRevista = queryGetId(tag);
    if (idRevista.empty() && tag.rfind("THE ", 0) == 0) {
        std::string strippedTag = tag.substr(4);
        idRevista = queryGetId(strippedTag);
        if (!idRevista.empty()) {
            tag = strippedTag;
        }
    }

    curl_easy_cleanup(curl);
    journalIdCache[tag] = idRevista;
    return idRevista;
}

bool deletePublicationById(const std::string& id) {
    if (id.empty()) return false;
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string delUrl = "https://dbpub.nipne.ro/stergere_lucrare.php";
    std::string postFields = "id=" + urlEncode(curl, id) +
        "&submit=" + urlEncode(curl, "delete this record !");
    std::string referer = "https://dbpub.nipne.ro/stergere_lucrare.php?id=" + id;

    std::string responseBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, delUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "logon.txt");
    curl_easy_setopt(curl, CURLOPT_REFERER, referer.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK && responseBuffer.find("successfully deleted!") != std::string::npos);
}

void deletePublicationByTitle(const std::string& targetTitle) {
    for (size_t i = 0; i < publicationsOnNIPNE.size(); ++i) {
        if (publicationsOnNIPNE[i].find(targetTitle) != std::string::npos) {
            std::cout << "Deleting \"" << publicationsOnNIPNE[i] << "\" (ID: " << idsOnNIPNE[i] << ")..." << std::endl;
            deletePublicationById(idsOnNIPNE[i]);
            return;
        }
    }
    std::cout << "Article \"" << targetTitle << "\" not found on NIPNE." << std::endl;
}

bool placePublication(const std::vector<std::string>& pub, const std::string& idRevista) {
    std::string lucrare = sanitizeTitleForNipne(pub.at(0));
    std::string autori = pub.at(5);
    std::string nr_autori = pub.at(6);
    std::string afiliere = "da";
    std::string principal = "nu";
    std::string revista = pub.at(7);
    std::string an = pub.at(8);
    std::string tag = pub.at(9);
    std::string citari = "";
    std::string nr_citari = pub.at(2);
    std::string submit = "record !";
    std::string tip_revista = idRevista.empty() ? "2" : "1";

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string extraField = idRevista.empty() ? "" : ("&id_revista=" + urlEncode(curl, idRevista));

    std::string postFields =
        "lucrare=" + urlEncode(curl, lucrare) +
        "&autori=" + urlEncode(curl, autori) +
        "&nr_autori=" + urlEncode(curl, nr_autori) +
        "&afiliere=" + urlEncode(curl, afiliere) +
        "&principal=" + urlEncode(curl, principal) +
        "&revista=" + urlEncode(curl, revista) +
        "&tip_revista=" + urlEncode(curl, tip_revista) +
        "&tag=" + urlEncode(curl, tag) +
        extraField +
        "&an=" + urlEncode(curl, an) +
        "&citari=" + urlEncode(curl, citari) +
        "&nr_citari=" + urlEncode(curl, nr_citari) +
        "&submit=" + urlEncode(curl, submit);

    std::string responseBuffer;
    curl_easy_setopt(curl, CURLOPT_URL, "https://dbpub.nipne.ro/inregistrare_lucrare.php");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "logon.txt");
    curl_easy_setopt(curl, CURLOPT_REFERER, "https://dbpub.nipne.ro/adauga_lucrare.php");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK && responseBuffer.find("Inregistrarea a fost efectuata cu succes") != std::string::npos);
}

void deleteAllPublications(unsigned int numThreads) {
    std::vector<std::string> targetIds;
    for (size_t i = 0; i < idsOnNIPNE.size(); ++i) {
        if (!yr.empty() && yearsOnNIPNE[i] != yr) continue;
        targetIds.push_back(idsOnNIPNE[i]);
    }

    if (targetIds.empty()) {
        std::cout << "No publications to delete on NIPNE"
            << (yr.empty() ? "." : (" for year " + yr + ".")) << std::endl;
        return;
    }

    std::cout << "Deleting " << targetIds.size() << " publications from NIPNE using "
        << numThreads << " threads..." << std::endl;

    std::atomic<size_t> idx{ 0 };
    std::atomic<size_t> deletedCount{ 0 };

    auto worker = [&]() {
        while (true) {
            size_t current = idx.fetch_add(1);
            if (current >= targetIds.size()) break;
            if (deletePublicationById(targetIds[current])) {
                size_t done = ++deletedCount;
                std::cout << "Deleted " << done << " / " << targetIds.size() << "\r" << std::flush;
            }
        }
        };

    std::vector<std::future<void>> futures;
    for (unsigned int t = 0; t < numThreads; ++t) {
        futures.push_back(std::async(std::launch::async, worker));
    }
    for (auto& f : futures) f.get();

    std::cout << "\nFinished deleting " << deletedCount << " / " << targetIds.size() << " publications." << std::endl;
}

void uploadAllPublications(unsigned int numThreads) {
    std::vector<std::vector<std::string>> targetPubs;
    std::vector<std::string> resolvedIds;

    // Pre-resolve NIPNE journal IDs sequentially (cached per unique journal)
    for (auto& kv : publicationMap) {
        auto pub = kv.second;
        if (!yr.empty() && pub.at(8) != yr) continue;
        std::string idRev = getNipneJournalId(pub.at(9));
        targetPubs.push_back(pub);
        resolvedIds.push_back(idRev);
    }

    if (targetPubs.empty()) {
        std::cout << "No INSPIRE publications to upload"
            << (yr.empty() ? "." : (" for year " + yr + ".")) << std::endl;
        return;
    }

    std::cout << "Uploading " << targetPubs.size() << " publications to NIPNE using "
        << numThreads << " threads..." << std::endl;

    std::atomic<size_t> idx{ 0 };
    std::atomic<size_t> uploadedCount{ 0 };

    auto worker = [&]() {
        while (true) {
            size_t current = idx.fetch_add(1);
            if (current >= targetPubs.size()) break;
            if (placePublication(targetPubs[current], resolvedIds[current])) {
                size_t done = ++uploadedCount;
                std::cout << "Uploaded " << done << " / " << targetPubs.size() << "\r" << std::flush;
            }
        }
        };

    std::vector<std::future<void>> futures;
    for (unsigned int t = 0; t < numThreads; ++t) {
        futures.push_back(std::async(std::launch::async, worker));
    }
    for (auto& f : futures) f.get();

    std::cout << "\nFinished uploading " << uploadedCount << " / " << targetPubs.size() << " publications." << std::endl;
}

void testJournalMappings() {
    std::vector<std::string> testJournals = {
        "Phys.Rev.D",
        "Phys.Rev.Lett.",
        "Phys.Rev.C",
        "JHEP",
        "Eur.Phys.J.C",
        "Phys.Lett.B",
        "Nucl.Phys.B",
        "JINST",
        "Comput.Phys.Commun.",
        "Nucl.Instrum.Meth.A",
        "Chin.Phys.C",
        "PTEP"
    };

    std::cout << "\n--- Testing INSPIRE -> NIPNE Journal Mapping ---\n";
    for (const auto& shortTitle : testJournals) {
        std::string fullTitle = getFullJournalName(shortTitle);
        std::string nipneTag = fullTitle;
        std::string idRevista = getNipneJournalId(nipneTag);

        std::cout << "Short: " << shortTitle
            << " | INSPIRE Full: " << fullTitle
            << " | NIPNE Tag: " << nipneTag
            << " | id_revista: " << (idRevista.empty() ? "NOT FOUND!" : idRevista)
            << std::endl;
    }
    std::cout << "------------------------------------------------\n";
}