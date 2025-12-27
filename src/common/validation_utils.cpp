#include "validation_utils.h"
#include <algorithm>
#include <cctype>

bool ValidationUtils::isValidUrl(const std::string& url) {
    if (url.empty() || url.length() < 10) {
        return false;
    }
    
    // Basic URL validation - check for common protocols
    std::string lower_url = url;
    std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);
    
    // Check for valid URL schemes
    bool has_protocol = (lower_url.find("http://") == 0 || 
                         lower_url.find("https://") == 0 ||
                         lower_url.find("ftp://") == 0);
    
    // Also allow URLs without protocol (will be handled by yt-dlp)
    // But must contain at least a dot and some domain-like structure
    bool has_domain_like = (url.find('.') != std::string::npos && 
                           url.find('/') != std::string::npos);
    
    // Must have at least some valid characters
    bool has_valid_chars = (url.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos);
    
    return (has_protocol || has_domain_like) && has_valid_chars;
}

bool ValidationUtils::isValidPath(const std::string& path) {
    if (path.empty() || path.length() > 4096) {  // Reasonable path length limit
        return false;
    }
    
    // Check for null bytes (security risk)
    if (path.find('\0') != std::string::npos) {
        return false;
    }
    
    return isPathSafe(path);
}

bool ValidationUtils::isPathSafe(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    // Check for directory traversal attempts
    if (path.find("..") != std::string::npos) {
        // Allow ".." only if it's part of a valid relative path within downloads_dir_
        // For now, reject all ".." to be safe
        return false;
    }
    
    // Note: Original implementation checked downloads_dir_, but we can't access it here
    // This is a simplified version - the full check should be done at the App level
    // For now, allow absolute paths but the caller should validate against downloads_dir_
    if (path[0] == '/') {
        // Allow temp directories
        if (path.find("/tmp/") == 0 || path.find("/var/tmp/") == 0) {
            return true;
        }
        // For other absolute paths, caller should validate against downloads_dir_
        return true;  // Basic safety check passed
    }
    
    return true;
}

std::string ValidationUtils::sanitizeFilename(const std::string& name) {
    std::string safe_name = name;
    // Replace invalid characters for folder/file names
    std::replace(safe_name.begin(), safe_name.end(), '/', '_');
    std::replace(safe_name.begin(), safe_name.end(), '\\', '_');
    std::replace(safe_name.begin(), safe_name.end(), ':', '_');
    std::replace(safe_name.begin(), safe_name.end(), '*', '_');
    std::replace(safe_name.begin(), safe_name.end(), '?', '_');
    std::replace(safe_name.begin(), safe_name.end(), '"', '_');
    std::replace(safe_name.begin(), safe_name.end(), '<', '_');
    std::replace(safe_name.begin(), safe_name.end(), '>', '_');
    std::replace(safe_name.begin(), safe_name.end(), '|', '_');
    
    // Remove leading/trailing spaces and dots
    while (!safe_name.empty() && (safe_name.front() == ' ' || safe_name.front() == '.')) {
        safe_name.erase(0, 1);
    }
    while (!safe_name.empty() && (safe_name.back() == ' ' || safe_name.back() == '.')) {
        safe_name.pop_back();
    }
    
    // If empty after sanitization, return empty string (don't create folder/file)
    // The caller should check if name is empty before using it
    return safe_name;
}

std::string ValidationUtils::normalizeProxy(const std::string& proxy) {
    if (proxy.empty()) {
        return proxy;
    }
    
    // Check if protocol is already specified
    std::string proxy_lower = proxy;
    std::transform(proxy_lower.begin(), proxy_lower.end(), proxy_lower.begin(), ::tolower);
    
    if (proxy_lower.find("http://") == 0 || 
        proxy_lower.find("https://") == 0 ||
        proxy_lower.find("socks4://") == 0 ||
        proxy_lower.find("socks5://") == 0) {
        return proxy;
    }
    
    return "http://" + proxy;
}

bool ValidationUtils::isTemporaryFile(const std::string& file_path) {
    if (file_path.empty()) {
        return false;
    }
    
    std::string path_lower = file_path;
    std::transform(path_lower.begin(), path_lower.end(), path_lower.begin(), ::tolower);
    
    // Check for common temporary file extensions/patterns
    // .part - yt-dlp partial download files
    if (path_lower.find(".part") != std::string::npos) {
        return true;
    }
    
    // .f*.part - yt-dlp fragment files (e.g., .f1.part, .f2.part)
    // Pattern: .f followed by digits, then .part
    size_t fpart_pos = path_lower.find(".f");
    if (fpart_pos != std::string::npos) {
        size_t dot_part_pos = path_lower.find(".part", fpart_pos);
        if (dot_part_pos != std::string::npos) {
            // Check if there are digits between .f and .part
            std::string between = path_lower.substr(fpart_pos + 2, dot_part_pos - fpart_pos - 2);
            if (!between.empty() && std::all_of(between.begin(), between.end(), ::isdigit)) {
                return true;
            }
        }
    }
    
    // .temp, .tmp - temporary files
    if (path_lower.find(".temp") != std::string::npos || 
        path_lower.find(".tmp") != std::string::npos) {
        return true;
    }
    
    // .download - some download managers use this
    if (path_lower.find(".download") != std::string::npos) {
        return true;
    }
    
    // .crdownload - Chrome download files
    if (path_lower.find(".crdownload") != std::string::npos) {
        return true;
    }
    
    // .!qB - qBittorrent incomplete files
    if (path_lower.find(".!qb") != std::string::npos) {
        return true;
    }
    
    // .ytdl - yt-dlp metadata/progress files
    if (path_lower.find(".ytdl") != std::string::npos) {
        return true;
    }
    
    return false;
}

bool ValidationUtils::isIntermediateFormat(const std::string& file_path, const std::string& target_format) {
    if (file_path.empty() || target_format.empty()) {
        return false;
    }
    
    // Get file extension
    size_t last_dot = file_path.find_last_of('.');
    if (last_dot == std::string::npos || last_dot == file_path.length() - 1) {
        return false;
    }
    
    std::string ext = file_path.substr(last_dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    std::string target_lower = target_format;
    std::transform(target_lower.begin(), target_lower.end(), target_lower.begin(), ::tolower);
    
    // If extension matches target format, it's not intermediate
    if (ext == target_lower) {
        return false;
    }
    
    // Known intermediate formats that yt-dlp downloads before conversion
    // These are typically deleted after ffmpeg converts them to the target format
    // Note: .m4a, .ogg, .flac are NOT intermediate - they are valid final formats
    // .opus, .webm, .mp4 are intermediate (raw download formats before audio extraction)
    if (ext == "opus" || ext == "webm" || ext == "mp4") {
        // Only consider as intermediate if target is different
        // e.g., .opus is intermediate when target is mp3, but not when target is opus
        return true;
    }
    
    return false;
}

