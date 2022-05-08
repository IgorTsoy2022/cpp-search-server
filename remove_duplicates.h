#pragma once

#include "search_server.h"

void RemoveDuplicates(SearchServer& search_server) {
    for (const auto id : search_server.GetDuplicates()) {
        std::cout << "Found duplicate document id "s
                  << id << std::endl;
        search_server.RemoveDocument(id);
    }
}
