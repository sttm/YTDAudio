#include "json_utils.h"

namespace JsonUtils {

std::string escapeJsonString(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    return result;
}

std::string unescapeJsonString(const std::string& str) {
    std::string result;
    result.reserve(str.length()); // Pre-allocate to avoid reallocations
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            if (str[i + 1] == '"') { result += '"'; i++; }
            else if (str[i + 1] == '\\') { 
                // Check if this is \\u (double backslash + u) - this means the u is escaped
                // In JSON, \\u means a literal backslash followed by u, not a Unicode escape
                // But if we see \\uXXXX, we should decode it as \uXXXX
                if (i + 2 < str.length() && str[i + 2] == 'u' && i + 6 < str.length()) {
                    // This is \\uXXXX - decode as \uXXXX (single backslash + unicode)
                    std::string hex = str.substr(i + 3, 4);
                    try {
                        unsigned int code_point = std::stoul(hex, nullptr, 16);
                        // Convert to UTF-8
                        if (code_point < 0x80) {
                            result += static_cast<char>(code_point);
                        } else if (code_point < 0x800) {
                            result += static_cast<char>(0xC0 | (code_point >> 6));
                            result += static_cast<char>(0x80 | (code_point & 0x3F));
                        } else if (code_point < 0xD800 || code_point >= 0xE000) {
                            result += static_cast<char>(0xE0 | (code_point >> 12));
                            result += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (code_point & 0x3F));
                        }
                        i += 6; // Skip \\uXXXX
                    } catch (...) {
                        // Invalid hex, treat as literal backslash
                        result += '\\';
                        i++; // Skip the second backslash, will process 'u' on next iteration
                    }
                } else {
                    // Regular escaped backslash
                    result += '\\'; 
                    i++; 
                }
            }
            else if (str[i + 1] == '/') { result += '/'; i++; }  // Handle escaped forward slash
            else if (str[i + 1] == 'n') { result += '\n'; i++; }
            else if (str[i + 1] == 'r') { result += '\r'; i++; }
            else if (str[i + 1] == 't') { result += '\t'; i++; }
            else if (str[i + 1] == 'u' && i + 5 < str.length()) {
                // Unicode escape sequence \uXXXX
                std::string hex = str.substr(i + 2, 4);
                try {
                    unsigned int code_point = std::stoul(hex, nullptr, 16);
                    // Convert to UTF-8
                    if (code_point < 0x80) {
                        result += static_cast<char>(code_point);
                    } else if (code_point < 0x800) {
                        result += static_cast<char>(0xC0 | (code_point >> 6));
                        result += static_cast<char>(0x80 | (code_point & 0x3F));
                    } else if (code_point < 0xD800 || code_point >= 0xE000) {
                        result += static_cast<char>(0xE0 | (code_point >> 12));
                        result += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (code_point & 0x3F));
                    }
                    i += 5; // Skip \uXXXX
                } catch (...) {
                    // Invalid hex, keep as is
                    result += str[i];
                }
            }
            else { result += str[i]; }
        } else {
            result += str[i];
        }
    }
    return result;
}

std::string extractJsonString(const std::string& json, const std::string& field_name) {
    std::string search = "\"" + field_name + "\":";
    size_t field_pos = json.find(search);
    if (field_pos == std::string::npos) return "";
    
    // Skip colon and any whitespace after it
    size_t pos = field_pos + search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    
    size_t start = json.find('"', pos);
    if (start == std::string::npos) return "";
    start++; // Skip opening quote
    
    // Find end quote, handling escaped quotes
    size_t end = start;
    while (end < json.length()) {
        if (json[end] == '"' && (end == start || json[end - 1] != '\\')) {
            break;
        }
        end++;
    }
    
    if (end > start && end < json.length()) {
        std::string raw_value = json.substr(start, end - start);
        return unescapeJsonString(raw_value);
    }
    return "";
}

int64_t extractJsonInt64(const std::string& json, const std::string& field_name) {
    std::string search = "\"" + field_name + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    
    size_t start = pos + search.length();
    // Skip whitespace
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t')) {
        start++;
    }
    // Find end (comma, closing brace, or newline)
    size_t end = start;
    while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != '\n') {
        end++;
    }
    if (end > start) {
        std::string value_str = json.substr(start, end - start);
        // Check if it's not "null" or "None"
        if (value_str != "null" && value_str != "None" && value_str.find("null") == std::string::npos) {
            try {
                return std::stoll(value_str);
            } catch (...) {
                // Ignore parse errors
            }
        }
    }
    return 0;
}

int extractJsonInt(const std::string& json, const std::string& field_name) {
    return static_cast<int>(extractJsonInt64(json, field_name));
}

double extractJsonDouble(const std::string& json, const std::string& field_name) {
    std::string search = "\"" + field_name + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0.0;
    
    size_t start = pos + search.length();
    // Skip whitespace
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t')) {
        start++;
    }
    // Find end (comma, closing brace, or newline)
    size_t end = start;
    while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != '\n') {
        end++;
    }
    if (end > start) {
        std::string value_str = json.substr(start, end - start);
        // Check if it's not "null" or "None"
        if (value_str != "null" && value_str != "None" && value_str.find("null") == std::string::npos) {
            try {
                return std::stod(value_str);
            } catch (...) {
                // Ignore parse errors
            }
        }
    }
    return 0.0;
}

std::string extractThumbnailUrl(const std::string& json) {
    // Check if this is SoundCloud
    bool is_soundcloud = (json.find("\"extractor_key\":\"Soundcloud\"") != std::string::npos ||
                          json.find("soundcloud.com") != std::string::npos);
    
    if (is_soundcloud) {
        // For SoundCloud, extract from thumbnails array (prefer t67x67)
        size_t thumbnails_pos = json.find("\"thumbnails\":");
        if (thumbnails_pos != std::string::npos) {
            // Try to find "t67x67" thumbnail - search flexibly
            std::string search_id_exact = "\"id\":\"t67x67\"";
            size_t id_pos = json.find(search_id_exact, thumbnails_pos);
            // If not found, try with spaces
            if (id_pos == std::string::npos) {
                std::string search_id_spaced = "\"id\": \"t67x67\"";
                id_pos = json.find(search_id_spaced, thumbnails_pos);
            }
            // If still not found, try to find "t67x67" anywhere in thumbnails section
            if (id_pos == std::string::npos) {
                size_t t67x67_pos = json.find("t67x67", thumbnails_pos);
                if (t67x67_pos != std::string::npos) {
                    size_t search_start = (t67x67_pos > 50) ? t67x67_pos - 50 : thumbnails_pos;
                    size_t id_field_pos = json.rfind("\"id\":", t67x67_pos);
                    if (id_field_pos != std::string::npos && id_field_pos >= search_start) {
                        size_t value_start = json.find('"', id_field_pos + 5);
                        if (value_start != std::string::npos && value_start < t67x67_pos) {
                            size_t value_end = json.find('"', value_start + 1);
                            if (value_end != std::string::npos) {
                                std::string id_value = json.substr(value_start + 1, value_end - value_start - 1);
                                if (id_value == "t67x67") {
                                    id_pos = id_field_pos;
                                }
                            }
                        }
                    }
                }
            }
            
            if (id_pos != std::string::npos) {
                size_t url_pos = json.find("\"url\":", id_pos);
                if (url_pos != std::string::npos && url_pos < id_pos + 200) {
                    std::string url = extractJsonString(json.substr(url_pos), "url");
                    if (!url.empty()) {
                        return url;
                    }
                }
            }
            
            // Fallback: first thumbnail in array
            size_t url_pos = json.find("\"url\":", thumbnails_pos);
            if (url_pos != std::string::npos && url_pos < thumbnails_pos + 500) {
                std::string url = extractJsonString(json.substr(url_pos), "url");
                if (!url.empty()) {
                    return url;
                }
            }
        }
        
        // For SoundCloud, also try "thumbnail" field if not found in thumbnails array
        std::string thumbnail = extractJsonString(json, "thumbnail");
        if (!thumbnail.empty()) {
            return thumbnail;
        }
    } else {
        // For YouTube and other platforms, try "thumbnail" field first
        std::string thumbnail = extractJsonString(json, "thumbnail");
        if (!thumbnail.empty()) {
            // For YouTube, ensure we use a small thumbnail URL and convert webp to jpg for compatibility
            bool is_youtube = (thumbnail.find("ytimg.com") != std::string::npos);
            if (is_youtube) {
                // Extract video ID from various URL formats
                // Format 1: https://i.ytimg.com/vi/VIDEO_ID/...
                // Format 2: https://i.ytimg.com/vi_webp/VIDEO_ID/...
                std::string video_id;
                size_t vi_pos = thumbnail.find("/vi/");
                size_t vi_webp_pos = thumbnail.find("/vi_webp/");
                
                if (vi_webp_pos != std::string::npos) {
                    // Found vi_webp format - extract video ID
                    size_t video_id_start = vi_webp_pos + 9; // After "/vi_webp/"
                    size_t video_id_end = thumbnail.find("/", video_id_start);
                    if (video_id_end != std::string::npos) {
                        video_id = thumbnail.substr(video_id_start, video_id_end - video_id_start);
                    }
                } else if (vi_pos != std::string::npos) {
                    // Found vi format - extract video ID
                    size_t video_id_start = vi_pos + 4; // After "/vi/"
                    size_t video_id_end = thumbnail.find("/", video_id_start);
                    if (video_id_end != std::string::npos) {
                        video_id = thumbnail.substr(video_id_start, video_id_end - video_id_start);
                    }
                }
                
                // If we found a video ID, build standard JPEG thumbnail URL (more compatible than webp)
                if (!video_id.empty()) {
                    return "https://i.ytimg.com/vi/" + video_id + "/default.jpg";
                }
            }
            return thumbnail;
        }
        
        // If thumbnail not found and this is YouTube, try to extract from video ID
        bool is_youtube = (json.find("youtube.com") != std::string::npos || 
                          json.find("ytimg.com") != std::string::npos ||
                          json.find("\"extractor_key\":\"Youtube\"") != std::string::npos);
        
        if (is_youtube) {
            std::string video_id = extractJsonString(json, "id");
            if (!video_id.empty()) {
                // Build YouTube thumbnail URL: https://i.ytimg.com/vi/VIDEO_ID/default.jpg
                return "https://i.ytimg.com/vi/" + video_id + "/default.jpg";
            }
        }
    }
    
    return "";
}

} // namespace JsonUtils


