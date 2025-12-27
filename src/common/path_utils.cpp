#include "path_utils.h"
#include <algorithm>
#include <string>

namespace PathUtils {

std::string normalizePath(const std::string& path) {
    if (path.empty()) return path;
    std::string normalized = path;
    
#ifdef _WIN32
    // Replace all forward slashes with backslashes on Windows
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    // Remove duplicate backslashes (except at the start for UNC paths like \\server\share)
    std::string result;
    bool is_unc = (normalized.length() > 2 && normalized[0] == '\\' && normalized[1] == '\\');
    if (is_unc) {
        // UNC path, keep first two backslashes
        result = "\\\\";
        for (size_t i = 2; i < normalized.length(); i++) {
            if (normalized[i] != '\\' || result.back() != '\\') {
                result += normalized[i];
            }
        }
    } else {
        // Regular path, remove duplicate backslashes
        for (size_t i = 0; i < normalized.length(); i++) {
            if (normalized[i] != '\\' || result.empty() || result.back() != '\\') {
                result += normalized[i];
            }
        }
    }
    normalized = result;
#else
    // Replace all backslashes with forward slashes on Unix
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    // Remove duplicate forward slashes (except at the start)
    std::string result;
    for (size_t i = 0; i < normalized.length(); i++) {
        if (normalized[i] != '/' || result.empty() || result.back() != '/') {
            result += normalized[i];
        }
    }
    normalized = result;
#endif
    return normalized;
}

std::string joinPath(const std::string& base, const std::string& part) {
    if (base.empty()) return normalizePath(part);
    std::string normalized_base = normalizePath(base);
    std::string normalized_part = normalizePath(part);
    
#ifdef _WIN32
    if (normalized_base.back() == '\\') {
        return normalized_base + normalized_part;
    }
    return normalized_base + "\\" + normalized_part;
#else
    if (normalized_base.back() == '/') {
        return normalized_base + normalized_part;
    }
    return normalized_base + "/" + normalized_part;
#endif
}

} // namespace PathUtils



