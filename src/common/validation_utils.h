#pragma once

#include <string>

class ValidationUtils {
public:
    // Validate URL format
    static bool isValidUrl(const std::string& url);
    
    // Validate file path
    static bool isValidPath(const std::string& path);
    
    // Check if path is safe (no directory traversal)
    static bool isPathSafe(const std::string& path);
    
    // Sanitize filename by replacing invalid characters
    static std::string sanitizeFilename(const std::string& name);
    
    // Normalize proxy URL (add http:// prefix if protocol not specified)
    static std::string normalizeProxy(const std::string& proxy);
    
    // Check if file path is a temporary/incomplete file (.part, .f*.part, .temp, etc.)
    static bool isTemporaryFile(const std::string& file_path);
    
    // Check if file is an intermediate format that will be converted/deleted (.opus, .webm)
    // target_format: the user's selected output format (e.g., "mp3", "m4a")
    static bool isIntermediateFormat(const std::string& file_path, const std::string& target_format);
};

