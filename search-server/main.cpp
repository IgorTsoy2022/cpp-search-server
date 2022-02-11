#include <cstdlib>
#include <iomanip> // for boolalpha
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <cmath>

using namespace std;

const int MAX_RESULT_DOCUMENT_COUNT = 5;

struct Document {
    int id;
    double relevance;
    int rating;
};

enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED
};

vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else { word += c; }
    }
    if (!word.empty()) { words.push_back(word); }
    return words;
}

class SearchServer {
public:
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }    

    void AddDocument(int document_id, const string& document,
                     DocumentStatus status,
                     const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        const double inv_word_count = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] +=
                                    inv_word_count;
        }
        documents_.emplace(document_id, 
            DocumentData{ ComputeAverageRating(ratings), 
                          status });
    }

    template <typename DocumentPredicate>
    vector<Document> FindTopDocuments(const string& raw_query,
                     DocumentPredicate document_predicate) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = 
             FindAllDocuments(query, document_predicate);

        sort(matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {
                if (abs(lhs.relevance - rhs.relevance) < 1e-6) {
                    return lhs.rating > rhs.rating;
                } else {
                    return lhs.relevance > rhs.relevance;
                }
             });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

    vector<Document> FindTopDocuments(const string& raw_query,
                                      DocumentStatus status) const {
        return FindTopDocuments(raw_query,
               [&status]([[maybe_unused]] int document_id,
                         DocumentStatus document_status,
                         [[maybe_unused]] int rating) {
                            return document_status == status; });    
    }

    vector<Document> FindTopDocuments(const string& raw_query) const {
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const {
        return documents_.size();
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(
        const string& raw_query, int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.clear();
                break;
            }
        }
        return {matched_words, documents_.at(document_id).status};
    }

private:
    struct DocumentData {
        int rating;
        DocumentStatus status;
    };

    set<string> stop_words_;
    map<string, map<int, double>> word_to_document_freqs_;
    map<int, DocumentData> documents_;

    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) { return 0; }
        int rating_sum = 0;
        for (const int rating : ratings) {
            rating_sum += rating;
        }
        return rating_sum / static_cast<int>(ratings.size());
    }

    struct QueryWord {
        string data;
        bool is_minus;
        bool is_stop;
    };

    QueryWord ParseQueryWord(string text) const {
        bool is_minus = false;
        // Word shouldn't be empty
        if (text[0] == '-') {
            is_minus = true;
            text = text.substr(1);
        }
        return {
            text,
            is_minus,
            IsStopWord(text)
        };
    }

    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };
    
    Query ParseQuery(const string& text) const {
        Query query;
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    query.minus_words.insert(query_word.data);
                } else {
                    query.plus_words.insert(query_word.data);
                }
            }
        }
        return query;
    }
    
    // Existence required
    double ComputeWordIDF(const string& word) const {
        return log(GetDocumentCount() * 1.0 / 
                   word_to_document_freqs_.at(word).size());
    }

    template <typename DocumentPredicate>
    vector<Document> FindAllDocuments(const Query& query,
                     DocumentPredicate document_predicate) const {
        map<int, double> document_to_relevance;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            const double dblIDF = ComputeWordIDF(word);
            for (const auto [document_id, term_freq] : 
                 word_to_document_freqs_.at(word)) {
                if (document_predicate(document_id,
                        documents_.at(document_id).status,
                        documents_.at(document_id).rating)) {
                    document_to_relevance[document_id] += 
                        term_freq * dblIDF;
                }
            }
        }

        for (const string& word : query.minus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            for (const auto [document_id, _] : 
                 word_to_document_freqs_.at(word)) {
                document_to_relevance.erase(document_id);
            }
        }

        vector<Document> matched_documents;
        for (const auto [document_id, relevance] : 
             document_to_relevance) {
            matched_documents.push_back({
                document_id,
                relevance,
                documents_.at(document_id).rating
            });
        }
        return matched_documents;
    }
};

/* 
   Подставьте сюда вашу реализацию макросов 
   ASSERT, ASSERT_EQUAL, ASSERT_EQUAL_HINT, ASSERT_HINT и RUN_TEST
*/

void AssertImpl(const bool expression, const string& strExpression,
                const string& file, const string& function,
                const unsigned line, const string& hint) {
    if (!expression) {
        cout << boolalpha;
        cout << file << "("s << line << "): "s;
        cout << function << ": "s;
        cout << "ASSERT("s << strExpression << ") failed."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

#define ASSERT(expression) AssertImpl((expression), #expression, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_HINT(expression, hint) AssertImpl((expression), #expression, __FILE__, __FUNCTION__, __LINE__, (hint))

template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u,
                     const string& t_str, const string& u_str,
                     const string& file,  const string& func,
                     unsigned line, const string& hint) {
    if (t != u) {
        cout << boolalpha;
        cout << file << "("s << line << "): "s << func << ": "s;
        cout << "ASSERT_EQUAL("s << t_str << ", "s << u_str;
        cout << ") failed: "s << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cout << " Hint: "s << hint;
        }
        cout << endl;
        abort();
    }
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))


// -------- Начало модульных тестов поисковой системы ----------

// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов
void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.AddDocument(doc_id, content,
                           DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content,
                           DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(),
                    "Stop words must be excluded from documents"s);
    }
}

void TestExcludeMinusWords() {
    string strDoc[3];
    strDoc[0] = "the key to understand this language is calculus"s;
    strDoc[1] = "calculus allows us to see the true beaty of nature"s;
    strDoc[2] = "calculus makes the predictions of modern physics possible"s;
    string strStop_words = "the to this is us the of"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.SetStopWords(strStop_words);
        for (int intI = 0; intI < 3; ++intI) {
            server.AddDocument(intI, strDoc[intI],
                               DocumentStatus::ACTUAL, ratings);
        }
        
        auto found_docs = 
                   server.FindTopDocuments("calculus -to"s);
        ASSERT_EQUAL(found_docs.size(), 3);
        found_docs = server.FindTopDocuments("calculus -modern"s);
        ASSERT_EQUAL(found_docs.size(), 2);
    }
}

void TestMatchWords() {
    string strDoc[3];
    strDoc[0] = "the key to understand this language is calculus"s;
    strDoc[1] = "calculus allows us to see the true beaty of nature"s;
    strDoc[2] = "calculus makes the predictions of modern physics possible"s;
    string strStop_words = "the to this is us the of"s;
    const vector<int> ratings = {1, 2, 3};
    {
        SearchServer server;
        server.SetStopWords(strStop_words);
        for (int intI = 0; intI < 3; ++intI) {
            server.AddDocument(intI, strDoc[intI],
                               DocumentStatus::ACTUAL, ratings);
        }

        string strQuery_words = "calculus nature -modern"s;

        const auto [words0, status0] = server.MatchDocument(strQuery_words, 0);
        ASSERT_EQUAL(words0.front(), "calculus"s);
        const auto [words1, status1] = server.MatchDocument(strQuery_words, 1);
        ASSERT_EQUAL(words1.front(), "calculus"s);
        ASSERT_EQUAL(words1.back(), "nature"s);
        const auto [words2, status2] = server.MatchDocument(strQuery_words, 2);
        ASSERT(words2.empty());
    }
}

void TestSort() {
    string strDoc[3];
    strDoc[0] = "the key to understand this language is calculus"s;
    strDoc[1] = "calculus allows us to see the true beaty of nature"s;
    strDoc[2] = "calculus makes the predictions of modern physics possible"s;
    const string strStop_words = "the to this is us the of"s;
    vector<int> ratings[3];
    ratings[0] = {1, 12, 2};
    ratings[1] = {24, 12, 0, -2, 55};
    ratings[2] = {2, 8, 28, 33, 6, 9};
    {
        SearchServer server;
        server.SetStopWords(strStop_words);
        for (int intI = 0; intI < 3; ++intI) {
            server.AddDocument(intI, strDoc[intI],
                               DocumentStatus::ACTUAL, ratings[intI]);
        }

        string strQuery_words = "calculus nature modern physics"s;

        double dblRelevance = 0.0;
        bool blnNext = false;
        for (const auto document : 
            server.FindTopDocuments(strQuery_words)) {

            if (blnNext) {
                ASSERT(document.relevance < dblRelevance);
            } else { blnNext = true; }

            dblRelevance = document.relevance;
        }
    }
}

int ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) { return 0; }
    int intTotal = 0;
    for (const int rating : ratings) {
         intTotal += rating;
    }
    return intTotal / static_cast<int>(ratings.size());
}

void TestRatings() {
    string strDoc[3];
    strDoc[0] = "the key to understand this language is calculus"s;
    strDoc[1] = "calculus allows us to see the true beaty of nature"s;
    strDoc[2] = "calculus makes the predictions of modern physics possible"s;
    const string strStop_words = "the to this is us the of"s;
    int intRatings[3];
    vector<int> ratings[3];
    ratings[0] = {1, 12, 2};
    ratings[1] = {24, 12, 0, -2, 55};
    ratings[2] = {2, 8, 28, 33, 6, 9};
    {
        SearchServer server;
        server.SetStopWords(strStop_words);
        for (int intI = 0; intI < 3; ++intI) {
            server.AddDocument(intI, strDoc[intI],
                               DocumentStatus::ACTUAL, ratings[intI]);
            intRatings[intI] = ComputeAverageRating(ratings[intI]);
        }

        string strQuery_words = "calculus nature modern physics"s;

        int intDocuments = 0;
        for (const auto document : 
             server.FindTopDocuments(strQuery_words)) {
            ++intDocuments;
            ASSERT(intRatings[document.id] == document.rating);
        }
        ASSERT(intDocuments == 3);
    }
}

void TestPredicates() {
    string strDoc[6];
    strDoc[0] = "the key to understand this language is calculus"s;
    strDoc[1] = "calculus allows us to see the true beaty of nature"s;
    strDoc[2] = "calculus makes the predictions of modern physics possible"s;
    strDoc[3] = "people tend to enjoy what they are really good"s;
    strDoc[4] = "when you are awake you know you are awake"s;
    strDoc[5] = "a very worthy sum to a very worthy question"s;
    const string strStop_words = "the to this is us the of what they are a"s;
    vector<int> ratings[6];
    ratings[0] = {1, 12, 2};
    ratings[1] = {24, 12, 0, -2, 55};
    ratings[2] = {2, 8, 28, 33, 6, 9};
    ratings[3] = {1, 2, 3};
    ratings[4] = {2, 5, 6};
    ratings[5] = {3, 8, 12};

    {
        SearchServer server;
        server.SetStopWords(strStop_words);

        server.AddDocument(0, strDoc[0], DocumentStatus::ACTUAL,
                           ratings[0]);
        server.AddDocument(1, strDoc[1], DocumentStatus::ACTUAL,
                           ratings[1]);
        server.AddDocument(2, strDoc[2], DocumentStatus::ACTUAL,
                           ratings[2]);
        server.AddDocument(3, strDoc[3], DocumentStatus::BANNED,
                           ratings[3]);
        server.AddDocument(4, strDoc[4], DocumentStatus::REMOVED,
                           ratings[4]);
        server.AddDocument(5, strDoc[5], DocumentStatus::IRRELEVANT,
                           ratings[5]);

        string strQuery_words = "calculus nature modern physics"s;

        ASSERT_EQUAL(server.FindTopDocuments(strQuery_words).size(), 3);
        ASSERT_EQUAL(server.FindTopDocuments(strQuery_words,
                    [](int, DocumentStatus, int rating) {
                       return rating > 5;
                    }).size(), 2);

        ASSERT_EQUAL(server.FindTopDocuments("people tend"s,
                    DocumentStatus::BANNED).size(), 1);

    }
}

vector<string> StringToVector(const string& text) {
    vector<string> vecWords;
    string strWord;
    for (const char chrC : text) {
        if (chrC == ' ') {
            if (!strWord.empty()) {
                vecWords.push_back(strWord);
                strWord.clear();
            }
        }
        else { strWord += chrC; }
    }
    if (!strWord.empty()) { vecWords.push_back(strWord); }
    return vecWords;
}

set<string> StringToSet(const string& text) {
    set<string> setWords;
    for (const string& strWord : StringToVector(text)) {
        setWords.insert(strWord);
    }
    return setWords;
}

vector<string> StringToVectorNoStop(const string& text,
               const set<string>& stop_words) {
    vector<string> vecWords;
    for (const string& strWord : StringToVector(text)) {
        if (stop_words.count(strWord) > 0) { continue; }
        vecWords.push_back(strWord);
    }
    return vecWords;
}

set<string> StringToSetNoStop(const string& text,
            const set<string>& stop_words) {
    set<string> setWords;
    for (const string& strWord : StringToVector(text)) {
        if (stop_words.count(strWord) > 0) { continue; }
        setWords.insert(strWord);
    }
    return setWords;
}

double ComputeWordIDF(const string& word, int documents,
       const map<string, map<int, double>>& word_documents) {
    return log(documents * 1.0 / word_documents.at(word).size());
}

void TestTF_IDF() {
    int intDocuments = 3;
    string strDoc[3];
    strDoc[0] = "the key to understand this language is calculus"s;
    strDoc[1] = "calculus allows us to see the true beaty of nature"s;
    strDoc[2] = "calculus makes the predictions of modern physics possible"s;
    const string strStop_words = "the to this is us the of"s;
    const set<string> setStop_words = StringToSet(strStop_words);
    vector<int> ratings[3];
    ratings[0] = {1, 12, 2};
    ratings[1] = {24, 12, 0, -2, 55};
    ratings[2] = {2, 8, 28, 33, 6, 9};
    map<string, map<int, double>> mapWord_documents;

    for (int intI = 0; intI < intDocuments; ++intI) {
        const vector<string> vecWords = 
              StringToVectorNoStop(strDoc[intI], setStop_words);
        const double dblInverse_ratio_of_word_count = 
                     1.0 / vecWords.size();
        for (const string& strWord : vecWords) {
            mapWord_documents[strWord][intI] +=
                dblInverse_ratio_of_word_count;
        }
    }

    string strQuery_plus_words_only = 
           "calculus nature modern physics"s;
    const set<string> setQuery_words = 
          StringToSetNoStop(strQuery_plus_words_only, setStop_words);
    map<int, double> mapDocument_relevances;

    for (const string& strWord : setQuery_words) {
        if (mapWord_documents.count(strWord) == 0) { continue; }
        const double dbl_IDF = ComputeWordIDF(strWord, intDocuments,
                                              mapWord_documents);
        for (const auto [intId, dblTF] :
                         mapWord_documents.at(strWord)) {
            mapDocument_relevances[intId] += dblTF * dbl_IDF;
        }
    }

    {
        SearchServer server;
        server.SetStopWords(strStop_words);

        server.AddDocument(0, strDoc[0], DocumentStatus::ACTUAL,
                           ratings[0]);
        server.AddDocument(1, strDoc[1], DocumentStatus::ACTUAL,
                           ratings[1]);
        server.AddDocument(2, strDoc[2], DocumentStatus::ACTUAL,
                           ratings[2]);

        for (const Document& docDocument : 
             server.FindTopDocuments(strQuery_plus_words_only)) {
            ASSERT(abs(mapDocument_relevances.at(docDocument.id)
                       - docDocument.relevance) < 1e-6);
        }
    }    
}

template <typename FunctionPredicate>
void RunTestImpl(const FunctionPredicate& function,
                 const string& function_name) {
    function();
    cerr << function_name << " OK" << endl;
}

#define RUN_TEST(test) RunTestImpl(test, #test)

// Функция TestSearchServer является точкой входа для запуска тестов

void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestExcludeMinusWords);
    RUN_TEST(TestMatchWords);
    RUN_TEST(TestSort);
    RUN_TEST(TestRatings);
    RUN_TEST(TestPredicates);
    RUN_TEST(TestTF_IDF);
}

// -------- Окончание модульных тестов поисковой системы --------

int main() {
    TestSearchServer();
    // Если вы видите эту строку, значит все тесты прошли успешно
    cout << "Search server testing finished"s << endl;
}
