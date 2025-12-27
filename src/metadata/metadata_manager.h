#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <functional>

// Forward declaration
class App;
struct DownloadTask;

class MetadataManager {
public:
    MetadataManager(App* app_context);
    ~MetadataManager();
    
    // Metadata operations
    void loadMetadata(DownloadTask* task);
    void startMetadataWorker();
    void enqueueMetadataRefresh(DownloadTask* task);
    void stopMetadataWorker();
    
private:
    App* app_context_;  // Pointer to App for accessing data
    
    void metadataWorkerLoop();
    
    std::thread metadata_worker_;
    std::mutex metadata_mutex_;
    std::condition_variable metadata_cv_;
    std::deque<DownloadTask*> metadata_queue_;
    std::atomic<bool> metadata_stop_;
    bool metadata_worker_started_;
};


