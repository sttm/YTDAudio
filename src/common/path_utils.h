#pragma once

#include <string>

namespace PathUtils {
    // Normalize path separators to platform-specific ones
    // On Windows: converts forward slashes to backslashes, handles UNC paths
    // On Unix: converts backslashes to forward slashes
    // Removes duplicate separators (except at start for UNC paths on Windows)
    std::string normalizePath(const std::string& path);
    
    // Join two path components with platform-specific separator
    std::string joinPath(const std::string& base, const std::string& part);
}



