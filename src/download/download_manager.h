#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../common/types.h"

// Forward declaration
class App;
class Downloader;

class DownloadManager {
public:
    DownloadManager(App* app_context);
    ~DownloadManager();
    
    // Download task management
    void addDownloadTask(const std::string& url);
    void startDownload(DownloadTask* task);
    void cancelDownload(DownloadTask* task);
    void retryMissingPlaylistItems(DownloadTask* task);
    void clearDownloadList();
    void removeTask(size_t index);
    
    // Download progress
    void updateDownloadProgress(DownloadTask* task, const std::string& output);
    
                // Platform detection
                void detectPlatform(const std::string& url, std::string& platform);
                
            private:
    App* app_context_;  // Pointer to App for accessing data
    
    // Helper functions
    std::string normalizeProxy(const std::string& proxy);
    std::string sanitizeFilename(const std::string& filename);
};


