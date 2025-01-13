#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <string>
#include <fstream>
#include <map>
#include <vector>
#include <curl/curl.h>
#include <json/json.h>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

static std::string stringPage;
static std::map<long, std::vector<std::string>> publicationMap;
static std::map<std::string, std::string> journalsMap;
static std::vector<std::string> publicationsOnNIPNE;
static std::vector<std::string> articlesOnNIPNE;
static std::map<std::string, float> SRImap;
static std::string yr;

static size_t getString(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    stringPage.append(static_cast<char*>(contents), realsize);
    return realsize;
}

void getInspirePublications(std::string author);
void readSRI(std::string file);
void computeAIS();
void login(std::string uname = "fake", std::string pass = "fake");
void getListOfPublications();
void replacePart(std::string& in, std::string replacee, std::string replacer);
void compareTitles();
void compareArticles();
void compareJournals();
void placePublication();
void getJournalsMap();

namespace po = boost::program_options;

int main(int argc, char* argv[]) {
    //program options
    static std::string author;
    static std::string uname = "";
    static std::string pass = "";
    static std::string SRIfileName = "";
    //static std::string yr = "";
    bool testrun = false;
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("author,a", po::value<std::string>(), "author name")
        ("username,u", po::value<std::string>(), "username for NIPNE login")
        ("password,p", po::value<std::string>(), "password for NIPNE login")
        ("srifile,s",po::value<std::string>(), "SRI file name")
        ("year,y", po::value<std::string>(&yr), "choose the publication year")
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

    readSRI(SRIfileName);
    
    getInspirePublications(author);
    //for (auto& pub : publicationMap) {
    //    std::cout << pub.first << ") " << pub.second.at(4) << std::endl;//should be article
    //}
    //Ai = A, A < 5
    //Ai = (A+10)/3, 5 <= A <= 80
    //Ai = A/10+22, A > 80

    //login(uname, pass);
    //getListOfPublications();
    //compareTitles();
    //compareArticles();
    //compareJournals();
    //placePublication();
    
    return EXIT_SUCCESS;
}

void getInspirePublications(std::string author) {
    //cURL query
    std::string queryLine;
    unsigned int page = 1;
    static int noArticles = 0;
    static int duplicateArts = 0;
    CURL* curl;
    CURLcode res;
    Json::FastWriter stringWriter;//Json::Value to string
    Json::Value root;
    Json::Reader reader;

    while (true) {
        queryLine = "https://inspirehep.net/api/literature?sort=mostrecent&size=10&page=";
        queryLine += std::to_string(page) + "&q=find%20a%20" + author + "%20and%20tc%20p";
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        stringPage.clear();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, queryLine.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getString);
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
                fprintf(stderr, "curl perform for inspire site failed: %s\n",
                    curl_easy_strerror(res));
            curl_easy_cleanup(curl);
        }
        curl_global_cleanup();

        //JSON stuff
        bool parsingSuccess = reader.parse(stringPage, root);
        if (!parsingSuccess)
            std::cout << "ERROR parsing" << std::endl;
        const Json::Value myArts = root["hits"]["hits"];
        if (myArts.size() == 0)
            break;
        std::string id, dois, title, cit_no_self, cit, href, doc_type, coll, corp, author, pub_info, pub_year, j_title, ref_art, no_auths;
        std::string j_volume, art_id;
        std::vector<std::string> authors;
        long ID;
        for (auto index : myArts) {
            id = stringWriter.write(index["id"]);
            id = id.substr(1, id.find('"', 1) - 1);
            ID = atol(id.c_str());
            title = stringWriter.write(index["metadata"]["titles"][0]["title"]);
            title = title.substr(1, title.find('"', 1) - 1);
            dois = stringWriter.write(index["metadata"]["dois"][0]["value"]);
            dois = dois.substr(1, dois.find('"', 1) - 1);
            cit_no_self = stringWriter.write(index["metadata"]["citation_count_without_self_citations"]);
            cit_no_self = cit_no_self.substr(0, cit_no_self.size() - 1);
            cit = stringWriter.write(index["metadata"]["citation_count"]);
            cit = cit.substr(1, cit.find('"', 1) - 1);
            href = "https://inspirehep.net/api/literature/" + id;
            doc_type = stringWriter.write(index["metadata"]["document_type"]);
            doc_type = doc_type.substr(doc_type.find('"') + 1, doc_type.rfind('"') - 2);
            if (doc_type.compare("article") != 0)
                continue;
            coll = stringWriter.write(index["metadata"]["collaborations"]);
            coll = coll.substr(coll.find(':') + 2, coll.rfind('"') - coll.find(':') - 2);

            //authors routine
            if (coll.find("ATLAS") == std::string::npos) {
                unsigned int i = 0;
                while (i < index["metadata"]["authors"].size()) {
                    author = stringWriter.write(index["metadata"]["authors"][i]["full_name"]);
                    author = author.substr(1, author.find('"', 1) - 1);
                    i++;
                    authors.push_back(author);
                    authors.at(0) += "," + author;
                }
            }
            else
                authors.push_back("ATLAS Collaboration");
            no_auths = std::to_string(index["metadata"]["authors"].size());
            pub_info = stringWriter.write(index["metadata"]["publication_info"][0]["journal_title"]) + stringWriter.write(index["metadata"]["publication_info"][0]["journal_issue"]) +
                stringWriter.write(index["metadata"]["publication_info"][0]["journal_volume"]) + stringWriter.write(index["metadata"]["publication_info"][0]["page_start"]);
            pub_year = stringWriter.write(index["metadata"]["publication_info"][0]["year"]);
            pub_year = pub_year.substr(0, pub_year.size() - 1);
            j_title = stringWriter.write(index["metadata"]["publication_info"][0]["journal_title"]);
            j_title = j_title.substr(1, j_title.find('"', 1) - 1);
            j_volume = stringWriter.write(index["metadata"]["publication_info"][0]["journal_volume"]);
            j_volume = j_volume.substr(0, j_volume.size() - 1);
            art_id = stringWriter.write(index["metadata"]["publication_info"][0]["artid"]);
            art_id = art_id.substr(0, art_id.size() - 1);
            ref_art = j_title + " " + j_volume + " (" + pub_year + ") ";// +art_id;
            replacePart(ref_art, "\"", "");
            if (!publicationMap[ID].size()) {
                publicationMap[ID].push_back(title);
                publicationMap[ID].push_back(dois);
                publicationMap[ID].push_back(cit_no_self);
                publicationMap[ID].push_back(href);
                publicationMap[ID].push_back(doc_type);
                publicationMap[ID].push_back(authors.at(0));
                publicationMap[ID].push_back(no_auths);
                publicationMap[ID].push_back(ref_art);
                publicationMap[ID].push_back(pub_year);
                publicationMap[ID].push_back(j_title);
                noArticles++;
            }
            else {
                if (ref_art.compare(publicationMap[ID].at(7)) != 0)
                    noArticles++;
                duplicateArts++;
                publicationMap[ID].clear();
                publicationMap[ID].push_back(title);
                publicationMap[ID].push_back(dois);
                publicationMap[ID].push_back(cit_no_self);
                publicationMap[ID].push_back(href);
                publicationMap[ID].push_back(doc_type);
                publicationMap[ID].push_back(authors.at(0));
                publicationMap[ID].push_back(no_auths);
                publicationMap[ID].push_back(ref_art);
                publicationMap[ID].push_back(pub_year);
                publicationMap[ID].push_back(j_title);
            }
            std::cout << ID << " : " << title << " : " << dois << " : " << href << " : " << doc_type << " : " << authors.at(0) << " : " << ref_art << " : " << pub_year << " : " << j_title << std::endl;
            authors.clear();
        }
        page++;
        queryLine.clear();
    }
    std::cout << "FINISHED PROBING INSPIREHEP DB, THERE WERE " << publicationMap.size() << " ARTICLES FOUND!" << std::endl;
}

void readSRI(std::string file) {
    std::ifstream SRIfile(file, std::ios::in);
    std::string line;

    while (std::getline(SRIfile, line)) {
        for (auto& character : line)
            character = std::toupper(character);
        std::size_t idx = line.rfind('\t');
        std::string sriStr = line.substr(idx + 1, line.length() - idx);
        sriStr.replace(sriStr.find(','), 1, ".");
        idx = line.find('\t');
        std::string journalStr = line.substr(0, idx);
        SRImap[journalStr] = std::atof(sriStr.c_str());
    }
    SRIfile.close();
}

void computeAIS() {

}

void login(std::string uname, std::string pass) {
    CURL* curl;
    CURLcode res;

    //rutina logare (curl - httpput w/ postfields)
    std::string loginPage = "https://www.nipne.ro/dbpub/login.php";
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    std::string unamepass = "utilizator=" + uname + "&parola=" + pass;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, unamepass.c_str());
        curl_easy_setopt(curl, CURLOPT_URL, loginPage.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "logon.txt");
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cout << "login failed" << std::endl;
            curl_easy_strerror(res);
        }
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

void getListOfPublications() {
    stringPage.clear();
    CURL* curl;
    CURLcode res;
    std::string queryLine = "https://www.nipne.ro/dbpub/vizualizare_lucrari.php";
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    stringPage.clear();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, queryLine.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getString);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "logon.txt");
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl perform in getListOfPublications failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();

    //maybe try to do it with strings (w/ sepparator "2.&nbsp;")
    unsigned long pubNo = 1;
    size_t pubLoc = 0;
    std::string title = "";
    std::string artRef = "";
    //std::cout << stringPage << std::endl;
    while (stringPage.find(std::to_string(pubNo) + ".&nbsp;<i>") != std::string::npos) {
        pubLoc = stringPage.find(std::to_string(pubNo) + ".&nbsp;<i>");
        pubLoc = stringPage.find("<i>", pubLoc);
        pubLoc += 3;
        title = stringPage.substr(pubLoc, stringPage.find("</i>", pubLoc) - pubLoc);
        artRef = stringPage.substr(stringPage.find("<code>", pubLoc) + 6, stringPage.find("&nbsp;", pubLoc) - stringPage.find("<code>", pubLoc) - 6);
        publicationsOnNIPNE.push_back(title);
        articlesOnNIPNE.push_back(artRef);
        //std::cout << artRef << std::endl;
        title.clear();
        pubNo++;
    }
    stringPage.clear();
}

void replacePart(std::string& in, std::string replacee, std::string replacer) {
    while (in.find(replacee) != std::string::npos) {
        in.replace(in.find(replacee), replacee.size(), replacer);
    }
}

void compareTitles() {
    std::ofstream out("outNew.txt", std::ios::out);
    int newPub = 1;
    for (auto& pubINSPIRE : publicationMap) {
        long index = 0;
        if(!yr.empty() && pubINSPIRE.second.at(8).compare(yr) != 0)
            continue;
        for (auto& pubNIPNE : publicationsOnNIPNE) {
            std::string insTitle = boost::to_upper_copy<std::string>(pubINSPIRE.second.at(0));
            std::string nipTitle = boost::to_upper_copy<std::string>(pubNIPNE);
            while (insTitle.find(" ") != std::string::npos) {
                insTitle.replace(insTitle.find(" "), 1, "");
            }
            while (insTitle.find("$") != std::string::npos) {
                size_t whereisdollar = insTitle.find("$");
                size_t whereisnextdollar = insTitle.find("$", whereisdollar + 1);
                insTitle.replace(whereisdollar, whereisnextdollar - whereisdollar + 1, "");
            }
            while (nipTitle.find(" ") != std::string::npos) {
                nipTitle.replace(nipTitle.find(" "), 1, "");
            }
            while (nipTitle.find("$") != std::string::npos) {
                size_t whereisdollar = nipTitle.find("$");
                size_t whereisnextdollar = nipTitle.find("$", whereisdollar + 1);
                nipTitle.replace(whereisdollar, whereisnextdollar - whereisdollar + 1, "");
            }
            if (nipTitle.compare(insTitle) == 0) {
                //std::cout << nipTitle << "\n" << insTitle << std::endl;
                //std::cout << std::endl;
                break;
            }
            else
                index++;
        }
        if (index == publicationsOnNIPNE.size()) {
            std::cout << newPub << ") " << pubINSPIRE.second.at(0) << " " << pubINSPIRE.second.at(8) << std::endl;
            out << std::endl;
            out << newPub << ") " << pubINSPIRE.second.at(0) << std::endl;
            out << pubINSPIRE.second.at(1) << std::endl;
            out << pubINSPIRE.second.at(2) << std::endl;
            out << pubINSPIRE.second.at(3) << std::endl;
            out << pubINSPIRE.second.at(4) << std::endl;
            out << pubINSPIRE.second.at(5) << std::endl;
            out << pubINSPIRE.second.at(6) << std::endl;
            out << pubINSPIRE.second.at(7) << std::endl;
            out << pubINSPIRE.second.at(8) << std::endl;
            out << pubINSPIRE.second.at(9) << std::endl;
            ++newPub;
        }
    }
    out.close();
    std::cout << "there were " << newPub << " new publications" << std::endl;
}

void compareArticles() {
    int newPub = 0;
    for (auto& pubINSPIRE : publicationMap) {
        long index = 0;
        for (auto& pubNIPNE : articlesOnNIPNE) {
            std::cout << pubNIPNE << std::endl;
            if (pubNIPNE.find(pubINSPIRE.second.at(7)) != std::string::npos) {
                break;
            }
            else
                index++;
        }
        if (index == articlesOnNIPNE.size()) {
            ++newPub;
        }
    }
    std::cout << "there were " << newPub << " new publications" << std::endl;
}

void compareJournals() {
    int newPub = 0;
    for (auto& pubINSPIRE : publicationMap) {
        long index = 0;
        for (auto& pubNIPNE : articlesOnNIPNE) {
            std::cout << pubNIPNE << std::endl;
            if (pubNIPNE.find(pubINSPIRE.second.at(9)) != std::string::npos) {
                break;
            }
            else
                index++;
        }
        if (index == articlesOnNIPNE.size()) {
            ++newPub;
        }
    }
    std::cout << "there were " << newPub << " new publications" << std::endl;
}

void placePublication() {
    stringPage.clear();
    CURL* curl;
    CURLcode res;
    //std::string queryLine = "https://www.nipne.ro/dbpub/autoc/autocomplete.php";
    std::string queryLine = "https://www.nipne.ro/dbpub/autoc/getid.php";
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    stringPage.clear();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, queryLine.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getString);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "logon.txt");
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl perform in getListOfPublications failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
    std::cout << stringPage << "END OF PAGE" << std::endl;
}

void getJournalsMap() {
    Json::Value root;
    Json::Reader reader;
    journalsMap.clear();
    std::cout << "fetching inspire journals list..." << std::endl;
    //cURL query
    std::string queryLine;
    unsigned int page = 1;
    static int noJournals = 0;
    CURL* curl;
    CURLcode res;

    while (true) {
        queryLine = "https://inspirehep.net/api/journals?sort=mostrecent&size=10&page=";
        queryLine += std::to_string(page) + "&q=";
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        stringPage.clear();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, queryLine.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getString);
            res = curl_easy_perform(curl);
            if (res != CURLE_OK)
                fprintf(stderr, "curl perform for inspire site failed: %s\n",
                    curl_easy_strerror(res));
            curl_easy_cleanup(curl);
        }
        curl_global_cleanup();

        //JSON stuff
        bool parsingSuccess = reader.parse(stringPage, root);
        if (!parsingSuccess)
            std::cout << "ERROR parsing" << std::endl;
        const Json::Value myArts = root["hits"]["hits"];
        std::stringstream ss;
        //auto myArts = json["hits"]["hits"];
        if (myArts.size() == 0)
            break;
        std::string title, short_title;
        for (auto idx : myArts) {
            ss.str(std::string());
            ss << idx["metadata"]["journal_title"]["title"];
            title = ss.str();
            replacePart(title, "\"", "");
            std::transform(title.begin(), title.end(), title.begin(), toupper);
            ss.str(std::string());
            ss << idx["metadata"]["short_title"];
            short_title = ss.str();
            replacePart(short_title, "\"", "");
            journalsMap[short_title] = title;
            noJournals++;
            std::cout << noJournals << "\r" << std::flush;
        }
        page++;
        queryLine.clear();
    }
    std::cout << std::endl;
}