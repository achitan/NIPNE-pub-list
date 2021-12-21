#include <iostream>
#include <stdio.h>
#include <string>
#include <fstream>
#include <map>
#include <vector>
#include <curl/curl.h>
#include <json/json.h>

static std::string stringPage;
static std::map<long, std::vector<std::string>> publicationMap;

static size_t getString(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    stringPage.append(static_cast<char*>(contents), realsize);
    return realsize;
}

int main(int argc, char* argv[]) {
    std::string queryLine;
    unsigned int page = 1;
    queryLine += std::to_string(page) + "&q=find%20a%20chitan%2C%20a%20and%20tc%20p";
    CURL* curl;
    CURLcode res;
    std::ofstream outputFile("out.txt", std::ios::out);
    Json::FastWriter stringWriter;//Json::Value to string
    Json::Value root;
    Json::Reader reader;

    while (true) {
        std::cout << page << std::endl;
        queryLine = "https://inspirehep.net/api/literature?sort=earliestdate&size=10&page=";
        queryLine += std::to_string(page) + "&q=find%20a%20chitan%2C%20a%20and%20tc%20p";
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        stringPage.clear();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, queryLine.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getString);
            
#ifdef SKIP_PEER_VERIFICATION
            /*
             * If you want to connect to a site who is not using a certificate that is
             * signed by one of the certs in the CA bundle you have, you can skip the
             * verification of the server's certificate. This makes the connection
             * A LOT LESS SECURE.
             *
             * If you have a CA cert for the server stored someplace else than in the
             * default bundle, then the CURLOPT_CAPATH option might come handy for
             * you.
             */
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif

#ifdef SKIP_HOSTNAME_VERIFICATION
            /*
             * If the site you are connecting to uses a different host name that what
             * they have mentioned in their server certificate's commonName (or
             * subjectAltName) fields, libcurl will refuse to connect. You can skip
             * this check, but this will make the connection less secure.
             */
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif

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
        std::string id, dois, title, cit_no_self, cit, href, doc_type, coll, corp, author, pub_info, pub_year, j_title, ref_art;
        std::vector<std::string> authors;
        for (auto index : myArts) {
            id = stringWriter.write(index["id"]);
            id = id.substr(1, id.find('"', 1) - 1);
            title = stringWriter.write(index["metadata"]["titles"][0]["title"]);
            title = title.substr(1, title.find('"', 1) - 1);
            dois = stringWriter.write(index["metadata"]["dois"][0]["value"]);
            dois = dois.substr(1, dois.find('"', 1) - 1);
            cit_no_self = stringWriter.write(index["metadata"]["citation_count_without_self_citations"]);
            cit_no_self = cit_no_self.substr(1, cit_no_self.find('"', 1) - 1);
            cit = stringWriter.write(index["metadata"]["citation_count"]);
            cit = cit.substr(1, cit.find('"', 1) - 1);
            href = "https://inspirehep.net/api/literature/" + id;
            doc_type = stringWriter.write(index["metadata"]["document_type"]);
            doc_type = doc_type.substr(doc_type.find('"') + 1, doc_type.rfind('"') - 2);
            coll = stringWriter.write(index["metadata"]["collaborations"]);
            coll = coll.substr(coll.find(':') + 2, coll.rfind('"') - coll.find(':') - 2);
            //corp = (index["metadata"]["corporate_author"].asBool()) ? stringWriter.write(index["metadata"]["corporate_author"]) : "";
            
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
            pub_info = stringWriter.write(index["metadata"]["publication_info"][0]);
            pub_year = stringWriter.write(index["metadata"]["publication_info"][0]["year"]);
            j_title = stringWriter.write(index["metadata"]["publication_info"][0]["journal_title"]);
            j_title = j_title.substr(1, j_title.find('"', 1) - 1);
            ref_art = j_title + stringWriter.write(index["metadata"]["publication_info"][0]["journal_volume"]) + pub_year + stringWriter.write(index["metadata"]["publication_info"][0]["artid"]);
            //in ref_art, values come with \r - fix it

            

            authors.clear();
            outputFile << id << "\t" << title << std::endl;
        }
        page++;
        queryLine.clear();
    }
    outputFile.close();

    return 0;
}