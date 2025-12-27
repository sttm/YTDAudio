#pragma once

#include <string>
#include <cstdint>

namespace JsonUtils {
    // Escape special characters in JSON string
    std::string escapeJsonString(const std::string& str);
    
    // Unescape JSON string (including Unicode escape sequences like \uXXXX)
    std::string unescapeJsonString(const std::string& str);
    
    // Extract JSON string value by field name
    // Returns empty string if field not found or invalid
    std::string extractJsonString(const std::string& json, const std::string& field_name);
    
    // Extract JSON integer value (int64_t) by field name
    // Returns 0 if field not found or invalid
    int64_t extractJsonInt64(const std::string& json, const std::string& field_name);
    
    // Extract JSON integer value (int) by field name
    // Returns 0 if field not found or invalid
    int extractJsonInt(const std::string& json, const std::string& field_name);
    
    // Extract JSON double value by field name
    // Returns 0.0 if field not found or invalid
    double extractJsonDouble(const std::string& json, const std::string& field_name);
    
    // Extract thumbnail URL from JSON (supports SoundCloud and YouTube)
    // For SoundCloud: extracts from thumbnails array (prefers t67x67)
    // For YouTube: extracts from thumbnail field or builds from video ID
    // Returns empty string if not found
    std::string extractThumbnailUrl(const std::string& json);
}


