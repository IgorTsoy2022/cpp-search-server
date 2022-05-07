#include "log_duration.h"
#include "process_queries.h"
#include "search_server.h"

#include <random>

using namespace std::string_literals;

std::string
GenerateWord(std::mt19937& generator, int max_length) {
    const int length = 
          std::uniform_int_distribution(1, max_length)(generator);
    std::string word;
    word.reserve(length);
    for (int i = 0; i < length; ++i) {
        word.push_back(
            std::uniform_int_distribution('a', 'z')(generator));
    }
    return word;
}

std::vector<std::string>
GenerateDictionary(std::mt19937& generator, int word_count,
                   int max_length) {
    std::vector<std::string> words;
    words.reserve(word_count);
    for (int i = 0; i < word_count; ++i) {
        words.push_back(GenerateWord(generator, max_length));
    }
    words.erase(std::unique(words.begin(), words.end()),
                            words.end());
    return words;
}

std::string GenerateQuery(std::mt19937& generator, 
            const std::vector<std::string>& dictionary,
            int word_count, double minus_prob = 0.0) {
    std::string query;
    for (int i = 0; i < word_count; ++i) {
        if (!query.empty()) {
            query.push_back(' ');
        }
        if (std::uniform_real_distribution<>(0, 1)(generator)
            < minus_prob) {
            query.push_back('-');
        }
        query += dictionary[std::uniform_int_distribution<int>(0, dictionary.size() - 1)(generator)];
    }
    return query;
}

std::vector<std::string>
GenerateQueries(std::mt19937& generator, 
                const std::vector<std::string>& dictionary,
                int query_count, int max_word_count) {
    std::vector<std::string> queries;
    queries.reserve(query_count);
    for (int i = 0; i < query_count; ++i) {
        queries.push_back(GenerateQuery(generator, dictionary,
                                        max_word_count));
    }
    return queries;
}

template <typename ExecutionPolicy>
void Test(std::string_view mark,
          const SearchServer& search_server,
          const std::vector<std::string>& queries,
          ExecutionPolicy&& policy) {
    LOG_DURATION(mark);
    double total_relevance = 0;
    for (const std::string_view query : queries) {
        for (const auto& document :
             search_server.FindTopDocuments(policy, query)) {
            total_relevance += document.relevance;
        }
    }
    std::cout << total_relevance << std::endl;
}

#define TEST(policy) Test(#policy, search_server, queries, std::execution::policy)

int main() {

    SearchServer ss("and with"s);

    int id = 0;
    for (const std::string& text : {
            "white cat and yellow hat"s,
            "curly cat curly tail"s,
            "nasty dog with big eyes"s,
            "nasty pigeon john"s,
            }) {
        ss.AddDocument(++id, text, DocumentStatus::ACTUAL, {1, 2});
    }

    std::cout << "ACTUAL by default:"s << std::endl;
    for (const Document& document : 
         ss.FindTopDocuments("curly nasty cat"s)) {
        PrintDocument(document);
    }

    std::cout << "BANNED:"s << std::endl;
    // последовательная версия
    for (const Document& document :
         ss.FindTopDocuments(std::execution::seq,
                             "curly nasty cat"s,
                             DocumentStatus::BANNED)) {
        PrintDocument(document);
    }

    std::cout << "Even ids:"s << std::endl;
    // параллельная версия
    for (const Document& document : 
         ss.FindTopDocuments(std::execution::par,
                             "curly nasty cat"s,
         [](int document_id, DocumentStatus status, int rating) {
             return document_id % 2 == 0; })) {
        PrintDocument(document);
    }

    std::mt19937 generator;

    const auto dictionary = GenerateDictionary(generator, 1000,
                                               10);
    const auto documents = GenerateQueries(generator, dictionary,
                                           10'000, 70);
    SearchServer search_server(dictionary[0]);
    for (size_t i = 0; i < documents.size(); ++i) {
        search_server.AddDocument(i, documents[i],
                                  DocumentStatus::ACTUAL,
                                  {1, 2, 3});
    }

    const auto queries = GenerateQueries(generator, dictionary,
                                         100, 70);
    TEST(seq);
    TEST(par);

    return 0;
}