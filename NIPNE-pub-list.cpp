#include <iostream>
#include <stdio.h>
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
static std::vector<std::string> publicationsOnNIPNE;
static std::vector<std::string> articlesOnNIPNE;

namespace po = boost::program_options;

static size_t getString(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    stringPage.append(static_cast<char*>(contents), realsize);
    return realsize;
}

void login(std::string uname = "fake", std::string pass = "fake");
void getListOfPublications();
void replacePart(std::string& in, std::string replacee, std::string replacer);
void compareTitles();
void compareArticles();

int main(int argc, char* argv[]) {
    //program options
    std::string author;
    std::string uname = "";
    std::string pass = "";
    bool testrun = false;
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("author,a", po::value<std::string>(), "author name")
        ("username,u", po::value<std::string>(), "username for NIPNE login")
        ("password,p", po::value<std::string>(), "password for NIPNE login")
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
            pub_info = stringWriter.write(index["metadata"]["publication_info"][0]["journal_title"] ) + stringWriter.write(index["metadata"]["publication_info"][0]["journal_issue"]) +
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
            authors.clear();
        }
        page++;
        queryLine.clear();
    }
    login(uname, pass);
    getListOfPublications();
    compareTitles();
    //compareArticles();
    
    return EXIT_SUCCESS;
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
    while (stringPage.find(std::to_string(pubNo) + ".&nbsp;<i>") != std::string::npos) {
        pubLoc = stringPage.find(std::to_string(pubNo) + ".&nbsp;<i>");
        pubLoc = stringPage.find("<i>", pubLoc);
        pubLoc += 3;
        title = stringPage.substr(pubLoc, stringPage.find("</i>", pubLoc) - pubLoc);
        artRef = stringPage.substr(stringPage.find("<code>", pubLoc) + 6, stringPage.find("&nbsp;", pubLoc) - stringPage.find("<code>", pubLoc) - 6);
        publicationsOnNIPNE.push_back(title);
        articlesOnNIPNE.push_back(artRef);
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
    int newPub = 0;
    for (auto& pubINSPIRE : publicationMap) {
        long index = 0;
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
            if (nipTitle.compare(insTitle) == 0)
                break;
            else
                index++;
        }
        if (index == publicationsOnNIPNE.size()) {
            ++newPub;
        }
    }
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