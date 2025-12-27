#include "url_parser.h"
#include <algorithm>
#include <sstream>
#include <cctype>

std::string UrlParser::ParsedUrl::getPathLower() const {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower;
}

bool UrlParser::ParsedUrl::hasSetsInPath() const {
    std::string lower_path = getPathLower();
    return lower_path.find("sets/") != std::string::npos;
}

UrlParser::ParsedUrl UrlParser::parse(const std::string& url) {
    ParsedUrl result;
    result.full_url = url;
    
    // Find scheme (http:// or https://)
    size_t scheme_end = url.find("://");
    if (scheme_end != std::string::npos) {
        result.scheme = url.substr(0, scheme_end);
        scheme_end += 3; // Skip "://"
    } else {
        scheme_end = 0;
    }
    
    // Find domain and path
    size_t path_start = url.find('/', scheme_end);
    size_t query_start = url.find('?', scheme_end);
    
    if (path_start != std::string::npos) {
        result.domain = url.substr(scheme_end, path_start - scheme_end);
        
        if (query_start != std::string::npos) {
            result.path = url.substr(path_start, query_start - path_start);
            result.query = url.substr(query_start + 1);
        } else {
            result.path = url.substr(path_start);
        }
    } else {
        if (query_start != std::string::npos) {
            result.domain = url.substr(scheme_end, query_start - scheme_end);
            result.query = url.substr(query_start + 1);
        } else {
            result.domain = url.substr(scheme_end);
        }
    }
    
    // Parse query parameters
    if (!result.query.empty()) {
        parseQueryString(result.query, result.query_params);
    }
    
    return result;
}

std::string UrlParser::getPath(const std::string& url) {
    ParsedUrl parsed = parse(url);
    return parsed.path;
}

std::string UrlParser::getQuery(const std::string& url) {
    ParsedUrl parsed = parse(url);
    return parsed.query;
}

std::map<std::string, std::string> UrlParser::getQueryParams(const std::string& url) {
    ParsedUrl parsed = parse(url);
    return parsed.query_params;
}

bool UrlParser::hasSetsInPath(const std::string& url) {
    ParsedUrl parsed = parse(url);
    return parsed.hasSetsInPath();
}

bool UrlParser::hasSetsInQuery(const std::string& url) {
    ParsedUrl parsed = parse(url);
    std::string query_lower = toLower(parsed.query);
    return query_lower.find("sets/") != std::string::npos;
}

void UrlParser::parseQueryString(const std::string& query, std::map<std::string, std::string>& params) {
    std::istringstream stream(query);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t equals_pos = pair.find('=');
        if (equals_pos != std::string::npos) {
            std::string key = pair.substr(0, equals_pos);
            std::string value = pair.substr(equals_pos + 1);
            // URL decode if needed (simplified - assumes no encoding for now)
            params[key] = value;
        } else {
            params[pair] = "";
        }
    }
}

std::string UrlParser::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

