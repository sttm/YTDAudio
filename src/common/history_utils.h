#pragma once

#include "types.h"
#include <string>
#include <ctime>
#include <functional>

namespace HistoryUtils {
    // Generate unique ID for HistoryItem
    // Format: timestamp_urlhash_taskpointer
    // This ensures uniqueness even if multiple tasks have the same URL
    std::string generateHistoryId(const std::string& url, void* task_pointer = nullptr);
    
    // Generate ID from existing timestamp and URL (for backward compatibility)
    std::string generateHistoryIdFromTimestamp(int64_t timestamp, const std::string& url);
}



