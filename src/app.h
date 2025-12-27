#pragma once

#include <SDL.h>
#include <imgui.h>
#include "common/types.h"
#include "downloader.h"  // Needed for Downloader::PlaylistInfo
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <map>
#include <set>
#include <deque>

// Forward declarations
class Downloader;
class HistoryManager;
class UIRenderer;
class DownloadManager;
class FileManager;
class MetadataManager;
class Settings;
class ServiceChecker;
class WindowManager;
class EventHandler;
struct YtDlpSettings;  // Defined in downloader.h

class App {
    friend class UIRenderer;  // Allow UIRenderer to access private members for rendering
    friend class DownloadManager;  // Allow DownloadManager to access private members
    friend class FileManager;  // Allow FileManager to access private members
    friend class MetadataManager;  // Allow MetadataManager to access private members
    
public:
    App();
    ~App();
    
    bool initialize();
    void run();
    void cleanup();

    // Exposed for UI helpers
    std::string sanitizeFilename(const std::string& name);
    
    // Accessors for managers
    Settings* getSettings() const { return settings_.get(); }
    ServiceChecker* getServiceChecker() const { return service_checker_.get(); }
    WindowManager* getWindowManager() const { return window_manager_.get(); }
    EventHandler* getEventHandler() const { return event_handler_.get(); }
    
private:
    // window_, renderer_, window_width_, window_height_ removed - using window_manager_-> methods directly
    
    // Adaptive FPS limiting
    static constexpr int MAX_FPS = 30;
    static constexpr int IDLE_FPS = 30;  // FPS when no activity
    static constexpr double MAX_FRAME_TIME_MS = 1000.0 / MAX_FPS;  // ~33.33 ms per frame
    static constexpr double IDLE_FRAME_TIME_MS = 1000.0 / IDLE_FPS;  // ~33.33 ms per frame
    std::chrono::steady_clock::time_point last_frame_time_;
    std::chrono::steady_clock::time_point last_activity_time_;  // Track last UI update/activity
    bool has_active_downloads_;  // Cache active downloads state
    
    // Download tasks
    std::vector<std::unique_ptr<DownloadTask>> tasks_;
    mutable std::mutex tasks_mutex_;  // mutable for const methods like isRetryInProgress
    
    // History URLs - to prevent duplicate downloads
    std::set<std::string> history_urls_;  // Set of URLs that have been downloaded
    
    // Retry state tracking - URLs currently being processed for retry
    std::set<std::string> retry_in_progress_;  // URLs being retried (show spinner, block button)
    
    // Downloaders (one per task)
    std::vector<std::unique_ptr<Downloader>> downloaders_;
    
    // Download queue management
    // Allow several parallel downloads; tune as needed
    static const int MAX_CONCURRENT_DOWNLOADS = 3;
    int active_downloads_;
    
    // UI state
    char url_input_[512];
    // Settings fields removed - using settings_->* directly:
    // - proxy_input_, use_proxy_ → settings_->proxy_input, settings_->use_proxy
    // - downloads_dir_ → settings_->downloads_dir
    // - selected_format_, selected_quality_ → settings_->selected_format, settings_->selected_quality
    // - disable_playlists_, save_playlists_to_separate_folder_ → settings_->disable_playlists, settings_->save_playlists_to_separate_folder
    // - show_settings_panel_ → settings_->show_settings_panel
    // - API keys (spotify_api_key_, youtube_api_key_, soundcloud_api_key_) → settings_->*_api_key
    // - All ytdlp_* fields → settings_->ytdlp_*
    
    // Drag and drop
    bool drag_drop_active_;
    std::string drag_drop_path_;
    
    void update();
    void render();
    
    void addDownloadTask(const std::string& url);
    void startDownload(DownloadTask* task);
    void startDownloadImpl(DownloadTask* task);  // Internal implementation, doesn't check download_manager_
    void retryMissingPlaylistItems(DownloadTask* task);  // Retry downloading missing playlist items
    void retryMissingFromHistory(const std::string& url);  // Retry downloading from history (re-add URL)
    bool isRetryInProgress(const std::string& url) const;  // Check if URL is being retried
    void clearRetryInProgress(const std::string& url);  // Clear retry state when done
    void cancelDownload(DownloadTask* task);
    void clearDownloadList();  // Clear all download tasks
    void removeTask(size_t index);  // Remove a specific task by index
    void updateDownloadProgress(DownloadTask* task, const std::string& output);
    void detectPlatform(const std::string& url, std::string& platform);
    
    // File operations
    void openDownloadsFolder();
    void openFileLocation(const std::string& file_path);
    void selectDownloadsFolder();
    void startFileDrag(const std::string& file_path);
    void checkServiceAvailability(bool force_check = false, bool is_startup = false);  // Check if download services are available (force_check = ignore cache and recheck, is_startup = use longer timeout for startup)
    
    // Metadata operations (delegated to MetadataManager)
    void loadMetadata(DownloadTask* task);
    void startMetadataWorker();
    void enqueueMetadataRefresh(DownloadTask* task);
    
    // Helper functions
    // formatFileSize and formatDuration moved to AudioUtils namespace
    std::string normalizeProxy(const std::string& proxy);  // Add http:// prefix if protocol not specified
    YtDlpSettings createYtDlpSettings() const;  // Create YtDlpSettings from current UI state
    
    // Helper function to process playlist items metadata (file_size, bitrate)
    void processPlaylistItemsMetadata(std::vector<PlaylistItem>& playlist_items);
    
    // Helper function to process playlist_item_file_paths and update playlist_items
    // Returns the number of newly marked downloaded items
    int processPlaylistItemFilePaths(DownloadTask* task);
    
    // Check if playlist files already exist on disk and match them to playlist items
    bool checkExistingPlaylistFiles(DownloadTask* task, const Downloader::PlaylistInfo& playlist_info);
    
    // Settings persistence
    std::string getConfigPath();
    void loadSettings();
    void saveSettings();
    
    // Sync methods removed - all settings fields migrated to Settings class
    
    // History persistence (delegated to HistoryManager)
    std::string getHistoryPath();  // Kept for backward compatibility, delegates to PlatformUtils
    void loadHistory();
    void saveHistory();
    void rewriteHistoryFromTasks();
    void addToHistory(DownloadTask* task);
    void createHistoryItemImmediately(DownloadTask* task, const std::string& platform);  // Create history item immediately with placeholder
    void persistHistoryItems();  // Write current history_items_ to history.json
    // History cache for rendering
    void reloadHistoryCacheFromFile();
    void rebuildHistoryViewTasks();
    
    // Accessors for history data (delegated to HistoryManager)
    const std::vector<HistoryItem>& getHistoryItems() const;  // Returns thread-local cached copy
    std::vector<std::unique_ptr<DownloadTask>> getHistoryViewTasks() const;  // Returns fresh copy (move semantics)
    bool isUrlDeleted(const std::string& url) const;
    void deleteUrlFromHistory(const std::string& url);
    
    // Manager instances
    std::unique_ptr<HistoryManager> history_manager_;
    std::unique_ptr<UIRenderer> ui_renderer_;
    std::unique_ptr<DownloadManager> download_manager_;
    std::unique_ptr<FileManager> file_manager_;
    std::unique_ptr<MetadataManager> metadata_manager_;
    std::unique_ptr<Settings> settings_;
    std::unique_ptr<ServiceChecker> service_checker_;
    std::unique_ptr<WindowManager> window_manager_;
    std::unique_ptr<EventHandler> event_handler_;
    
    // yt-dlp update
    void updateYtDlp();
    bool ytdlp_update_in_progress_ = false;
    std::string ytdlp_update_status_;
    std::string ytdlp_version_;  // Cached yt-dlp version from config file
    bool ytdlp_version_present_ = false; // Was version loaded from config
    
    // Application shutdown flag for threads
    std::atomic<bool> shutting_down_;
    
    // Helper functions
    bool isValidUrl(const std::string& url);  // Validate URL format
    bool isValidPath(const std::string& path);  // Validate file path
    bool isPathSafe(const std::string& path);  // Check if path is safe (no directory traversal)
    
    // Debug: Create test data
    void createDebugTestData();

    // Generic background threads (join on cleanup)
    void runBackground(std::function<void()> fn);
    void joinBackgroundThreads();
    std::vector<std::thread> background_threads_;
    std::mutex background_threads_mutex_;
};

