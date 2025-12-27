#include "history_manager.h"
#include "../platform/platform_utils.h"
#include "../common/path_utils.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <algorithm>
#include <chrono>

HistoryManager::HistoryManager() {
}

HistoryManager::~HistoryManager() {
}

std::string HistoryManager::getHistoryPath() {
    return PlatformUtils::getHistoryPath();
}

std::string HistoryManager::escapeJsonString(const std::string& str) {
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

std::string HistoryManager::unescapeJsonString(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            if (str[i + 1] == '"') { result += '"'; i++; }
            else if (str[i + 1] == '\\') { result += '\\'; i++; }
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

void HistoryManager::loadHistory() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    std::string history_path = getHistoryPath();
    std::ifstream file(history_path);
    if (!file.is_open()) {
        // Don't create file if it doesn't exist - user may have deleted it intentionally
        // File will be created automatically when first item is added to history
        std::cout << "[DEBUG] History file not found at " << history_path << ", starting with empty history" << std::endl;
        return;
    }
    json doc;
    try {
        file >> doc;
    } catch (...) {
        return;
    }
    if (!doc.contains("items") || !doc["items"].is_array()) {
        return;
    }
    std::vector<HistoryItem> items;
    try {
        items = doc["items"].get<std::vector<HistoryItem>>();
    } catch (...) {
        return;
    }
    
    // Normalize all file paths in loaded items to fix mixed separators
    bool paths_normalized = false;
    for (auto& item : items) {
        if (!item.filepath.empty()) {
            std::string normalized = PathUtils::normalizePath(item.filepath);
            if (normalized != item.filepath) {
                item.filepath = normalized;
                paths_normalized = true;
            }
        }
        // Normalize paths in playlist items
        for (auto& playlist_item : item.playlist_items) {
            if (!playlist_item.file_path.empty()) {
                std::string normalized = PathUtils::normalizePath(playlist_item.file_path);
                if (normalized != playlist_item.file_path) {
                    playlist_item.file_path = normalized;
                    paths_normalized = true;
                }
            }
        }
    }
    
    history_items_ = items;
    
    // If paths were normalized, save the corrected history back to file
    if (paths_normalized) {
        std::cout << "[DEBUG] HistoryManager::loadHistory: Normalized file paths, saving corrected history" << std::endl;
        persistHistoryItems();
    }
    
    rebuildHistoryViewTasks();
    
    // Debug: check thumbnails in loaded items
    size_t items_with_thumbnails = 0;
    for (const auto& item : items) {
        if (!item.thumbnail_base64.empty()) {
            items_with_thumbnails++;
            std::cout << "[DEBUG] HistoryManager::loadHistory: Item URL=" << item.url << " has thumbnail_base64 (size: " << item.thumbnail_base64.length() << " bytes)" << std::endl;
        }
    }
    std::cout << "[DEBUG] HistoryManager::loadHistory: Loaded " << items.size() << " history items from " << history_path << " (" << items_with_thumbnails << " with thumbnails)" << std::endl;
}

void HistoryManager::rebuildHistoryViewTasks() {
    // NOTE: This function should be called only when history_mutex_ is already locked
    // Helper function to decode Unicode escape sequences in strings
    auto decodeUnicodeEscapes = [this](const std::string& str) -> std::string {
        return unescapeJsonString(str);
    };
    
    history_view_tasks_.clear();
    history_view_tasks_.reserve(history_items_.size());
    for (const auto& h : history_items_) {
        if (h.url.empty()) continue;
        auto task = std::make_unique<DownloadTask>(h.url);
        task->status = h.status.empty() ? "completed" : h.status;
        task->platform = h.platform;
        task->filename = decodeUnicodeEscapes(h.filename);
        // Normalize file path when loading from history
        task->file_path = PathUtils::normalizePath(h.filepath);
        task->metadata.title = decodeUnicodeEscapes(h.title);
        task->metadata.artist = decodeUnicodeEscapes(h.artist);
        task->metadata.duration = h.duration;
        task->metadata.bitrate = h.bitrate;
        task->file_size = h.file_size;
        task->metadata_loaded = true;
        task->is_playlist = h.is_playlist && h.total_playlist_items > 1;
        task->playlist_name = decodeUnicodeEscapes(h.playlist_name);
        task->total_playlist_items = h.total_playlist_items;
        if (task->is_playlist) {
            task->playlist_items = h.playlist_items;
            // Decode Unicode escapes in playlist items titles and normalize file paths
            for (auto& item : task->playlist_items) {
                item.title = decodeUnicodeEscapes(item.title);
                // Normalize file path when loading from history
                if (!item.file_path.empty()) {
                    item.file_path = PathUtils::normalizePath(item.file_path);
                }
            }
            if (task->total_playlist_items == 0 && !task->playlist_items.empty()) {
                task->total_playlist_items = static_cast<int>(task->playlist_items.size());
            }
        }
        if (task->is_playlist && task->total_playlist_items <= 1) {
            task->is_playlist = false;
            task->total_playlist_items = 0;
        }
        if (task->file_size == 0 && !task->file_path.empty()) {
            struct stat st;
            if (stat(task->file_path.c_str(), &st) == 0) {
                task->file_size = st.st_size;
            }
        }
        // Store thumbnail_base64 in a way that can be accessed from UI
        // We'll need to store it in DownloadTask - but DownloadTask doesn't have this field yet
        // For now, we'll store it in a map or add it to DownloadTask structure
        // Since we can't modify DownloadTask easily, we'll access it from history_items_ directly in UI
        history_view_tasks_.push_back(std::move(task));
    }
}

void HistoryManager::persistHistoryItems() {
    // Protect against concurrent writes to prevent data loss
    std::lock_guard<std::timed_mutex> persist_lock(persist_mutex_);
    
    // Also lock history_items_ to ensure we read consistent state
    std::lock_guard<std::mutex> history_lock(history_mutex_);
    
    std::string history_path = getHistoryPath();
    
    // Check if file exists
    struct stat st;
    bool file_exists = (stat(history_path.c_str(), &st) == 0);
    
    // If file was deleted by user and history is empty, don't recreate it
    if (!file_exists && history_items_.empty()) {
        std::cout << "[DEBUG] History file was deleted and history is empty, not recreating file" << std::endl;
        return;
    }
    
    // Ensure directory exists before creating file
    size_t last_slash = history_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        std::string dir_path = history_path.substr(0, last_slash);
        if (!PlatformUtils::createDirectory(dir_path)) {
            std::cout << "[DEBUG] HistoryManager::persistHistoryItems: Failed to create directory: " << dir_path << std::endl;
        } else {
            std::cout << "[DEBUG] HistoryManager::persistHistoryItems: Directory ensured: " << dir_path << std::endl;
        }
    }
    
    json doc;
    doc["items"] = history_items_;
    std::ofstream file(history_path);
    if (file.is_open()) {
        file << doc.dump(2, ' ', false);
        file.close(); // Explicitly close to ensure data is flushed
        std::cout << "[DEBUG] History persisted to " << history_path << " (" << history_items_.size() << " items)" << std::endl;
        
        // Verify file was written correctly
        struct stat verify_st;
        if (stat(history_path.c_str(), &verify_st) == 0) {
            std::cout << "[DEBUG] HistoryManager::persistHistoryItems: File verified, size: " << verify_st.st_size << " bytes" << std::endl;
        } else {
            std::cout << "[DEBUG] HistoryManager::persistHistoryItems: WARNING - File verification failed after write" << std::endl;
        }
    } else {
        std::cout << "[DEBUG][rewriteHistory] cannot open " << history_path << " for writing" << std::endl;
    }
}

void HistoryManager::saveHistory() {
    persistHistoryItems();
}

void HistoryManager::waitForSaveCompletion() {
    // OPTIMIZATION: Try to acquire persist_mutex_ with timeout to avoid blocking GUI
    // Wait up to 2 seconds for save to complete
    std::unique_lock<std::timed_mutex> lock(persist_mutex_, std::defer_lock);
    if (lock.try_lock_for(std::chrono::seconds(2))) {
        // Lock acquired means save is complete (or was not in progress)
        std::cout << "[DEBUG] HistoryManager: Save completion confirmed, proceeding with shutdown" << std::endl;
    } else {
        // Timeout - save is taking too long, proceed with shutdown anyway
        // The save will complete in background thread, but we won't wait for it
        std::cout << "[DEBUG] HistoryManager: Save timeout (2s), proceeding with shutdown (save will complete in background)" << std::endl;
    }
    // Lock will be released when function returns
}

void HistoryManager::reloadHistoryCacheFromFile() {
    loadHistory();
}

void HistoryManager::deleteUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    deleted_urls_.insert(url);
    // Remove from history_items_
    history_items_.erase(
        std::remove_if(history_items_.begin(), history_items_.end(),
            [&url](const HistoryItem& item) { return item.url == url; }),
        history_items_.end()
    );
    // Rebuild view tasks after deletion
    rebuildHistoryViewTasks();
}

void HistoryManager::deleteItemByIndex(size_t index) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    if (index < history_items_.size()) {
        std::string deleted_url = history_items_[index].url;
        deleted_urls_.insert(deleted_url);
        history_items_.erase(history_items_.begin() + index);
        // Rebuild view tasks after deletion
        rebuildHistoryViewTasks();
    }
}

void HistoryManager::deleteItemById(const std::string& id) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    auto it = std::find_if(history_items_.begin(), history_items_.end(),
        [&id](const HistoryItem& item) { return item.id == id; });
    if (it != history_items_.end()) {
        std::string deleted_url = it->url;
        deleted_urls_.insert(deleted_url);
        history_items_.erase(it);
        // Rebuild view tasks after deletion
        rebuildHistoryViewTasks();
    }
}

// clearDeletedUrls() removed - not used anywhere in the codebase

void HistoryManager::removeDeletedUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    deleted_urls_.erase(url);
}

void HistoryManager::clearAll() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_items_.clear();
    history_view_tasks_.clear();
    deleted_urls_.clear();
}

// Note: rewriteHistoryFromTasks and addToHistory are implemented in App class
// These methods in HistoryManager are not used (dead code removed)

void HistoryManager::addHistoryItem(const HistoryItem& item) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    // Check if URL already exists
    auto it = std::find_if(history_items_.begin(), history_items_.end(),
        [&item](const HistoryItem& h) { return h.url == item.url; });
    if (it != history_items_.end()) {
        // Update existing item, but preserve important fields
        HistoryItem updated_item = item;
        // CRITICAL: Always preserve existing ID to maintain identity
        if (!it->id.empty()) {
            updated_item.id = it->id;
        }
        // Preserve thumbnail_base64 if new item doesn't have it
        if (updated_item.thumbnail_base64.empty() && !it->thumbnail_base64.empty()) {
            updated_item.thumbnail_base64 = it->thumbnail_base64;
            std::cout << "[DEBUG] HistoryManager::addHistoryItem: Preserved existing thumbnail_base64 for URL=" << item.url << std::endl;
        }
        // Preserve other fields if they're empty in new item but exist in old item
        if (updated_item.playlist_name.empty() && !it->playlist_name.empty()) {
            updated_item.playlist_name = it->playlist_name;
        }
        if (updated_item.title.empty() && !it->title.empty()) {
            updated_item.title = it->title;
        }
        if (updated_item.artist.empty() && !it->artist.empty()) {
            updated_item.artist = it->artist;
        }
        if (updated_item.total_playlist_items == 0 && it->total_playlist_items > 0) {
            updated_item.total_playlist_items = it->total_playlist_items;
        }
        if (updated_item.playlist_items.empty() && !it->playlist_items.empty()) {
            updated_item.playlist_items = it->playlist_items;
        }
        *it = updated_item;
        std::cout << "[DEBUG] HistoryManager::addHistoryItem: Updated existing item for URL=" << item.url << ", status=" << item.status << std::endl;
    } else {
        // Add new item
        history_items_.push_back(item);
        std::cout << "[DEBUG] HistoryManager::addHistoryItem: Added new item for URL=" << item.url << ", status=" << item.status << std::endl;
    }
    // Rebuild view tasks after adding
    rebuildHistoryViewTasks();
}

