#include "metadata_manager.h"
#include "../app.h"
#include "../settings/settings.h"
#include "../downloader.h"
#include "../common/types.h"
#include "../common/audio_utils.h"
#include <sys/stat.h>
#include <iostream>

MetadataManager::MetadataManager(App* app_context)
    : app_context_(app_context)
    , metadata_stop_(false)
    , metadata_worker_started_(false) {
}

MetadataManager::~MetadataManager() {
    stopMetadataWorker();
}

void MetadataManager::loadMetadata(DownloadTask* task) {
    if (!task || task->file_path.empty() || task->metadata_loaded) {
        return;
    }
    
    // Metadata loading not available (TagLib removed)
    task->metadata_loaded = false;
}

void MetadataManager::startMetadataWorker() {
    if (metadata_worker_started_) return;
    metadata_stop_ = false;
    metadata_worker_started_ = true;
    // Create thread directly instead of using runBackground() to have control over it
    metadata_worker_ = std::thread([this]() {
        try {
            metadataWorkerLoop();
        } catch (...) {
            // Swallow exceptions to avoid std::terminate from background thread
            std::cout << "[DEBUG] MetadataManager: Exception caught in worker thread" << std::endl;
        }
    });
}

void MetadataManager::stopMetadataWorker() {
    if (!metadata_worker_started_) return;
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        metadata_stop_ = true;
    }
    metadata_cv_.notify_one();
    if (metadata_worker_.joinable()) {
        // Check if app is shutting down - if so, detach instead of join to avoid blocking
        if (app_context_ && app_context_->shutting_down_) {
            std::cout << "[DEBUG] MetadataManager: Detaching worker thread during shutdown" << std::endl;
            metadata_worker_.detach();
        } else {
            metadata_worker_.join();
        }
    }
    metadata_worker_started_ = false;
}

void MetadataManager::enqueueMetadataRefresh(DownloadTask* task) {
    if (!task) return;
    startMetadataWorker();
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        metadata_queue_.push_back(task);
    }
    metadata_cv_.notify_one();
}

void MetadataManager::metadataWorkerLoop() {
    while (true) {
        DownloadTask* task = nullptr;
        {
            std::unique_lock<std::mutex> lock(metadata_mutex_);
            // Wait for work or stop signal
            // Use only metadata_stop_ to avoid accessing app_context_ which may be destroyed
            metadata_cv_.wait(lock, [this]() {
                return metadata_stop_ || !metadata_queue_.empty();
            });
            if (metadata_stop_ && metadata_queue_.empty()) {
                break;
            }
            if (!metadata_queue_.empty()) {
                task = metadata_queue_.front();
                metadata_queue_.pop_front();
            }
        }
        if (metadata_stop_) break;
        if (!task) continue;

        // Skip playlists - they get metadata from getPlaylistItems and JSON during download
        // CRITICAL: Check app_context_ before accessing it (it may be destroyed during shutdown)
        if (!app_context_) {
            std::cout << "[DEBUG] MetadataManager: app_context_ is null, stopping worker" << std::endl;
            break;
        }
        
        bool is_playlist = false;
        {
            std::lock_guard<std::mutex> lock(app_context_->tasks_mutex_);
            is_playlist = task->is_playlist && task->total_playlist_items > 1;
        }
        if (is_playlist) {
            std::cout << "[DEBUG] MetadataManager: Skipping playlist (metadata already loaded)" << std::endl;
            continue;
        }

        std::string proxy_for_info;
        {
            // CRITICAL: Check app_context_ again before accessing (may be destroyed)
            if (!app_context_) break;
            std::lock_guard<std::mutex> lock(app_context_->tasks_mutex_);
            if (app_context_->getSettings()->use_proxy && !app_context_->getSettings()->proxy_input.empty()) {
                std::string proxy = app_context_->getSettings()->proxy_input;
                // Basic normalization: add http:// if no protocol specified
                if (proxy.find("://") == std::string::npos) {
                    proxy = "http://" + proxy;
                }
                proxy_for_info = proxy;
            }
        }

        YtDlpSettings ytdlp_settings = app_context_->getSettings()->createYtDlpSettings();
        Downloader::VideoInfo info = Downloader::getVideoInfo(
            task->url, 
            app_context_->getSettings()->downloads_dir, 
            app_context_->getSettings()->selected_format, 
            proxy_for_info,
            ytdlp_settings
        );

        {
            std::lock_guard<std::mutex> lock(app_context_->tasks_mutex_);
            if (task->status != "completed") {
                continue;
            }
            if (!info.title.empty() && task->metadata.title.empty()) {
                task->metadata.title = info.title;
            }
            if (!info.artist.empty() && task->metadata.artist.empty()) {
                task->metadata.artist = info.artist;
            }
            if (task->metadata.duration == 0 && !info.duration.empty()) {
                try { 
                    task->metadata.duration = std::stoi(info.duration); 
                } catch (...) {}
            }
            // REMOVED: bitrate from getVideoInfo - bitrate will be calculated from file_size and duration after conversion
            // This ensures we get the actual bitrate of the converted file, not the source stream bitrate
            if (task->file_size == 0 && !info.filepath.empty()) {
                struct stat file_stat;
                if (stat(info.filepath.c_str(), &file_stat) == 0) {
                    task->file_size = file_stat.st_size;
                }
            }
            if (task->metadata.bitrate == 0 && task->metadata.duration > 0 && task->file_size > 0) {
                task->metadata.bitrate = AudioUtils::calculateBitrate(task->file_size, task->metadata.duration);
            }
            // Save thumbnail URL if not already set
            if (task->thumbnail_url.empty() && !info.thumbnail_url.empty()) {
                task->thumbnail_url = info.thumbnail_url;
                std::cout << "[DEBUG] MetadataManager: Saved thumbnail_url=" << task->thumbnail_url << std::endl;
            }
        }
        // Persist updated metadata asynchronously to avoid blocking UI
        app_context_->runBackground([this]() {
            app_context_->rewriteHistoryFromTasks();
        });
    }
}

