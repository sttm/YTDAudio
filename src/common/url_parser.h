#ifndef URL_PARSER_H
#define URL_PARSER_H

#include <string>
#include <map>

class UrlParser {
public:
    struct ParsedUrl {
        std::string full_url;
        std::string scheme;
        std::string domain;
        std::string path;
        std::string query;
        std::map<std::string, std::string> query_params;
        
        // Helper methods
        std::string getPathLower() const;
        bool hasSetsInPath() const;
    };
    
    // Parse full URL into components
    static ParsedUrl parse(const std::string& url);
    
    // Quick accessors
    static std::string getPath(const std::string& url);
    static std::string getQuery(const std::string& url);
    static std::map<std::string, std::string> getQueryParams(const std::string& url);
    
    // SoundCloud-specific helpers
    static bool hasSetsInPath(const std::string& url);
    static bool hasSetsInQuery(const std::string& url);
    
private:
    static void parseQueryString(const std::string& query, std::map<std::string, std::string>& params);
    static std::string toLower(const std::string& str);
};

#endif // URL_PARSER_H

