#include "search_server.h"
#include "paginator.h"
#include "request_queue.h"

using namespace std;

void AddDocument(SearchServer& search_server, int document_id,
                 const std::string& document,
                 DocumentStatus status,
                 const std::vector<int>& ratings) {
    try {
        search_server.AddDocument(document_id, document,
                                  status, ratings);
    } catch (const std::invalid_argument& e) {
        std::cout << "Ошибка добавления документа "s
                  << document_id << ": "s << e.what()
                  << std::endl;
    }
}

void FindTopDocuments(const SearchServer& search_server,
                      const std::string& raw_query) {
    std::cout << "Результаты поиска по запросу: "s
              << raw_query << std::endl;
    try {
        for (const Document& document :
             search_server.FindTopDocuments(raw_query)) {
            PrintDocument(document);
        }
    } catch (const std::invalid_argument& e) {
        std::cout << "Ошибка поиска: "s << e.what() << std::endl;
    }
}

void MatchDocuments(const SearchServer& search_server,
                    const std::string& query) {
    try {
        std::cout << "Матчинг документов по запросу: "s
                  << query << std::endl;
        const int document_count =
              search_server.GetDocumentCount();
        for (int index = 0; index < document_count; ++index) {
            const int document_id =
                  search_server.GetDocumentId(index);
            const auto [words, status] =
                  search_server.MatchDocument(query, document_id);
            PrintMatchDocumentResult(document_id, words, status);
        }
    } catch (const std::invalid_argument& e) {
        std::cout << "Ошибка матчинга документов на запрос "s
                  << query << ": "s << e.what() << std::endl;
    }
}

int main() {
    SearchServer search_server("and in at"s);
    RequestQueue request_queue(search_server);

    search_server.AddDocument(1, "curly cat curly tail"s,
                              DocumentStatus::ACTUAL, {7, 2, 7});
    search_server.AddDocument(2, "curly dog and fancy collar"s,
                              DocumentStatus::ACTUAL, {1, 2, 3});
    search_server.AddDocument(3, "big cat fancy collar "s,
                              DocumentStatus::ACTUAL, {1, 2, 8});
    search_server.AddDocument(4, "big dog sparrow Eugene"s,
                              DocumentStatus::ACTUAL, {1, 3, 2});
    search_server.AddDocument(5, "big dog sparrow Vasiliy"s,
                              DocumentStatus::ACTUAL, {1, 1, 1});

    // 1439 запросов с нулевым результатом
    for (int i = 0; i < 1439; ++i) {
        request_queue.AddFindRequest("empty request"s);
    }
    // все еще 1439 запросов с нулевым результатом
    request_queue.AddFindRequest("curly dog"s);
    // новые сутки, первый запрос удален, 1438 запросов с нулевым
    // результатом
    request_queue.AddFindRequest("big collar"s);
    // первый запрос удален, 1437 запросов с нулевым результатом
    request_queue.AddFindRequest("sparrow"s);
    std::cout << "Total empty requests: "s
              << request_queue.GetNoResultRequests()
              << std::endl;
    
    return 0;
}