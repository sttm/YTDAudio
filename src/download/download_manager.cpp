#include "download_manager.h"
#include "../app.h"
#include "../downloader.h"
#include "../history/history_manager.h"
#include "../common/validation_utils.h"
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <sstream>

DownloadManager::DownloadManager(App* app_context) : app_context_(app_context) {
}

DownloadManager::~DownloadManager() {
}

void DownloadManager::addDownloadTask(const std::string& url) {
    if (!app_context_) return;
    
    // Simply delegate to App::addDownloadTask
    // All duplicate checking is done there atomically with the lock
    // Temporarily disable download_manager_ to break the recursion cycle
    std::unique_ptr<DownloadManager> temp_manager = std::move(app_context_->download_manager_);
    app_context_->addDownloadTask(url);
    app_context_->download_manager_ = std::move(temp_manager);
}

void DownloadManager::startDownload(DownloadTask* task) {
    if (!app_context_) return;
    
    // Check if this URL was already downloaded before starting download
    // This is an additional check beyond addDownloadTask to ensure we don't download duplicates
    {
        std::lock_guard<std::mutex> lock(app_context_->tasks_mutex_);
        
        // Check history_urls_ for both original URL and processed URL
        std::string check_url = task->url;
        if (app_context_->history_urls_.find(check_url) != app_context_->history_urls_.end()) {
            std::cout << "[DEBUG] DownloadManager: URL already in history, skipping download: " << check_url << std::endl;
            task->status = "already_exists";
            task->error_message = "This URL has already been downloaded";
            return;
        }
        
        // Also check if there's a completed task with the same URL
        for (const auto& existing_task : app_context_->tasks_) {
            if (existing_task.get() != task && 
                existing_task->url == check_url && 
                (existing_task->status == "completed" || existing_task->status == "already_exists")) {
                std::cout << "[DEBUG] DownloadManager: Task with same URL already completed, skipping download: " << check_url << std::endl;
                task->status = "already_exists";
                task->error_message = "This URL has already been downloaded";
                return;
            }
        }
        
        // For playlists, also check if playlist name matches (if available)
        if (task->is_playlist && !task->playlist_name.empty()) {
            // Check history items for playlist with same name
            if (app_context_->history_manager_) {
                const auto& history_items = app_context_->history_manager_->getHistoryItems();
                for (const auto& item : history_items) {
                    if (item.is_playlist && 
                        item.playlist_name == task->playlist_name && 
                        item.url == check_url) {
                        std::cout << "[DEBUG] DownloadManager: Playlist with same name and URL already in history, skipping: " 
                                  << task->playlist_name << " (" << check_url << ")" << std::endl;
                        task->status = "already_exists";
                        task->error_message = "This playlist has already been downloaded";
                        return;
                    }
                }
            }
        }
        
        // Note: File existence check removed from here to avoid blocking GUI
        // yt-dlp will handle file existence check automatically and skip if file already exists
        // This prevents GUI blocking from synchronous getExpectedFilename() call
    }
    
    // CRITICAL: Run startDownloadImpl in background thread to avoid blocking UI
    // The blocking wait for playlist name inside startDownloadImpl will not freeze the interface
    // Use startDownloadImpl to avoid recursion back to DownloadManager
    app_context_->runBackground([app = app_context_, task]() {
        app->startDownloadImpl(task);
    });
}

void DownloadManager::cancelDownload(DownloadTask* task) {
    if (!app_context_) return;
    
    // Temporarily disable download_manager_ to break the recursion cycle
    std::unique_ptr<DownloadManager> temp_manager = std::move(app_context_->download_manager_);
    app_context_->cancelDownload(task);
    app_context_->download_manager_ = std::move(temp_manager);
}

void DownloadManager::retryMissingPlaylistItems(DownloadTask* task) {
    if (!app_context_) return;
    
    // Temporarily disable download_manager_ to break the recursion cycle
    std::unique_ptr<DownloadManager> temp_manager = std::move(app_context_->download_manager_);
    app_context_->retryMissingPlaylistItems(task);
    app_context_->download_manager_ = std::move(temp_manager);
}

void DownloadManager::clearDownloadList() {
    if (!app_context_) return;
    
    // Temporarily disable download_manager_ to break the recursion cycle
    std::unique_ptr<DownloadManager> temp_manager = std::move(app_context_->download_manager_);
    app_context_->clearDownloadList();
    app_context_->download_manager_ = std::move(temp_manager);
}

void DownloadManager::removeTask(size_t index) {
    if (!app_context_) return;
    
    // Temporarily disable download_manager_ to break the recursion cycle
    std::unique_ptr<DownloadManager> temp_manager = std::move(app_context_->download_manager_);
    app_context_->removeTask(index);
    app_context_->download_manager_ = std::move(temp_manager);
}

void DownloadManager::updateDownloadProgress(DownloadTask* task, const std::string& output) {
    if (!app_context_) return;
    
    // Temporarily disable download_manager_ to break the recursion cycle
    std::unique_ptr<DownloadManager> temp_manager = std::move(app_context_->download_manager_);
    app_context_->updateDownloadProgress(task, output);
    app_context_->download_manager_ = std::move(temp_manager);
}

void DownloadManager::detectPlatform(const std::string& url, std::string& platform) {
    if (!app_context_) return;
    
    // This method doesn't call download_manager_, so no need to break cycle
    app_context_->detectPlatform(url, platform);
}

std::string DownloadManager::normalizeProxy(const std::string& proxy) {
    // Use ValidationUtils directly instead of delegating through App
    return ValidationUtils::normalizeProxy(proxy);
}

std::string DownloadManager::sanitizeFilename(const std::string& filename) {
    // Use ValidationUtils directly instead of delegating through App
    return ValidationUtils::sanitizeFilename(filename);
}
