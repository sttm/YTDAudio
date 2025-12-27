#include "history_utils.h"
#include <sstream>
#include <iomanip>

namespace HistoryUtils {

std::string generateHistoryId(const std::string& url, void* task_pointer) {
    int64_t now = time(nullptr);
    std::hash<std::string> hasher;
    size_t url_hash = hasher(url);
    
    std::ostringstream oss;
    oss << now << "_" << url_hash;
    if (task_pointer) {
        oss << "_" << reinterpret_cast<uintptr_t>(task_pointer);
    }
    return oss.str();
}

std::string generateHistoryIdFromTimestamp(int64_t timestamp, const std::string& url) {
    std::hash<std::string> hasher;
    size_t url_hash = hasher(url);
    
    std::ostringstream oss;
    oss << timestamp << "_" << url_hash;
    return oss.str();
}

} // namespace HistoryUtils



