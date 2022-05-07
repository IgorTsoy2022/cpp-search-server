#include "search_server.h"

// PUBLIC

void SearchServer::AddDocument(int document_id,
                   const std::string_view document,
                   DocumentStatus status,
                   const std::vector<int>& ratings) {

    if ((document_id < 0) ||
        (documents_.count(document_id) > 0)) {
        throw std::invalid_argument("Invalid document_id");
    }

    const auto words = SplitIntoWordsNoStop(document);
    const double inv_word_count = 1.0 / words.size();

    for (const auto word : words) {
        auto it = words_.insert(std::string(word));
        std::string_view sv_word = *(it.first);
        word_to_document_freqs_[sv_word][document_id]
            += inv_word_count;
        document_to_word_freqs_[document_id][sv_word]
            += inv_word_count;
    }

    documents_.emplace(document_id,
                       DocumentData
                       {ComputeAverageRating(ratings), status});

    document_ids_.push_back(document_id);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

std::vector<int>::const_iterator SearchServer::begin() const {
    return document_ids_.begin();
}

std::vector<int>::const_iterator SearchServer::end() const {
    return document_ids_.end();
}

bool SearchServer::CompareDocumentsWords(int id1, int id2) const {

    if ((id1 < 0) || (id2 < 0) || (documents_.count(id1) == 0) ||
        (documents_.count(id2) == 0)) {
        throw std::out_of_range("Invalid document_id");
    }

    const auto& word_freqs1 = document_to_word_freqs_.at(id1);
    const auto& word_freqs2 = document_to_word_freqs_.at(id2);
    if (word_freqs1.size() != word_freqs2.size()) {
        return false;
    }

    std::vector<std::string_view> w1(word_freqs1.size());
    std::transform(std::execution::par, 
                   word_freqs1.begin(), word_freqs1.end(),
                   w1.begin(), [](auto value) {
                                   return value.first;
                               });

    std::vector<std::string_view> w2(word_freqs2.size());
    std::transform(std::execution::par, 
                   word_freqs2.begin(), word_freqs2.end(),
                   w2.begin(), [](auto value) {
                                   return value.first;
                               });

    return (w1 == w2);
}

const std::map<std::string_view, double>&
SearchServer::GetWordFrequencies(int document_id) const {
    static std::map<std::string_view, double> result;
    if (documents_.count(document_id) > 0) {
        result = document_to_word_freqs_.at(document_id);
    } else {
        result.clear();
    }
    return result;
}

// RemoveDocument
void SearchServer::RemoveDocument(int document_id) {
    std::vector<int>::iterator
    it = find(document_ids_.begin(), document_ids_.end(),
              document_id);
    if (it == document_ids_.end()) {
        return;
    }

    for (auto [word, _] : document_to_word_freqs_[document_id]) {
        word_to_document_freqs_[word].erase(document_id);
        if (word_to_document_freqs_[word].empty()) {
            word_to_document_freqs_.erase(word);
        }
    }

    document_ids_.erase(it);
    documents_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
}

// RemoveDocument sequenced_policy
void SearchServer::RemoveDocument(
                   const std::execution::sequenced_policy& policy,
                   int document_id) {

    std::vector<int>::iterator
    it = find(policy, document_ids_.begin(), document_ids_.end(),
              document_id);
    if (it == document_ids_.end()) {
        return;
    }

    const auto& word_freqs = document_to_word_freqs_[document_id];

    for_each(policy, word_freqs.begin(), word_freqs.end(),
             [this, document_id](auto& cntr) {
                 word_to_document_freqs_[cntr.first].erase(document_id);
                 if (word_to_document_freqs_[cntr.first].empty()) {
                     word_to_document_freqs_.erase(cntr.first);
                 }
             });

    document_ids_.erase(it);
    documents_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
}

// RemoveDocument parallel_policy
void SearchServer::RemoveDocument(
                   const std::execution::parallel_policy& policy,
                   int document_id) {
    std::vector<int>::iterator
    it = find(policy, document_ids_.begin(), document_ids_.end(),
              document_id);
    if (it == document_ids_.end()) {
        return;
    }

    const auto& word_freqs = document_to_word_freqs_[document_id];

    std::vector<const std::string_view*>
    words(word_freqs.size(), nullptr);
    std::transform(policy, word_freqs.begin(), word_freqs.end(),
                   words.begin(),
                   [](auto& value) {
                       return &value.first;
                   });

    for_each(policy, words.begin(), words.end(),
             [this, document_id](auto& ptr) {
                 word_to_document_freqs_[*ptr].erase(document_id);
                 if (word_to_document_freqs_[*ptr].empty()) {
                     word_to_document_freqs_.erase(*ptr);
                 }
             });

    document_ids_.erase(it);
    documents_.erase(document_id);
    document_to_word_freqs_.erase(document_id);
}

// FindTopDocuments
std::vector<Document>
SearchServer::FindTopDocuments(
              const std::string_view raw_query,
              DocumentStatus status) const {
    return FindTopDocuments(raw_query,
           [status](int document_id,
                    DocumentStatus document_status,
                    int rating) {
                        return document_status == status;
                    });
}

std::vector<Document>
SearchServer::FindTopDocuments(
              const std::string_view raw_query) const {
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

// MatchDocument
std::tuple<std::vector<std::string_view>, DocumentStatus>
SearchServer::MatchDocument(const std::string_view raw_query,
                            int document_id) const {

    if ((document_id < 0) || (documents_.count(document_id) == 0)) {
        throw std::invalid_argument("document_id out of range"s);
    }

    const auto& word_freqs = document_to_word_freqs_.at(document_id);
    const auto& query = ParseQuery(raw_query);

    for (const std::string_view word : query.minus_words) {
        if (word_freqs.count(word) > 0) {
            return { {}, documents_.at(document_id).status };
        }
    }

    std::vector<std::string_view> matched_words;
    for (const std::string_view word : query.plus_words) {
        if (word_freqs.count(word) > 0) {
            matched_words.push_back(word);
        }
    }

    return { matched_words, documents_.at(document_id).status };
}

// MatchDocument sequenced_policy
std::tuple<std::vector<std::string_view>, DocumentStatus>
SearchServer::MatchDocument(
              const std::execution::sequenced_policy& policy,
              const std::string_view raw_query,
              int document_id) const {

    if ((document_id < 0) || (documents_.count(document_id) == 0)) {
        throw std::invalid_argument("document_id out of range"s);
    }

    const auto& word_freqs = document_to_word_freqs_.at(document_id);
    const auto& query = ParseQuery(policy, raw_query);

    if (std::any_of(policy,
                    query.minus_words.begin(),
                    query.minus_words.end(),
                    [&word_freqs](const std::string_view word) {
                        return word_freqs.count(word) > 0;
                    })) {
        return { {}, documents_.at(document_id).status };
    }

    std::vector<std::string_view> matched_words;
    matched_words.reserve(query.plus_words.size());
    std::copy_if(policy,
                 query.plus_words.begin(),
                 query.plus_words.end(),
                 std::back_inserter(matched_words),
                 [&word_freqs](const std::string_view word) {
                     return word_freqs.count(word) > 0;
                 });

/*
    for (const std::string_view word : query.minus_words) {
        if (word_freqs.count(word) > 0) {
            return { {}, documents_.at(document_id).status };
        }
    }

    std::vector<std::string_view> matched_words;
    for (const std::string_view word : query.plus_words) {
        if (word_freqs.count(word) > 0) {
            matched_words.push_back(word);
        }
    }
*/

    return { matched_words, documents_.at(document_id).status };

}

// MatchDocument parallel_policy
std::tuple<std::vector<std::string_view>, DocumentStatus>
SearchServer::MatchDocument(
              const std::execution::parallel_policy& policy,
              const std::string_view raw_query,
              int document_id) const {

    if ((document_id < 0) || (documents_.count(document_id) == 0)) {
        throw std::invalid_argument("document_id out of range"s);
    }

    const auto& word_freqs = document_to_word_freqs_.at(document_id);
    const auto& query = ParseQuery(policy, raw_query);

    if (std::any_of(//policy,
                    query.minus_words.begin(),
                    query.minus_words.end(),
                    [&word_freqs](const std::string_view word) {
                        return word_freqs.count(word) > 0;
                    })) {
        return { {}, documents_.at(document_id).status };
    }

    std::vector<std::string_view> matched_words;
    matched_words.reserve(query.plus_words.size());
    std::copy_if(//policy,
                 query.plus_words.begin(),
                 query.plus_words.end(),
                 std::back_inserter(matched_words),
                 [&word_freqs](const std::string_view word) {
                     return word_freqs.count(word) > 0;
                 });

    std::sort(policy,
              matched_words.begin(),
              matched_words.end());
    auto it = std::unique(policy,
                          matched_words.begin(),
                          matched_words.end());
    matched_words.erase(it, matched_words.end());

    return { matched_words, documents_.at(document_id).status };
}

// PRIVATE

bool SearchServer::IsStopWord(const std::string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(const std::string_view word) {
    return std::none_of(word.begin(), word.end(),
                        [](char c) {
                            return c >= '\0' && c < ' ';
                        });
}

bool SearchServer::IsNonValidWord(const std::string_view word) {
    return std::any_of(word.begin(), word.end(),
                       [](char c) {
                           return c >= '\0' && c < ' ';
                       });
}

std::vector<std::string_view>
SearchServer::SplitIntoWordsNoStop(
              const std::string_view text) const {
    std::vector<std::string_view> words;
    for (const auto word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw std::invalid_argument(
                       "Word "s + std::string(word) + " is invalid"s);
        }
        if (!IsStopWord(word) && word.size() > 0) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(
                  const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    return std::accumulate(ratings.begin(), ratings.end(), 0) /
           static_cast<int>(ratings.size());
}

double SearchServer::ComputeWordInverseDocumentFreq(
                     const std::string_view word) const {
    return log(GetDocumentCount() * 1.0 /
               word_to_document_freqs_.at(word).size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(
                        const std::string_view text) const {
    if (text.empty()) {
        throw std::invalid_argument("Query word is empty"s);
    }

    std::string_view word = text;
    bool is_minus = false;
    if (word[0] == '-') {
        is_minus = true;
        word = word.substr(1);
    }
    if (word.empty() || word[0] == '-' || IsNonValidWord(word)) {
        throw std::invalid_argument(
                   "Query word "s + std::string(text) +
                   " is invalid"s);
    }
    return { word, is_minus, IsStopWord(word) };
}

// ParseQuery
SearchServer::Query SearchServer::ParseQuery(
                    const std::string_view text) const {
    Query result;
    for (const std::string_view word : SplitIntoWords(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            }
            else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }

    std::sort(result.minus_words.begin(),
              result.minus_words.end());
    auto it = std::unique(result.minus_words.begin(),
                          result.minus_words.end());
    result.minus_words.resize(
           std::distance(result.minus_words.begin(), it));

    std::sort(result.plus_words.begin(),
              result.plus_words.end());
    it = std::unique(result.plus_words.begin(),
                     result.plus_words.end());
    result.plus_words.resize(
           std::distance(result.plus_words.begin(), it));        

    return result;
}

// ParseQuery sequenced_policy
SearchServer::Query SearchServer::ParseQuery(
                    const std::execution::sequenced_policy& policy,
                    const std::string_view text) const {
    Query result;
    bool has_minus = false;
    bool has_plus = false;
    const auto& words = SplitIntoWords(text);
    for_each(policy, words.begin(), words.end(),
             [&](std::string_view word) {
                 const auto query_word = ParseQueryWord(word);
                 if (!query_word.is_stop) {
                     if (query_word.is_minus) {
                         has_minus = true;
                         result.minus_words.push_back(query_word.data);
                     } else {
                         has_plus = true;
                         result.plus_words.push_back(query_word.data);
                     }
                 }
             });

    if (has_minus) {
        std::sort(policy, result.minus_words.begin(),
                  result.minus_words.end());
        auto it = std::unique(policy, result.minus_words.begin(),
                              result.minus_words.end());
        result.minus_words.erase(it, result.minus_words.end());
    }
    if (has_plus) {
        std::sort(policy, result.plus_words.begin(),
                  result.plus_words.end());
        auto it = std::unique(policy, result.plus_words.begin(),
                              result.plus_words.end());
        result.plus_words.erase(it, result.plus_words.end());
    }
    return result;
}

// ParseQuery parallel_policy
SearchServer::Query SearchServer::ParseQuery(
                    const std::execution::parallel_policy& policy,
                    const std::string_view text) const {
    Query result;
    const auto& words = SplitIntoWords(text);
    for_each(words.begin(), words.end(),
             [this, &result](auto& word) {
                 const auto query_word = ParseQueryWord(word);
                 if (!query_word.is_stop) {
                     if (query_word.is_minus) {
                         result.minus_words.push_back(query_word.data);
                     } else {
                         result.plus_words.push_back(query_word.data);
                     }
                 }
             });
    return result;
}