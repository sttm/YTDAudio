#pragma once

#include "../common/types.h"
#include "../common/history_utils.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <mutex>

using json = nlohmann::json;

// JSON serialization helpers (must be inline for ADL to work across translation units)
inline void to_json(json& j, const PlaylistItem& p) {
    j = json{
        {"title", p.title},
        {"url", p.url},
        {"id", p.id},
        {"index", p.index},
        {"downloaded", p.downloaded},
        {"file_path", p.file_path},
        {"filename", p.filename},
        {"duration", p.duration},
        {"bitrate", p.bitrate},
        {"file_size", p.file_size}
    };
}

inline void from_json(const json& j, PlaylistItem& p) {
    if (j.contains("title")) j.at("title").get_to(p.title);
    if (j.contains("url")) j.at("url").get_to(p.url);
    if (j.contains("id")) j.at("id").get_to(p.id);
    if (j.contains("index")) j.at("index").get_to(p.index);
    if (j.contains("downloaded")) j.at("downloaded").get_to(p.downloaded);
    if (j.contains("file_path")) j.at("file_path").get_to(p.file_path);
    if (j.contains("filename")) j.at("filename").get_to(p.filename);
    if (j.contains("duration")) j.at("duration").get_to(p.duration);
    if (j.contains("bitrate")) j.at("bitrate").get_to(p.bitrate);
    if (j.contains("file_size")) j.at("file_size").get_to(p.file_size);
}

inline void to_json(json& j, const HistoryItem& h) {
    j = json{
        {"id", h.id},
        {"url", h.url},
        {"status", h.status},
        {"timestamp", h.timestamp},
        {"filename", h.filename},
        {"filepath", h.filepath},
        {"title", h.title},
        {"artist", h.artist},
        {"platform", h.platform},
        {"is_playlist", h.is_playlist},
        {"playlist_name", h.playlist_name},
        {"total_playlist_items", h.total_playlist_items},
        {"duration", h.duration},
        {"bitrate", h.bitrate},
        {"file_size", h.file_size},
        {"thumbnail_base64", h.thumbnail_base64},
        {"playlist_items", h.playlist_items}
    };
}

inline void from_json(const json& j, HistoryItem& h) {
    // ID is optional for backward compatibility - generate if missing
    if (j.contains("id")) {
        j.at("id").get_to(h.id);
    } else {
        // Generate ID from timestamp + URL hash for old items (backward compatibility)
        h.id = HistoryUtils::generateHistoryIdFromTimestamp(j.value("timestamp", 0), j.value("url", ""));
    }
    j.at("url").get_to(h.url);
    if (j.contains("status")) j.at("status").get_to(h.status);
    if (j.contains("timestamp")) j.at("timestamp").get_to(h.timestamp);
    if (j.contains("filename")) j.at("filename").get_to(h.filename);
    if (j.contains("filepath")) j.at("filepath").get_to(h.filepath);
    if (j.contains("title")) j.at("title").get_to(h.title);
    if (j.contains("artist")) j.at("artist").get_to(h.artist);
    if (j.contains("platform")) j.at("platform").get_to(h.platform);
    if (j.contains("is_playlist")) j.at("is_playlist").get_to(h.is_playlist);
    if (j.contains("playlist_name")) j.at("playlist_name").get_to(h.playlist_name);
    if (j.contains("total_playlist_items")) j.at("total_playlist_items").get_to(h.total_playlist_items);
    if (j.contains("duration")) j.at("duration").get_to(h.duration);
    if (j.contains("bitrate")) j.at("bitrate").get_to(h.bitrate);
    if (j.contains("file_size")) j.at("file_size").get_to(h.file_size);
    if (j.contains("thumbnail_base64")) j.at("thumbnail_base64").get_to(h.thumbnail_base64);
    if (j.contains("playlist_items")) h.playlist_items = j.at("playlist_items").get<std::vector<PlaylistItem>>();
}

class HistoryManager {
public:
    HistoryManager();
    ~HistoryManager();
    
    // History persistence
    void loadHistory();
    void saveHistory();
    // Note: rewriteHistoryFromTasks and addToHistory are implemented in App class, not here
    void addHistoryItem(const HistoryItem& item);  // Add a single history item
    void persistHistoryItems();  // Write current history_items_ to history.json
    void waitForSaveCompletion();  // Wait for any ongoing save to complete (for shutdown, no timeout)
    
    // History cache for rendering
    void reloadHistoryCacheFromFile();
    void rebuildHistoryViewTasks();
    
    // Getters
    std::vector<HistoryItem> getHistoryItems() const {
        std::lock_guard<std::mutex> lock(history_mutex_);
        return history_items_;
    }
    std::vector<std::unique_ptr<DownloadTask>> getHistoryViewTasks() const {
        std::lock_guard<std::mutex> lock(history_mutex_);
        // Return a copy since we can't return reference to unique_ptr vector safely
        std::vector<std::unique_ptr<DownloadTask>> result;
        result.reserve(history_view_tasks_.size());
        for (const auto& task : history_view_tasks_) {
            // Create a copy of DownloadTask
            auto copy = std::make_unique<DownloadTask>(task->url);
            *copy = *task;
            result.push_back(std::move(copy));
        }
        return result;
    }
    bool isUrlDeleted(const std::string& url) const { return deleted_urls_.find(url) != deleted_urls_.end(); }
    
    // History management
    void deleteUrl(const std::string& url);
    void deleteItemByIndex(size_t index);
    void deleteItemById(const std::string& id);  // Delete by unique ID (more reliable than index)
    // Note: clearDeletedUrls() removed - not used anywhere
    void removeDeletedUrl(const std::string& url);  // Remove URL from deleted_urls_ (e.g., when successfully re-downloaded)
    void clearAll();  // Clear all history items and view tasks
    size_t getHistoryItemsCount() const {
        std::lock_guard<std::mutex> lock(history_mutex_);
        return history_items_.size();
    }
    size_t getHistoryViewTasksCount() const {
        std::lock_guard<std::mutex> lock(history_mutex_);
        return history_view_tasks_.size();
    }
    
private:
    mutable std::mutex history_mutex_;  // Protect history_items_ from concurrent access (mutable for const methods)
    mutable std::timed_mutex persist_mutex_;  // Protect persistHistoryItems() from concurrent writes (mutable for const methods)
    std::vector<HistoryItem> history_items_;
    std::vector<std::unique_ptr<DownloadTask>> history_view_tasks_;
    std::set<std::string> deleted_urls_;  // Track URLs explicitly deleted by user
    
    std::string getHistoryPath();
    std::string escapeJsonString(const std::string& str);
    std::string unescapeJsonString(const std::string& str);
};

