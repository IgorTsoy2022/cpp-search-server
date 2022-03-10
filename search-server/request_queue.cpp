#include "request_queue.h"

std::vector<Document> RequestQueue::AddFindRequest(
                      const std::string& raw_query,
                      DocumentStatus status) {
    return AddFindRequest(raw_query,
           [status](int document_id,
                    DocumentStatus document_status,
                    int rating) {
           return document_status == status; });
}