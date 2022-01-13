#include <iostream>
#include <stdio.h>
#include <string>
#include <fstream>
#include <map>
#include <vector>
#include <curl/curl.h>
#include <json/json.h>
#include <boost/program_options.hpp>

static std::string stringPage;
static std::map<long, std::vector<std::string>> publicationMap;

namespace po = boost::program_options;

static size_t getString(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    stringPage.append(static_cast<char*>(contents), realsize);
    return realsize;
}

void login(std::string uname = "fake", std::string pass = "fake");

int main(int argc, char* argv[]) {
    //program options
    std::string author;
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
    }
    else {
        std::cout << "Username or password not set!" << std::endl;
        return EXIT_FAILURE;
    }


    //cURL query
    std::string queryLine;
    unsigned int page = 1;
    static int noArticles = 0;
    CURL* curl;
    CURLcode res;
    Json::FastWriter stringWriter;//Json::Value to string
    Json::Value root;
    Json::Reader reader;

    while (false) {
        //std::cout << page << std::endl;
        queryLine = "https://inspirehep.net/api/literature?sort=earliestdate&size=10&page=";
        queryLine += std::to_string(page) + "&q=find%20a%20" + author + "%20and%20tc%20p";
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        stringPage.clear();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, queryLine.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getString);

            /* Perform the request, res will get the return code */
            res = curl_easy_perform(curl);
            
            /* Check for errors */
            if (res != CURLE_OK)
                fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));

            /* always cleanup */
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
            ref_art = j_title + stringWriter.write(index["metadata"]["publication_info"][0]["journal_volume"]) + pub_year + stringWriter.write(index["metadata"]["publication_info"][0]["artid"]);
            //in ref_art, values come with \r - fix it
            noArticles++;

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
            std::cout << j_title << std::endl;
            
            authors.clear();
        }
        page++;
        queryLine.clear();
    }
    login();

    return 0;
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
        struct curl_slist* headers = NULL;
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, unamepass.c_str());
        curl_easy_setopt(curl, CURLOPT_URL, loginPage.c_str());
        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cout << "failed" << std::endl;
            curl_easy_strerror(res);
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    curl_global_cleanup();
}