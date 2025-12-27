#include "app.h"
#include "downloader.h"
#include "platform/platform_utils.h"
#include "platform/platform_detector.h"
#include "platform/path_finder.h"
#include "history/history_manager.h"
#include "ui/ui_renderer.h"
#include "download/download_manager.h"
#include "file/file_manager.h"
#include "metadata/metadata_manager.h"
#include "settings/settings.h"
#include "service/service_checker.h"
#include "window/window_manager.h"
#include "events/event_handler.h"
#include "common/validation_utils.h"
#include "common/json_utils.h"
#include "common/browser_utils.h"
#include "common/audio_utils.h"
#include "common/windows_utils.h"
#include "common/url_parser.h"
#include "common/playlist_detector.h"
#include "common/thumbnail_downloader.h"
#include "common/path_utils.h"
#include "common/history_utils.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <functional>
#include <regex>
#include <cstring>
#include <sys/stat.h>
#include <thread>
#include <ctime>
#include <chrono>
#include <mutex>
#include <set>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Platform-specific includes only for system calls (stat, mkdir, etc.)
// Use PlatformDetector instead of direct platform checks where possible
#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif
#else
#include <windows.h>
#include "common/platform_macros.h"
#endif

// Path utilities moved to src/common/path_utils.h/cpp
// Use PathUtils::normalizePath() and PathUtils::joinPath() instead

App::App()
    : drag_drop_active_(false)
    , active_downloads_(0)
    , shutting_down_(false)
    , last_frame_time_(std::chrono::steady_clock::now())
    , last_activity_time_(std::chrono::steady_clock::now())
    , has_active_downloads_(false)
    , history_manager_(std::make_unique<HistoryManager>())
    , ui_renderer_(std::make_unique<UIRenderer>(this))
    , download_manager_(std::make_unique<DownloadManager>(this))
    , file_manager_(std::make_unique<FileManager>(this))
    , metadata_manager_(std::make_unique<MetadataManager>(this))
    , settings_(std::make_unique<Settings>())
    , service_checker_(std::make_unique<ServiceChecker>())
    , window_manager_(std::make_unique<WindowManager>())
    , event_handler_(std::make_unique<EventHandler>())
{
    // Initialize string buffers safely
    url_input_[0] = '\0';
    // API keys removed - using settings_->*_api_key directly
    ytdlp_update_in_progress_ = false;
    ytdlp_update_status_.clear();
    // ytdlp_version_ removed - using settings_->ytdlp_version
}

App::~App() {
    cleanup();
}

bool App::initialize() {
    std::cout << "[DEBUG] App::initialize: Starting application initialization" << std::endl;
    
    // Initialize WindowManager (handles SDL, window, renderer, ImGui)
    std::cout << "[DEBUG] App::initialize: Initializing WindowManager..." << std::endl;
    if (!window_manager_->initialize(900, 650, "YTDAudio")) {
        std::cerr << "[DEBUG] App::initialize: Failed to initialize WindowManager" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] App::initialize: WindowManager initialized successfully" << std::endl;
    
    // Setup ImGui through WindowManager
    std::cout << "[DEBUG] App::initialize: Setting up ImGui..." << std::endl;
    if (!window_manager_->setupImGui()) {
        std::cerr << "[DEBUG] App::initialize: Failed to setup ImGui" << std::endl;
        return false;
    }
    std::cout << "[DEBUG] App::initialize: ImGui setup completed" << std::endl;
    
    // window_ and renderer_ removed - using window_manager_->getWindow()/getRenderer() directly
    
    // Load settings
    std::cout << "[DEBUG] App::initialize: Loading settings..." << std::endl;
    settings_->load();
    std::cout << "[DEBUG] App::initialize: Settings loaded" << std::endl;
    
    // Settings fields removed - using settings_->* directly, no sync needed
    
    // Set default downloads directory if not set
    if (settings_->downloads_dir.empty() || settings_->downloads_dir == ".") {
        std::cout << "[DEBUG] App::initialize: Downloads directory not set, using default path" << std::endl;
        settings_->downloads_dir = PlatformUtils::getDownloadsPath();
    }
    std::cout << "[DEBUG] App::initialize: Downloads directory: " << settings_->downloads_dir << std::endl;
    
    // Validate downloads directory path before creating
    if (!settings_->downloads_dir.empty() && ValidationUtils::isValidPath(settings_->downloads_dir)) {
        PlatformUtils::createDirectory(settings_->downloads_dir);
        std::cout << "[DEBUG] App::initialize: Downloads directory validated and created if needed" << std::endl;
    } else {
        std::cerr << "[DEBUG] App::initialize: WARNING - Invalid downloads directory path: " << settings_->downloads_dir << ", using fallback" << std::endl;
        settings_->downloads_dir = ".";  // Fallback to current directory
    }
    
    // downloads_dir_ removed - using settings_->downloads_dir directly
    
    // Setup service checker proxy
    if (settings_->use_proxy && !settings_->proxy_input.empty()) {
        std::cout << "[DEBUG] App::initialize: Setting up proxy for service checker: " << settings_->proxy_input << std::endl;
        service_checker_->setProxy(settings_->proxy_input);
    } else {
        std::cout << "[DEBUG] App::initialize: No proxy configured" << std::endl;
    }
    
    // Setup event handler paste callback
    event_handler_->setPasteCallback([this](const std::string& text) {
        size_t text_len = text.length();
        size_t copy_len = (text_len < sizeof(url_input_) - 1) ? text_len : sizeof(url_input_) - 1;
        strncpy(url_input_, text.c_str(), copy_len);
        url_input_[copy_len] = '\0';
    });
    std::cout << "[DEBUG] App::initialize: Event handler configured" << std::endl;
    
    // Load history and restore completed tasks
    std::cout << "[DEBUG] App::initialize: Loading history..." << std::endl;
    loadHistory();
    
    // OPTIMIZATION: Combine version check and service availability check to avoid running yt-dlp twice
    // Check yt-dlp version on first run only if not loaded from config
    // Version check is needed only if version is NOT present OR version is empty
    bool need_version_check = !(settings_->ytdlp_version_present && !settings_->ytdlp_version.empty());
    
    std::cout << "[DEBUG] App::initialize: ytdlp_version_present=" << settings_->ytdlp_version_present 
              << ", ytdlp_version='" << settings_->ytdlp_version 
              << "', need_version_check=" << need_version_check << std::endl;
    
    if (need_version_check) {
        std::cout << "[DEBUG] App::initialize: yt-dlp version not in config, will check version during service check..." << std::endl;
    } else {
        std::cout << "[DEBUG] App::initialize: yt-dlp version from config: " << settings_->ytdlp_version << std::endl;
    }
    
    // Check service availability on startup (and version if needed)
    std::cout << "[DEBUG] App::initialize: Scheduling service availability check (will run in 2 seconds)..." << std::endl;
    runBackground([this, need_version_check]() {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!shutting_down_) {
            // If we need version check, do it first, then service check
            if (need_version_check) {
                std::cout << "[DEBUG] App::initialize: Checking yt-dlp version..." << std::endl;
                std::string version = Downloader::getYtDlpVersion();
                if (!version.empty() && version != "Unknown") {
                    std::cout << "[DEBUG] App::initialize: Detected yt-dlp version: " << version << std::endl;
                    settings_->ytdlp_version = version;
                    settings_->ytdlp_version_present = true;
                    settings_->save();
                } else {
                    std::cout << "[DEBUG] App::initialize: Could not detect yt-dlp version" << std::endl;
                }
            } else {
                std::cout << "[DEBUG] App::initialize: Skipping version check (already in config)" << std::endl;
            }
            
            // Now check service availability
            std::cout << "[DEBUG] App::initialize: Starting service availability check..." << std::endl;
            // Use wrapper function for consistency
            checkServiceAvailability(false, true);
        } else {
            std::cout << "[DEBUG] App::initialize: Skipping checks (shutdown in progress)" << std::endl;
        }
    });
    
    std::cout << "[DEBUG] App::initialize: Initialization completed successfully" << std::endl;
    return true;
}

void App::run() {
    bool running = true;
    
    while (running) {
        // Process events through EventHandler (eliminates code duplication)
        auto event_result = event_handler_->processEvents();
        
        if (event_result.should_quit) {
            running = false;
        }
        
        // Handle window resize
        if (event_result.window_resized) {
            if (window_manager_) {
                window_manager_->updateSize();
            }
        }
        
        // Handle file drop (paste is handled by EventHandler callback)
        if (event_result.file_dropped) {
            drag_drop_path_ = event_result.dropped_file_path;
            drag_drop_active_ = true;
        }
        
        // Adaptive FPS: Use MAX_FPS (30) when active, IDLE_FPS (30) when idle
        auto current_time = std::chrono::steady_clock::now();
        
        // Check if there's activity (downloads or recent UI changes)
        bool has_activity = false;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            has_active_downloads_ = (active_downloads_ > 0);
            // Check if any task is downloading or queued
            for (const auto& task : tasks_) {
                if (task->status == "downloading" || task->status == "queued") {
                    has_activity = true;
                    break;
                }
            }
        }
        
        // Update activity time if there's activity
        if (has_activity || has_active_downloads_) {
            last_activity_time_ = current_time;
        }
        
        // Determine target FPS based on activity
        // If activity was recent (within 2 seconds), use MAX_FPS, otherwise IDLE_FPS
        auto time_since_activity = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_activity_time_).count();
        bool is_active = (time_since_activity < 2000) || has_activity || has_active_downloads_;
        
        double target_frame_time_ms = is_active ? MAX_FRAME_TIME_MS : IDLE_FRAME_TIME_MS;
        
        // Limit FPS based on activity
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(current_time - last_frame_time_).count();
        double elapsed_ms = elapsed / 1000.0;
        
        if (elapsed_ms < target_frame_time_ms) {
            // Frame was too fast, sleep to maintain target FPS
            double sleep_time_ms = target_frame_time_ms - elapsed_ms;
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(sleep_time_ms * 1000)));
            current_time = std::chrono::steady_clock::now();  // Update time after sleep
        }
        
        update();
        render();
        
        // Check for user interaction after rendering (for next frame)
        // This will keep FPS at 60 when user is interacting
        ImGuiIO& io = ImGui::GetIO();
        // Check if any key is pressed (using new ImGui API)
        bool any_key_pressed = false;
        for (int i = 0; i < IM_ARRAYSIZE(io.KeysData); i++) {
            if (io.KeysData[i].Down) {
                any_key_pressed = true;
                break;
            }
        }
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || 
            io.MouseClicked[0] || io.MouseClicked[1] || io.MouseClicked[2] ||
            any_key_pressed || io.WantCaptureKeyboard) {
            last_activity_time_ = std::chrono::steady_clock::now();  // User is interacting, keep high FPS
        }
        
        // Update frame time after processing
        last_frame_time_ = current_time;
    }
}


void App::update() {
    // Quickly count active downloads and collect queued tasks
    std::vector<DownloadTask*> queued_tasks;
    int new_active_count = 0;
    bool had_activity = false;
    
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        // Count active downloads
        int prev_active = active_downloads_;
        new_active_count = 0;
        for (auto& task : tasks_) {
            if (task->status == "downloading") {
                new_active_count++;
                had_activity = true;
            } else if (task->status == "queued" && new_active_count < MAX_CONCURRENT_DOWNLOADS) {
                queued_tasks.push_back(task.get());
                had_activity = true;
            }
        }
        
        active_downloads_ = new_active_count;
        has_active_downloads_ = (new_active_count > 0);
        
        if (prev_active != active_downloads_) {
            // Update activity time when downloads change
            if (new_active_count > 0) {
                last_activity_time_ = std::chrono::steady_clock::now();
            }
        }
    } // Release lock early
    
    // Start queued downloads outside of lock (startDownload will acquire lock when needed)
    for (auto* task : queued_tasks) {
        if (active_downloads_ < MAX_CONCURRENT_DOWNLOADS) {
            // For playlists with separate folder option, wait for playlist name to be loaded from JSON
            // Don't start download until we have playlist name and can create the folder
            bool should_wait_for_playlist_name = false;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                if (settings_->save_playlists_to_separate_folder) {
                    // Check if URL looks like a playlist
                    std::string url_lower = task->url;
                    std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);
                    
                    // Use PlaylistDetector for centralized playlist detection
                    PlaylistDetector::PlaylistInfo playlist_info = PlaylistDetector::detectFromUrl(task->url, task->platform);
                    bool is_playlist_url = playlist_info.is_playlist;
                    
                    // If it's a playlist URL, wait until we get playlist info from JSON
                    // We need the name to create the folder before starting download
                    // But if we have playlist items, we can start download even without name
                    // Note: total_playlist_items is "soft" - we don't wait for it, will get from yt-dlp stdout
                    if (is_playlist_url || task->is_playlist) {
                        // If we have playlist items, we can start download (name is optional)
                        // Only wait if we don't have items yet (still loading)
                        if (task->playlist_name.empty() && task->playlist_items.empty()) {
                            // Wait until JSON is loaded and we have either playlist name or items
                            should_wait_for_playlist_name = true;
                        }
                    }
                }
            }
            
            if (!should_wait_for_playlist_name) {
                // CRITICAL: Run startDownloadImpl in background thread to avoid blocking UI
                // The blocking wait for playlist name inside startDownloadImpl will not freeze the interface
                // Increment counter before starting background thread
                active_downloads_++;
                last_activity_time_ = std::chrono::steady_clock::now();  // Mark activity
                runBackground([this, task]() {
                    startDownloadImpl(task);
                    // Note: active_downloads_ will be decremented in complete callback or in update() if status changes to error
                });
            }
        }
    }
}

void App::render() {
    // Update window size
    if (window_manager_) {
        window_manager_->updateSize();
    }
    
    // Begin ImGui frame through WindowManager
    if (window_manager_) {
        window_manager_->beginImGuiFrame();
    }
    
    // Render UI
    if (ui_renderer_) {
        ui_renderer_->renderUI();
    }
    
    // End ImGui frame and render through WindowManager
    if (window_manager_) {
        window_manager_->endImGuiFrame();
    }
}

// Note: renderUI(), renderSettings(), renderDownloadList(), renderProgressBar() 
// removed - these were unused wrappers. UI rendering is handled directly by UIRenderer.

void App::addDownloadTask(const std::string& url) {
    if (download_manager_) {
        download_manager_->addDownloadTask(url);
        return;
    }
    // Fallback to old implementation if download_manager_ is not initialized
    // Validate URL before processing
    if (!ValidationUtils::isValidUrl(url)) {
        return;
    }
    
    std::string processed_url = url;
    std::string platform_str;
    DownloadTask* task_ptr = nullptr;
    
    // Scope for lock - release before calling createHistoryItemImmediately to avoid deadlock
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        // Check if this URL was already downloaded (prevent duplicates)
        // Check both original and processed URL after processing
        if (history_urls_.find(processed_url) != history_urls_.end() || 
            history_urls_.find(url) != history_urls_.end()) {
            return;  // Don't add duplicate
        }
        
        auto task = std::make_unique<DownloadTask>(processed_url);
        detectPlatform(processed_url, task->platform);
        
        // Save platform string before moving task (to avoid accessing moved object)
        platform_str = task->platform;
        task_ptr = task.get();
        
        // Quick check if URL looks like a playlist (for immediate UI feedback)
        // Use PlaylistDetector for centralized playlist detection logic
        // For YouTube: check if URL contains list= parameter
        // For other platforms: will be determined by getPlaylistItems response
        if (PlaylistDetector::looksLikePlaylist(processed_url, task_ptr->platform)) {
            // Mark as playlist immediately so UI can show list structure
            task_ptr->is_playlist = true;
        }
        
        // CRITICAL: Add URL to history_urls_ BEFORE releasing lock to prevent duplicates
        // This ensures no other thread can add the same URL between lock release and createHistoryItemImmediately
        history_urls_.insert(processed_url);
        
        tasks_.push_back(std::move(task));
    } // Lock released here
    
    // CRITICAL: Create history item immediately with placeholder thumbnail
    // This ensures the card appears in history right away, even if service is unavailable
    // The card will be updated later as we get more information (playlist items, real thumbnail, etc.)
    // Note: createHistoryItemImmediately will update history_urls_ internally to prevent duplicates
    // Called OUTSIDE the lock to avoid deadlock
    createHistoryItemImmediately(task_ptr, platform_str);
    
    // Safe debug output - use saved platform string
    // Get playlist items asynchronously immediately after adding task (if playlists enabled)
    // This fills track names before download starts and doesn't block GUI
    // IMPORTANT: For YouTube and SoundCloud, this gets:
    // 1. Playlist name (for folder creation)
    // 2. Number of items (total_playlist_items)
    // 3. Determine if this is a playlist or single file by calling getPlaylistItems
    // NEW APPROACH: Always call getPlaylistItems to determine type based on number of elements
    // - If elements > 1: playlist
    // - If elements = 1: single file
    // - For YouTube: also check if URL contains list= parameter (quick check for UI feedback)
    // OPTIMIZATION: Check history first to avoid duplicate requests
    {
        // For YouTube: quick check if URL contains list= parameter (for immediate UI feedback)
        PlaylistDetector::PlaylistInfo url_playlist_info = PlaylistDetector::detectFromUrl(processed_url, platform_str);
        if (url_playlist_info.is_youtube_playlist) {
            // YouTube URL with list= parameter - mark as playlist immediately for UI
            task_ptr->is_playlist = true;
        }
        
        // Capture settings needed for folder operations
        bool use_separate_folder = settings_->save_playlists_to_separate_folder;
        std::string downloads_dir_copy = settings_->downloads_dir;
        
        // Always call getPlaylistItems to determine actual type (playlist vs single file)
        // This works for all platforms: YouTube, SoundCloud, etc.
        runBackground([this, task_ptr, processed_url, use_separate_folder, downloads_dir_copy]() {
                if (shutting_down_) {
                    return;
                }
                
                // OPTIMIZATION: Check history first - if URL is in history, use metadata from there
                bool from_history = false;
                Downloader::PlaylistInfo playlist_info;
                
                // Get history items BEFORE locking tasks_mutex_ to avoid deadlock
                std::vector<HistoryItem> history_items_snapshot;
                if (history_manager_) {
                    history_items_snapshot = history_manager_->getHistoryItems();
                }
                
                {
                    std::lock_guard<std::mutex> lock(tasks_mutex_);
                    for (const auto& item : history_items_snapshot) {
                        if (item.url == processed_url) {
                            // Found URL in history - use metadata from history
                            if (item.is_playlist && !item.playlist_items.empty()) {
                                // It's a playlist in history
                                playlist_info.playlist_name = item.playlist_name;
                                playlist_info.items.resize(item.playlist_items.size());
                                for (size_t i = 0; i < item.playlist_items.size(); i++) {
                                    playlist_info.items[i].index = static_cast<int>(i);
                                    playlist_info.items[i].title = item.playlist_items[i].title;
                                    playlist_info.items[i].id = item.playlist_items[i].id;
                                    playlist_info.items[i].duration = item.playlist_items[i].duration;
                                }
                                std::cout << "[DEBUG] Using playlist metadata from history for URL: " << processed_url 
                                          << " (" << playlist_info.items.size() << " items)" << std::endl;
                            } else {
                                // It's a single file in history - create empty playlist_info (0 items)
                                playlist_info.items.clear();
                                std::cout << "[DEBUG] Using single file metadata from history for URL: " << processed_url << std::endl;
                            }
                            from_history = true;
                            break;
                        }
                    }
                }
                
                // Only call getPlaylistItems if URL is not in history
                if (!from_history) {
                    std::string proxy_for_check = settings_->use_proxy ? ValidationUtils::normalizeProxy(settings_->proxy_input) : "";
                    
                    std::cout << "[DEBUG] addDownloadTask: Starting getPlaylistItems for URL: " << processed_url << std::endl;
                    auto start_time = std::chrono::steady_clock::now();
                    YtDlpSettings ytdlp_settings = createYtDlpSettings();
                    playlist_info = Downloader::getPlaylistItems(processed_url, proxy_for_check, ytdlp_settings);
                    auto end_time = std::chrono::steady_clock::now();
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                    std::cout << "[DEBUG] addDownloadTask: getPlaylistItems completed in " << elapsed_ms << " ms, found " 
                              << playlist_info.items.size() << " items" << std::endl;
                }
            
            if (shutting_down_) {
                return;
            }
            
            // Update task with playlist info
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                
                // Check if task still exists and is not cancelled
                if (task_ptr->status == "cancelled" || shutting_down_) {
                    return;
                }
                
                // Check for errors from getPlaylistItems (e.g., bot detection)
                if (!playlist_info.error_message.empty()) {
                    std::cout << "[DEBUG] addDownloadTask: getPlaylistItems returned error: " << playlist_info.error_message << std::endl;
                    task_ptr->status = "error";
                    task_ptr->error_message = playlist_info.error_message;
                    active_downloads_--;  // Decrement since we won't start download
                    return;
                }
                
                // NEW APPROACH: Determine playlist vs single file based on number of elements
                // - If elements > 1: playlist
                // - If elements = 1: single file
                // - If elements = 0: error or single file (treat as single file)
                if (playlist_info.items.size() > 1) {
                    // This is a playlist with multiple items
                    task_ptr->is_playlist = true;
                    task_ptr->total_playlist_items = static_cast<int>(playlist_info.items.size());
                    
                    // Set playlist name if available (for folder creation)
                    bool name_changed = false;
                    if (!playlist_info.playlist_name.empty() && task_ptr->playlist_name != playlist_info.playlist_name) {
                        task_ptr->playlist_name = playlist_info.playlist_name;
                        name_changed = true;
                    }
                    
                    // Note: We don't set thumbnail_url from getPlaylistItems anymore
                    // Thumbnail will be extracted from parseJsonProgress during download of first item
                    // This matches the behavior for single files
                    
                    // Pre-fill playlist items with titles immediately
                    // This ensures all track names are available for display
                    task_ptr->playlist_items.resize(playlist_info.items.size());
                    for (size_t i = 0; i < playlist_info.items.size(); i++) {
                        task_ptr->playlist_items[i].index = static_cast<int>(i);
                        task_ptr->playlist_items[i].title = playlist_info.items[i].title;
                        task_ptr->playlist_items[i].id = playlist_info.items[i].id;
                        task_ptr->playlist_items[i].duration = playlist_info.items[i].duration;
                        // We don't currently display duration string in UI, but duration can be used later
                    }
                    
                    // If playlist name was set and we're using separate folders, ensure folder exists
                    // NOTE: startDownload now waits for playlist name, so this is mainly for edge cases
                    // where the name loads very late or download hasn't started yet
                    if (name_changed && use_separate_folder && !playlist_info.playlist_name.empty()) {
                        std::string folder_name = sanitizeFilename(playlist_info.playlist_name);
                        std::string output_dir = downloads_dir_copy + "/" + folder_name;
                        
                        // Create folder if it doesn't exist (download may not have started yet)
                        if (ValidationUtils::isValidPath(output_dir)) {
                            if (!isDirectory(output_dir)) {
                                // Folder doesn't exist, create it
                                PlatformUtils::createDirectory(output_dir);
                            }
                        }
                    }
                } else {
                    // This is NOT a playlist (0 or 1 items) - explicitly set is_playlist to false
                    // This ensures that tasks don't get stuck waiting for playlist name
                    task_ptr->is_playlist = false;
                    task_ptr->total_playlist_items = 0;
                    task_ptr->playlist_items.clear();
                    task_ptr->playlist_name.clear();
                }
            }  // Release lock before checking files
            
            // Check if playlist files already exist before starting download
            // This handles the case where playlist is not in history but files exist
            // OPTIMIZATION: Do this check quickly and don't block download start
            // If files are found, we'll mark task as already_exists, but download can start in parallel
            bool files_found = false;
            // Check if this is a playlist before acquiring lock
            bool is_playlist_check = false;
            int total_items = 0;
            std::string task_status;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                is_playlist_check = (task_ptr->is_playlist && playlist_info.items.size() > 1 && task_ptr->status != "cancelled");
                if (is_playlist_check) {
                    total_items = static_cast<int>(playlist_info.items.size());
                    task_status = task_ptr->status;
                }
            }
            
            // Call checkExistingPlaylistFiles OUTSIDE the lock to avoid blocking UI
            // This function may take time to scan directory and match files
            if (is_playlist_check) {
                std::cout << "[DEBUG] addDownloadTask: Checking existing files for playlist (non-blocking)..." << std::endl;
                files_found = checkExistingPlaylistFiles(task_ptr, playlist_info);
                std::cout << "[DEBUG] addDownloadTask: File check completed, files_found=" << files_found << std::endl;
            }
            
            // If files were found, add to history and mark URL as downloaded
            // Do this outside the lock to avoid deadlock when addToHistory acquires its own lock
            if (files_found) {
                {
                    std::lock_guard<std::mutex> lock(tasks_mutex_);
                    // Add URL to history_urls_ to prevent duplicate downloads
                    if (task_ptr->status == "already_exists") {
                        history_urls_.insert(task_ptr->url);
                    }
                }
                // Call addToHistory outside lock (it acquires its own lock)
                addToHistory(task_ptr);
            }
        });  // End of runBackground for playlist detection
    }  // End of playlist detection block
}

void App::startDownload(DownloadTask* task) {
    if (download_manager_) {
        download_manager_->startDownload(task);
        return;
    }
    // Fallback: call implementation directly
    startDownloadImpl(task);
}

void App::startDownloadImpl(DownloadTask* task) {
    // Internal implementation - doesn't check download_manager_ to avoid recursion
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (task->status != "queued") {
            // Task status changed before we started - decrement counter
            active_downloads_--;
            return;
        }
        // Change status immediately to prevent double execution
        task->status = "downloading";
    }
    
    // Determine if this is a playlist URL and prepare output directory
    std::string output_dir = settings_->downloads_dir;
    bool is_playlist_url = false;
    
    // Check if URL is a playlist and get playlist name BEFORE downloading
    // This allows us to create the correct folder name immediately
    {
        // Use PlaylistDetector for centralized playlist detection
        PlaylistDetector::PlaylistInfo playlist_info = PlaylistDetector::detectFromUrl(task->url, task->platform);
        is_playlist_url = playlist_info.is_playlist;
        bool is_soundcloud_playlist = playlist_info.is_soundcloud_set;
        
        // Playlist info should already be loaded asynchronously in addDownloadTask
        // If using separate folders, wait for playlist name from JSON and create folder before starting download
        // Note: update() already waits before calling startDownload(), so this is a final check
        if (settings_->save_playlists_to_separate_folder && (is_playlist_url || is_soundcloud_playlist || task->is_playlist)) {
            // Wait until playlist name is available from JSON (no timeout - wait as long as needed)
            // Check every 100ms
            const int check_interval_ms = 100;
            
            // Wait only for playlist name - total_playlist_items is "soft" and will come from yt-dlp stdout
            while (task->playlist_name.empty()) {
                // Check if cancelled BEFORE sleeping to avoid blocking during shutdown
                {
                    std::lock_guard<std::mutex> lock(tasks_mutex_);
                    if (task->status == "cancelled" || shutting_down_) {
                        active_downloads_--; // Decrement counter if cancelled
                        return;
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms));
                
                // Re-check playlist name (it might have been loaded while we were sleeping)
                {
                    std::lock_guard<std::mutex> lock(tasks_mutex_);
                    if (!task->playlist_name.empty()) {
                        break;  // Got the name, exit wait loop
                    }
                }
            }
            
            // Now check if playlist info is available - it should be by now
            // We already waited for playlist_name, so if it's still empty, something went wrong
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                if (!task->playlist_name.empty()) {
                    // Use playlist name for folder (we have the name, that's what we waited for)
                    std::string folder_name = sanitizeFilename(task->playlist_name);
                    output_dir = settings_->downloads_dir + "/" + folder_name;
                    
                    // Validate output directory path before creating
                    if (!ValidationUtils::isValidPath(output_dir)) {
                        task->status = "error";
                        task->error_message = "Invalid output directory path";
                        active_downloads_--; // Decrement counter on error
                        return;
                    }
                    
                    // Create the playlist folder if it doesn't exist
                    // This MUST be done before starting download
                    struct stat st;
                    if (stat(output_dir.c_str(), &st) != 0) {
                        // Folder doesn't exist, create it
                        PlatformUtils::createDirectory(output_dir);
                    }
                    
                    // Ensure task is marked as playlist if we have playlist name
                    // (total_playlist_items might not be set yet, but will be set during download)
                    if (!task->is_playlist && task->total_playlist_items == 0) {
                        // Mark as playlist if we have playlist name (it means getPlaylistItems found items)
                        task->is_playlist = true;
                    }
                } else {
                    // This should not happen - we waited for the name
                    // But if it does, don't start download - return error
                    task->status = "error";
                    task->error_message = "Failed to get playlist name from JSON";
                    active_downloads_--; // Decrement counter on error
                    return;
                }
            }
        } else if (settings_->save_playlists_to_separate_folder) {
            // Not a detected playlist URL, but check if task was marked as playlist
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            if (task->is_playlist && !task->playlist_name.empty()) {
                // Use pre-loaded playlist name
                std::string folder_name = sanitizeFilename(task->playlist_name);
                output_dir = settings_->downloads_dir + "/" + folder_name;
                
                // Validate output directory path before creating
                if (!ValidationUtils::isValidPath(output_dir)) {
                    task->status = "error";
                    task->error_message = "Invalid output directory path";
                    active_downloads_--; // Decrement counter on error
                    return;
                }
                
                // Create the playlist folder if it doesn't exist
                PlatformUtils::createDirectory(output_dir);
            }
        }
    }
    
    // OPTIMIZATION: Check history for metadata before downloading
    // We no longer call getVideoInfo() here to avoid duplicate requests
    // Metadata will be extracted from JSON during download (downloadAsync already gets JSON with --print-json)
    // This reduces server load and avoids potential errors (e.g., missing cookies)
    // The first JSON line from downloadAsync arrives immediately, so metadata appears almost instantly
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        // Use PlaylistDetector for centralized playlist detection
        PlaylistDetector::PlaylistInfo playlist_info = PlaylistDetector::detectFromUrl(task->url, task->platform);
        
        // For SoundCloud, "sets/" in URL path doesn't necessarily mean it's a playlist
        bool url_is_playlist = false;
        if (playlist_info.is_soundcloud_set) {
            url_is_playlist = (task->total_playlist_items > 1 || 
                              (task->is_playlist && task->playlist_items.size() > 1));
        } else {
            url_is_playlist = playlist_info.is_playlist;
        }
        
        bool task_is_playlist = task->is_playlist && task->total_playlist_items > 1;
        bool is_playlist = url_is_playlist || task_is_playlist;
        
        // For single videos, check history for metadata (playlists already have metadata from getPlaylistItems)
        if (!is_playlist && history_manager_) {
            const auto& history_items = getHistoryItems();
            for (const auto& item : history_items) {
                if (item.url == task->url && !item.is_playlist) {
                    // Found URL in history - use metadata from history if available
                    if (!item.title.empty() || !item.artist.empty() || item.duration > 0 || !item.thumbnail_base64.empty()) {
                        if (task->metadata.title.empty() && !item.title.empty()) {
                            task->metadata.title = item.title;
                        }
                        if (task->metadata.artist.empty() && !item.artist.empty()) {
                            task->metadata.artist = item.artist;
                        }
                        if (task->metadata.duration == 0 && item.duration > 0) {
                            task->metadata.duration = item.duration;
                        }
                        if (task->thumbnail_url.empty() && !item.thumbnail_base64.empty()) {
                            // Note: We have thumbnail_base64 but need thumbnail_url - can't convert back, so skip
                            // Thumbnail will be loaded from history when rendering
                        }
                        task->metadata_loaded = true;
                        std::cout << "[DEBUG] Using metadata from history for URL: " << task->url << std::endl;
                        break;
                    }
                }
            }
        }
    }
    
    auto downloader = std::make_unique<Downloader>();
    Downloader* downloader_ptr = downloader.get();
    task->downloader_ptr = downloader_ptr; // Store pointer for cancellation
    
    // Create yt-dlp settings from UI variables
    YtDlpSettings ytdlp_settings = createYtDlpSettings();
    
    downloader_ptr->downloadAsync(
        task->url,
        output_dir,
        settings_->selected_format,
        settings_->selected_quality,
        settings_->use_proxy ? ValidationUtils::normalizeProxy(settings_->proxy_input) : "",
        std::string(settings_->spotify_api_key),
        std::string(settings_->youtube_api_key),
        std::string(settings_->soundcloud_api_key),
        task->is_playlist,  // Determined by getPlaylistItems: elements > 1 = playlist (true), elements <= 1 = single file (false)
        [this, task](const Downloader::ProgressInfo& info) {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            // Don't update status if already cancelled
            if (task->status == "cancelled") {
                return;
            }
            task->progress = info.progress;
            // Always keep status as "downloading" during active download
            // This ensures Cancel button remains visible throughout the download
            if (info.status.empty() || info.status == "downloading" || info.status == "postprocessing" || info.status == "merging") {
                task->status = "downloading";  // Keep as "downloading" to show Cancel button
            } else if (info.status == "completed" || info.status == "error") {
                // Only change to completed/error when download actually finishes
                task->status = info.status;
            }
            // For other statuses, keep as "downloading" to maintain Cancel button visibility
            
            // Don't update file size from progress - it may be the original file size before conversion
            // We'll get the actual file size after download completes
            
            // Update thumbnail URL from first playlist item if available
            // For playlists, always update if we get a thumbnail from the first item (index 0 or -1)
            // For single files, only update if thumbnail_url is empty
            bool is_first_playlist_item = (info.is_playlist || task->is_playlist) && 
                                          (info.current_item_index == 0 || info.current_item_index == -1);
            if (!info.thumbnail_url.empty()) {
                if (is_first_playlist_item || task->thumbnail_url.empty()) {
                    task->thumbnail_url = info.thumbnail_url;
                    std::cout << "[DEBUG] Set thumbnail_url from parseJsonProgress: " << task->thumbnail_url 
                              << " (is_first_playlist_item=" << is_first_playlist_item << ")" << std::endl;
                }
            }
            
            // Update playlist information
            // IMPORTANT: For SoundCloud, the first track's title may arrive BEFORE is_playlist is set to true
            // So we need to check if this is a playlist by checking task->is_playlist OR if we have playlist_items
            // This ensures the first item's title is updated even if is_playlist arrives later
            // BUT: For SoundCloud, if there's only 1 item, don't treat it as a playlist
            bool is_playlist_update = info.is_playlist || task->is_playlist || !task->playlist_items.empty();
            
            // Check if this should actually be a playlist (more than 1 item for SoundCloud)
            bool should_be_playlist = true;
            if (task->platform == "SoundCloud") {
                // For SoundCloud, only treat as playlist if more than 1 item
                if (task->playlist_items.size() == 1 && !info.is_playlist) {
                    should_be_playlist = false;
                } else if (info.total_items > 0 && info.total_items == 1) {
                    should_be_playlist = false;
                }
            }
            
            if (is_playlist_update && should_be_playlist) {
                task->is_playlist = true;
                int old_current_item = task->current_playlist_item;
                
                // For SoundCloud playlists, info.current_item_index might be incorrect during download
                // Use a more reliable method: find the item by title match if index seems wrong
                int actual_item_index = info.current_item_index;
                
                // Special handling for first item (index 0): 
                // Case 1: old_current_item is -1 and info.current_item_index is 0 (normal case)
                // Case 2: old_current_item is -1 and info.current_item_index is -1 BUT we have a title
                //         This happens when title arrives BEFORE current_item_index is set to 0 in downloader
                //         In this case, use index 0 directly to update the first item's title
                // Case 3: info.current_item_index is 0 (even if old_current_item is already 0)
                //         This ensures first item always uses index 0, preventing it from being matched to wrong position
                if (!task->playlist_items.empty()) {
                    if (info.current_item_index == 0) {
                        // CRITICAL FIX: Always use index 0 when info.current_item_index is 0
                        // This prevents first item from being matched to wrong position by title matching
                        actual_item_index = 0;
                    } else if (old_current_item == -1 && info.current_item_index < 0 && !info.current_item_title.empty()) {
                        // Title arrived before current_item_index was set - this is the first item
                        // Check if first item still has placeholder title
                        if (task->playlist_items[0].title.empty() || task->playlist_items[0].title.find("Item ") == 0) {
                            actual_item_index = 0;
                        }
                    }
                }
                
                // If we have a title and playlist_items is pre-filled, try to match by title
                // This is more reliable than trusting current_item_index which might be wrong for SoundCloud
                // Skip title matching for first item if we already set actual_item_index to 0 above
                // Also skip if this is the first item and title arrived before index was set
                // CRITICAL FIX: Also skip if info.current_item_index is 0 (first item) to prevent wrong matching
                bool skip_title_matching = (info.current_item_index == 0) ||
                                          (old_current_item == -1 && info.current_item_index < 0 && 
                                           !info.current_item_title.empty() && 
                                           !task->playlist_items.empty() &&
                                           (task->playlist_items[0].title.empty() || task->playlist_items[0].title.find("Item ") == 0));
                if (!info.current_item_title.empty() && !task->playlist_items.empty() && !skip_title_matching) {
                    // First, try to find exact match by title, but prefer items that haven't been downloaded yet
                    // Start search from old_current_item + 1 (next item) or from 0 if old_current_item is invalid
                    bool found_by_title = false;
                    int search_start = (old_current_item >= 0) ? old_current_item + 1 : 0;
                    
                    // First pass: search from search_start to end (prefer next items)
                    for (int i = search_start; i < static_cast<int>(task->playlist_items.size()); i++) {
                        if (task->playlist_items[i].title == info.current_item_title) {
                            actual_item_index = i;
                            found_by_title = true;
                            break;
                        }
                    }
                    
                    // Second pass: if not found, search from beginning to search_start (backward search)
                    if (!found_by_title && search_start > 0) {
                        for (int i = 0; i < search_start; i++) {
                            if (task->playlist_items[i].title == info.current_item_title && !task->playlist_items[i].downloaded) {
                                actual_item_index = i;
                                found_by_title = true;
                                break;
                            }
                        }
                    }
                    
                    // If not found by exact match and we have a valid current_playlist_item, check if title matches
                    if (!found_by_title && task->current_playlist_item >= 0 && 
                        task->current_playlist_item < static_cast<int>(task->playlist_items.size())) {
                        if (task->playlist_items[task->current_playlist_item].title == info.current_item_title) {
                            actual_item_index = task->current_playlist_item;
                            found_by_title = true;
                        }
                    }
                    
                    // If still not found and title changed, increment from old_current_item
                    if (!found_by_title && old_current_item >= 0 && 
                        old_current_item < static_cast<int>(task->playlist_items.size())) {
                        if (task->playlist_items[old_current_item].title != info.current_item_title) {
                            actual_item_index = old_current_item + 1;
                            if (actual_item_index >= static_cast<int>(task->playlist_items.size())) {
                                actual_item_index = old_current_item; // Don't go out of bounds
                            }
                        } else {
                            actual_item_index = old_current_item;
                        }
                    }
                }
                
                // Fallback 1: use info.current_item_index if it's valid
                if (actual_item_index < 0 && info.current_item_index >= 0 && 
                    info.current_item_index < static_cast<int>(task->playlist_items.size())) {
                    actual_item_index = info.current_item_index;
                }
                
                // Fallback 2: use current_playlist_item if it's valid (even if current_item_title is empty)
                if (actual_item_index < 0 && task->current_playlist_item >= 0 && 
                    task->current_playlist_item < static_cast<int>(task->playlist_items.size())) {
                    actual_item_index = task->current_playlist_item;
                }
                
                // Fallback 3: use old_current_item if it's valid (for progress updates without title)
                if (actual_item_index < 0 && old_current_item >= 0 && 
                    old_current_item < static_cast<int>(task->playlist_items.size())) {
                    actual_item_index = old_current_item;
                }
                
                // Fallback 4: if we're downloading but don't have an index, start from 0
                if (actual_item_index < 0 && task->status == "downloading" && !task->playlist_items.empty()) {
                    // Check if any item is already downloaded to determine starting point
                    int first_not_downloaded = -1;
                    for (size_t i = 0; i < task->playlist_items.size(); i++) {
                        if (!task->playlist_items[i].downloaded) {
                            first_not_downloaded = static_cast<int>(i);
                            break;
                        }
                    }
                    if (first_not_downloaded >= 0) {
                        actual_item_index = first_not_downloaded;
                    } else {
                        // All items downloaded or none started - use 0
                        actual_item_index = 0;
                    }
                }
                
                // Update current_playlist_item only if we have a valid index
                if (actual_item_index >= 0 && actual_item_index < static_cast<int>(task->playlist_items.size())) {
                    task->current_playlist_item = actual_item_index;
                } else {
                }
                
                // Set total_playlist_items from info.total_items, but fallback to playlist_items.size() if 0
                if (info.total_items > 0) {
                    task->total_playlist_items = info.total_items;
                } else if (!task->playlist_items.empty() && task->total_playlist_items == 0) {
                    task->total_playlist_items = static_cast<int>(task->playlist_items.size());
                }
                task->current_item_title = info.current_item_title;
                
                // Update playlist name if provided (only if we didn't get it before download started)
                // NOTE: This is a fallback - playlist name should already be loaded via getPlaylistItems
                // in addDownloadTask, and startDownload now waits for it. This handles rare edge cases.
                if (!info.playlist_name.empty() && task->playlist_name.empty()) {
                    task->playlist_name = info.playlist_name;
                    
                    // NOTE: We cannot rename the folder at this point because yt-dlp is already downloading
                    // to the output directory that was passed to it. The folder rename logic here won't help
                    // because files will continue downloading to the old folder.
                    // This is why startDownload now waits for the playlist name before starting the download.
                    if (settings_->save_playlists_to_separate_folder && !task->playlist_name.empty()) {
                    }
                }
                
                // Mark previous item as completed when moving to next item
                if (old_current_item >= 0 && old_current_item != actual_item_index && 
                    old_current_item < static_cast<int>(task->playlist_items.size())) {
                    task->playlist_items[old_current_item].downloaded = true;
                }
                
                // Initialize playlist_items vector with correct size and default indices
                // Only if not already initialized (titles should be filled BEFORE download starts)
                if (task->playlist_items.size() < static_cast<size_t>(info.total_items)) {
                    size_t old_size = task->playlist_items.size();
                    task->playlist_items.resize(info.total_items);
                    // Initialize new items with default index and placeholder title
                    for (size_t idx = old_size; idx < task->playlist_items.size(); idx++) {
                        task->playlist_items[idx].index = static_cast<int>(idx);
                        // Only set placeholder if title is empty (shouldn't happen if pre-filled)
                        if (task->playlist_items[idx].title.empty()) {
                            task->playlist_items[idx].title = "Item " + std::to_string(idx + 1);
                        }
                    }
                }
                
                // Update playlist item title during download using the corrected index
                // For SoundCloud and other platforms, titles might not be available before download
                // Always update if we have a valid title from JSON, even if it was pre-filled
                // Use actual_item_index if valid, otherwise fallback to info.current_item_index
                int item_index_to_update = actual_item_index;
                
                if (item_index_to_update < 0 && info.current_item_index >= 0 && 
                    info.current_item_index < static_cast<int>(task->playlist_items.size())) {
                    // Fallback 1: use info.current_item_index if actual_item_index is invalid
                    // This is especially important for the first item (index 0) where actual_item_index
                    // might not be correctly computed due to old_current_item being -1
                    item_index_to_update = info.current_item_index;
                } else if (item_index_to_update < 0 && old_current_item == -1 && 
                           !info.current_item_title.empty() && !task->playlist_items.empty() &&
                           (task->playlist_items[0].title.empty() || task->playlist_items[0].title.find("Item ") == 0)) {
                    // Fallback 2: if actual_item_index is still -1, but this is the first item (old_current_item == -1)
                    // and we have a title, and first item still has placeholder, use index 0
                    // This handles the case when title arrives before current_item_index is set in downloader
                    item_index_to_update = 0;
                }
                
                if (item_index_to_update >= 0 && item_index_to_update < static_cast<int>(task->playlist_items.size())) {
                    std::string& existing_title = task->playlist_items[item_index_to_update].title;
                    
                    // Try to get title from current_item_title first
                    if (!info.current_item_title.empty()) {
                        // Always update if title from JSON is different (more reliable than pre-filled)
                        // This is especially important for SoundCloud where pre-filled titles might be empty
                        // Also update if existing title is empty or is a placeholder ("Item X")
                        bool should_update = existing_title != info.current_item_title || 
                                           existing_title.empty() || 
                                           existing_title.find("Item ") == 0;
                        
                        if (should_update) {
                            std::string old_title = existing_title;
                            existing_title = info.current_item_title;
                            task->playlist_items[item_index_to_update].index = item_index_to_update;
                        }
                    } else if (existing_title.empty() || existing_title.find("Item ") == 0) {
                        // If current_item_title is empty and we have a placeholder, try to use task->current_item_title
                        // This happens when JSON doesn't contain title but task has it from previous updates
                        if (!task->current_item_title.empty() && task->current_item_title != existing_title) {
                            existing_title = task->current_item_title;
                            task->playlist_items[item_index_to_update].index = item_index_to_update;
                        }
                    }
                }
                
                // Update duration for current playlist item from JSON
                // NOTE: bitrate is NOT taken from JSON - it will be calculated from file_size and duration after conversion
                if (item_index_to_update >= 0 && item_index_to_update < static_cast<int>(task->playlist_items.size())) {
                    if (info.duration > 0 && task->playlist_items[item_index_to_update].duration == 0) {
                        task->playlist_items[item_index_to_update].duration = info.duration;
                    }
                    
                    // Calculate bitrate from file size and duration after conversion
                    // This gives accurate bitrate of the converted file, not the source stream bitrate
                    if (task->playlist_items[item_index_to_update].bitrate == 0 && 
                        task->playlist_items[item_index_to_update].duration > 0 && 
                        task->playlist_items[item_index_to_update].file_size > 0) {
                        task->playlist_items[item_index_to_update].bitrate = AudioUtils::calculateBitrate(
                            task->playlist_items[item_index_to_update].file_size,
                            task->playlist_items[item_index_to_update].duration
                        );
                    }
                    
                    // Save file path from progress info if available
                    if (!info.current_file_path.empty() && task->playlist_items[item_index_to_update].file_path.empty()) {
                        std::string normalized_path = PathUtils::normalizePath(info.current_file_path);
                        
                        // Try to find final converted file in the requested format
                        std::string target_format = settings_->selected_format;
                        if (!target_format.empty() && normalized_path.find_last_of(".") != std::string::npos) {
                            size_t last_dot = normalized_path.find_last_of(".");
                            std::string current_ext = normalized_path.substr(last_dot);
                            std::string target_ext = "." + target_format;
                            
                            // If path has intermediate format (opus, webm), try to find final format
                            // Only .opus and .webm are truly intermediate - they are raw download formats
                            // .m4a, .ogg, .flac, .wav are valid final formats, not intermediate
                            if (current_ext != target_ext) {
                                // Only .opus and .webm are intermediate formats
                                bool is_intermediate = (current_ext == ".opus" || current_ext == ".webm");
                                
                                if (is_intermediate) {
                                    std::string base_path = normalized_path.substr(0, last_dot);
                                    std::string final_path = base_path + target_ext;
                                    if (fileExists(final_path)) {
                                        normalized_path = final_path;
#ifdef _WIN32
                                        std::string debug_msg = "[DEBUG] Found final " + target_format + " file: " + normalized_path;
                                        writeConsoleUtf8(debug_msg + "\n");
#else
                                        std::cout << "[DEBUG] Found final " << target_format << " file: " << normalized_path << std::endl;
#endif
                                    } else {
                                        // CRITICAL: Don't save intermediate file path - it will be deleted after conversion
                                        // Wait for the final converted file to appear
#ifdef _WIN32
                                        std::string debug_msg = "[DEBUG] Skipping intermediate file (will be converted): " + normalized_path;
                                        writeConsoleUtf8(debug_msg + "\n");
#else
                                        std::cout << "[DEBUG] Skipping intermediate file (will be converted): " << normalized_path << std::endl;
#endif
                                        normalized_path.clear();  // Don't save this path
                                    }
                                }
                            }
                        }
                        
                        // Only save path if it's not empty, not intermediate, and not a temporary file
                        if (!normalized_path.empty() && !ValidationUtils::isTemporaryFile(normalized_path)) {
                            task->playlist_items[item_index_to_update].file_path = normalized_path;
                            task->playlist_item_file_paths[item_index_to_update] = normalized_path;
#ifdef _WIN32
                            std::string debug_msg = "[DEBUG] Saved file path for item " + std::to_string(item_index_to_update) + " from progress_cb: " + normalized_path;
                            writeConsoleUtf8(debug_msg + "\n");
#else
                            std::cout << "[DEBUG] Saved file path for item " << item_index_to_update 
                                      << " from progress_cb: " << normalized_path << std::endl;
#endif
                        }
                    }
                }
            }
            
            // Extract and save metadata from JSON for single videos (if not already loaded)
            // This is the primary source of metadata - we no longer call getVideoInfo() separately
            // to avoid duplicate requests. JSON arrives immediately at the start of download.
            if (!task->is_playlist && !task->metadata_loaded) {
                // Save title from JSON if available
                if (!info.current_item_title.empty() && task->metadata.title.empty()) {
                    task->metadata.title = info.current_item_title;
                    std::cout << "[DEBUG] Saved metadata.title from JSON during download: " << task->metadata.title << std::endl;
                }
                
                // Save duration from JSON if available
                if (info.duration > 0 && task->metadata.duration == 0) {
                    task->metadata.duration = info.duration;
                    std::cout << "[DEBUG] Saved metadata.duration from JSON during download: " << task->metadata.duration << "s" << std::endl;
                } else if (info.duration == 0) {
                    // Log if duration is not available in JSON (for debugging)
                    std::cout << "[DEBUG] Duration not found in JSON (info.duration=0) for single file" << std::endl;
                }
                
                // Mark as loaded if we got at least title or duration
                if (!task->metadata.title.empty() || task->metadata.duration > 0) {
                    task->metadata_loaded = true;
                    std::cout << "[DEBUG] Marked metadata as loaded from JSON during download" << std::endl;
                }
            }
            
            if (info.progress > 0) {
                if (info.is_playlist) {
                }
            }
        },
        [this, task](const std::string& file_path, const std::string& error) {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            active_downloads_--; // Decrement active downloads count
            
            if (error.empty()) {
                // For playlists, empty file_path is acceptable - items are saved in playlist_item_file_paths
                // Only validate path if it's not empty
                if (!file_path.empty() && !ValidationUtils::isValidPath(file_path)) {
                    // Don't mark as completed if path is invalid
                    task->status = "error";
                    task->error_message = "Invalid file path received";
                    return;
                }
                
                // For playlists with empty path, this is normal - process finished, items are in playlist_item_file_paths
                if (task->is_playlist && file_path.empty()) {
                }
                
                // For playlists, save file path for the current item and mark as downloaded BEFORE setting status
                if (task->is_playlist && task->current_playlist_item >= 0 && !file_path.empty()) {
                    int item_idx = task->current_playlist_item;
                    // CRITICAL: Normalize file path for Windows compatibility (use backslashes)
                    std::string normalized_file_path = PathUtils::normalizePath(file_path);
                    
                    // Check if this is an intermediate format that will be converted
                    std::string target_format = settings_->selected_format;
                    if (ValidationUtils::isIntermediateFormat(normalized_file_path, target_format)) {
                        // Try to find the final converted file
                        size_t last_dot = normalized_file_path.find_last_of('.');
                        if (last_dot != std::string::npos) {
                            std::string base_path = normalized_file_path.substr(0, last_dot);
                            std::string final_path = base_path + "." + target_format;
                            if (fileExists(final_path)) {
                                normalized_file_path = final_path;
                                std::cout << "[DEBUG] Complete callback: Found final " << target_format << " file: " << normalized_file_path << std::endl;
                            } else {
                                // Don't save intermediate file path - it will be deleted
                                std::cout << "[DEBUG] Complete callback: Skipping intermediate file (not converted yet): " << normalized_file_path << std::endl;
                                normalized_file_path.clear();
                            }
                        }
                    }
                    
                    // Only save if not an intermediate file and not a temporary file
                    if (!normalized_file_path.empty() && !ValidationUtils::isTemporaryFile(normalized_file_path)) {
                        task->playlist_item_file_paths[item_idx] = normalized_file_path;
                        // Mark item as downloaded and save metadata
                        if (item_idx >= 0 && item_idx < static_cast<int>(task->playlist_items.size())) {
                            task->playlist_items[item_idx].downloaded = true;
                            task->playlist_items[item_idx].file_path = normalized_file_path;
                        
                            // Update file_size for current item if file exists
                            if (task->playlist_items[item_idx].file_size == 0 && !normalized_file_path.empty()) {
                                int64_t file_size = getFileSize(normalized_file_path);
                                if (file_size >= 0) {
                                    task->playlist_items[item_idx].file_size = file_size;
                                    
                                    // Calculate bitrate if duration is available but bitrate is not
                                    if (task->playlist_items[item_idx].bitrate == 0 && 
                                        task->playlist_items[item_idx].duration > 0 && 
                                        task->playlist_items[item_idx].file_size > 0) {
                                        task->playlist_items[item_idx].bitrate = AudioUtils::calculateBitrate(
                                            task->playlist_items[item_idx].file_size,
                                            task->playlist_items[item_idx].duration
                                        );
                                        std::cout << "[DEBUG] Calculated bitrate for item " << item_idx 
                                                  << " (last item): " << task->playlist_items[item_idx].bitrate 
                                                  << " kbps (size=" << task->playlist_items[item_idx].file_size 
                                                  << ", duration=" << task->playlist_items[item_idx].duration << ")" << std::endl;
                                    }
                                }
                            }
                        }
                    }
                } else if (!task->is_playlist && !file_path.empty()) {
                    // For single files, set the file_path directly from the downloader
                    // This is the actual path determined by yt-dlp (or our fallback logic)
                    task->file_path = file_path;
#ifdef _WIN32
                    writeConsoleUtf8("[DEBUG] Set task->file_path from complete_cb: " + task->file_path + "\n");
#else
                    std::cout << "[DEBUG] Set task->file_path from complete_cb: " << task->file_path << std::endl;
#endif
                }
                
                // complete_cb is called when yt-dlp process has finished
                // For playlists, this means all available items have been processed (downloaded or skipped)
                // We should NOT check downloaded_count >= total_playlist_items because:
                // 1. yt-dlp may skip unavailable items
                // 2. total_playlist_items may not match actually downloaded items
                // 3. If process finished, the download is complete regardless of item count
                // CRITICAL: Don't overwrite "cancelled" status
                if (task->status != "cancelled") {
                    task->status = "completed";
                }
                
                if (task->is_playlist) {
                    // Ensure is_playlist is set if we have playlist_items
                    if (!task->playlist_items.empty()) {
                        task->is_playlist = true;
                    }
                    // Set total_playlist_items from playlist_items size if not set
                    if (task->total_playlist_items == 0 && !task->playlist_items.empty()) {
                        task->total_playlist_items = static_cast<int>(task->playlist_items.size());
                    }
                    
                    // Count downloaded items for logging only
                    int downloaded_count = 0;
                    for (const auto& item : task->playlist_items) {
                        if (item.downloaded) {
                            downloaded_count++;
                        }
                    }
                    
                    // Determine output directory
                    std::string output_dir = settings_->downloads_dir;
                    if (settings_->save_playlists_to_separate_folder && !task->playlist_name.empty()) {
                        std::string folder_name = sanitizeFilename(task->playlist_name);
                        output_dir = settings_->downloads_dir + "/" + folder_name;
                    }
                    
                    // Fill file_path and file_size for all playlist items
                    // First, use paths from playlist_item_file_paths
                    downloaded_count += processPlaylistItemFilePaths(task);
                    
                    // For items without file_path, try to find files in output directory
                    // This handles cases where yt-dlp didn't provide file_path for each item
                    for (size_t i = 0; i < task->playlist_items.size(); i++) {
                        if (task->playlist_items[i].file_path.empty() && task->playlist_items[i].downloaded) {
                            // Try to find file by matching title or index
                            // This is a fallback - ideally file_path should be set during download
                            std::string search_title = sanitizeFilename(task->playlist_items[i].title);
                            
#ifdef _WIN32
                            // Use Unicode version (FindFirstFileW) for proper support of Russian and other non-ASCII filenames
                            int dir_size_needed = MultiByteToWideChar(CP_UTF8, 0, output_dir.c_str(), -1, NULL, 0);
                            if (dir_size_needed > 0) {
                                std::vector<wchar_t> dir_wide(dir_size_needed);
                                MultiByteToWideChar(CP_UTF8, 0, output_dir.c_str(), -1, dir_wide.data(), dir_size_needed);
                                std::wstring search_pattern = std::wstring(dir_wide.data()) + L"\\*";
                                
                                WIN32_FIND_DATAW find_data;
                                HANDLE find_handle = FindFirstFileW(search_pattern.c_str(), &find_data);
                                if (find_handle != INVALID_HANDLE_VALUE) {
                                    do {
                                        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                            // Convert filename from wide string to UTF-8
                                            int filename_size = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, NULL, 0, NULL, NULL);
                                            if (filename_size > 0) {
                                                std::vector<char> filename_utf8(filename_size);
                                                WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, filename_utf8.data(), filename_size, NULL, NULL);
                                                std::string file_name_utf8(filename_utf8.data());
                                                std::string file_path = PathUtils::joinPath(output_dir, file_name_utf8);
                                                
                                                // Check if filename contains title (case-insensitive)
                                                std::string file_name_lower = file_name_utf8;
                                                std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);
                                                std::string search_title_lower = search_title;
                                                std::transform(search_title_lower.begin(), search_title_lower.end(), search_title_lower.begin(), ::tolower);
                                                
                                                if (file_name_lower.find(search_title_lower) != std::string::npos) {
                                                    // Check if this file is not already assigned to another item
                                                    bool already_assigned = false;
                                                    for (size_t j = 0; j < task->playlist_items.size(); j++) {
                                                        if (j != i && task->playlist_items[j].file_path == file_path) {
                                                            already_assigned = true;
                                                            break;
                                                        }
                                                    }
                                                    if (!already_assigned) {
                                                        task->playlist_items[i].file_path = file_path;
                                                        task->playlist_item_file_paths[static_cast<int>(i)] = file_path;
                                                        int64_t file_size = getFileSize(file_path);
                                                        if (file_size >= 0) {
                                                            task->playlist_items[i].file_size = file_size;
                                                        }
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    } while (FindNextFileW(find_handle, &find_data));
                                    FindClose(find_handle);
                                }
                            }
#else
                            DIR* dir = opendir(output_dir.c_str());
                            if (dir != nullptr) {
                                struct dirent* entry;
                                while ((entry = readdir(dir)) != nullptr) {
                                    if (entry->d_name[0] == '.') continue;
                                    
                                    std::string file_path = output_dir + "/" + entry->d_name;
                                    if (isRegularFile(file_path)) {
                                        std::string file_name = entry->d_name;
                                        // Check if filename contains title (case-insensitive)
                                        std::string file_name_lower = file_name;
                                        std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);
                                        std::string search_title_lower = search_title;
                                        std::transform(search_title_lower.begin(), search_title_lower.end(), search_title_lower.begin(), ::tolower);
                                        
                                        if (file_name_lower.find(search_title_lower) != std::string::npos) {
                                            // Check if this file is not already assigned to another item
                                            bool already_assigned = false;
                                            for (size_t j = 0; j < task->playlist_items.size(); j++) {
                                                if (j != i && task->playlist_items[j].file_path == file_path) {
                                                    already_assigned = true;
                                                    break;
                                                }
                                            }
                                            if (!already_assigned) {
                                                task->playlist_items[i].file_path = file_path;
                                                task->playlist_item_file_paths[static_cast<int>(i)] = file_path;
                                                int64_t item_file_size = getFileSize(file_path);
                                                if (item_file_size >= 0) {
                                                    task->playlist_items[i].file_size = item_file_size;
                                                }
                                                
                                                // Calculate bitrate if duration is available
                                                if (task->playlist_items[i].bitrate == 0 && 
                                                    task->playlist_items[i].duration > 0 && 
                                                    task->playlist_items[i].file_size > 0) {
                                                    task->playlist_items[i].bitrate = AudioUtils::calculateBitrate(
                                                        task->playlist_items[i].file_size,
                                                        task->playlist_items[i].duration
                                                    );
                                                }
                                                break;
                                            }
                                        }
                                    }
                                }
                                closedir(dir);
                            }
#endif
                        }
                    }
                    
                    // For items that still don't have duration/bitrate, try to get metadata from file
                    // This is especially important for the last item, which may not have received progress updates
                    for (size_t i = 0; i < task->playlist_items.size(); i++) {
                        if (task->playlist_items[i].downloaded && 
                            (task->playlist_items[i].duration == 0 || task->playlist_items[i].bitrate == 0)) {
                            std::string item_file_path = task->playlist_items[i].file_path;
                            if (item_file_path.empty() && task->playlist_item_file_paths.find(static_cast<int>(i)) != task->playlist_item_file_paths.end()) {
                                item_file_path = task->playlist_item_file_paths[static_cast<int>(i)];
                            }
                            
                            if (!item_file_path.empty()) {
                                // Get file size if not set
                                if (task->playlist_items[i].file_size == 0) {
                                    int64_t file_size = getFileSize(item_file_path);
                                    if (file_size >= 0) {
                                        task->playlist_items[i].file_size = file_size;
                                    }
                                }
                                
                                // REMOVED: getVideoInfo call for playlist items - metadata already obtained via getPlaylistItems and JSON during download
                                // If metadata is still missing, calculate bitrate from file size and duration if available
                                if (task->playlist_items[i].bitrate == 0 && 
                                    task->playlist_items[i].duration > 0 && 
                                    task->playlist_items[i].file_size > 0) {
                                    // Calculate bitrate from file size and duration if duration is available
                                    task->playlist_items[i].bitrate = AudioUtils::calculateBitrate(
                                        task->playlist_items[i].file_size,
                                        task->playlist_items[i].duration
                                    );
                                    std::cout << "[DEBUG] Calculated bitrate for playlist item " << i 
                                              << " from file size and duration: " << task->playlist_items[i].bitrate 
                                              << " kbps (size=" << task->playlist_items[i].file_size 
                                              << ", duration=" << task->playlist_items[i].duration << "s)" << std::endl;
                                }
                            }
                        }
                    }
                    
                    // Final pass: calculate bitrate for all items that have file_size and duration but no bitrate
                    // CRITICAL: This must complete BEFORE addToHistory is called
                    for (size_t i = 0; i < task->playlist_items.size(); i++) {
                        if (task->playlist_items[i].bitrate == 0 && 
                            task->playlist_items[i].duration > 0 && 
                            task->playlist_items[i].file_size > 0) {
                            task->playlist_items[i].bitrate = AudioUtils::calculateBitrate(
                                task->playlist_items[i].file_size,
                                task->playlist_items[i].duration
                            );
                            std::cout << "[DEBUG] Final pass: Calculated bitrate for item " << i 
                                      << ": " << task->playlist_items[i].bitrate << " kbps" << std::endl;
                        }
                    }
                    
                    // CRITICAL: Ensure file_size is set for ALL items from playlist_item_file_paths
                    // This is especially important for the last item, which may not have been processed yet
                    // Do this AFTER all other processing to catch any items that were missed
                    for (const auto& pair : task->playlist_item_file_paths) {
                        if (pair.first >= 0 && pair.first < static_cast<int>(task->playlist_items.size())) {
                            // Update file_size if not set
                            if (task->playlist_items[pair.first].file_size == 0 && !pair.second.empty()) {
                                int64_t file_size = getFileSize(pair.second);
                                if (file_size >= 0) {
                                    task->playlist_items[pair.first].file_size = file_size;
                                    std::cout << "[DEBUG] Final check: Set file_size for item " << pair.first 
                                              << ": " << task->playlist_items[pair.first].file_size << " bytes" << std::endl;
                                    
                                    // Recalculate bitrate if we now have file_size and duration
                                    if (task->playlist_items[pair.first].bitrate == 0 && 
                                        task->playlist_items[pair.first].duration > 0 && 
                                        task->playlist_items[pair.first].file_size > 0) {
                                        task->playlist_items[pair.first].bitrate = AudioUtils::calculateBitrate(
                                            task->playlist_items[pair.first].file_size,
                                            task->playlist_items[pair.first].duration
                                        );
                                        std::cout << "[DEBUG] Final check: Calculated bitrate for item " << pair.first 
                                                  << " (file_size was 0): " << task->playlist_items[pair.first].bitrate 
                                                  << " kbps (size=" << task->playlist_items[pair.first].file_size 
                                                  << ", duration=" << task->playlist_items[pair.first].duration << ")" << std::endl;
                                    }
                                }
                            }
                        }
                    }
                    
                    // Final check: if all playlist items are downloaded, force status to "completed" and clear error message
                    // This overrides any yt-dlp error code if all files are successfully found
                    int final_downloaded_count = 0;
                    for (const auto& item : task->playlist_items) {
                        if (item.downloaded) {
                            final_downloaded_count++;
                        }
                    }
                    if (final_downloaded_count == task->total_playlist_items && task->total_playlist_items > 0) {
                        // CRITICAL: Don't overwrite "cancelled" status
                        if (task->status != "cancelled") {
                            task->status = "completed";
                            task->error_message.clear();
                        }
#ifdef _WIN32
                        writeConsoleUtf8("[DEBUG] App::complete_cb: All playlist items found (" + std::to_string(final_downloaded_count) + "/" + std::to_string(task->total_playlist_items) + "), forcing task status to 'completed'.\n");
#else
                        std::cout << "[DEBUG] App::complete_cb: All playlist items found (" << final_downloaded_count << "/" << task->total_playlist_items << "), forcing task status to 'completed'." << std::endl;
#endif
                    }
                    
                } else {
                    // Check if this should be a playlist (has playlist_items but is_playlist is false)
                    // IMPORTANT: For SoundCloud, only treat as playlist if more than 1 item
                    if (!task->playlist_items.empty()) {
                        // For SoundCloud, don't treat single items as playlists
                        if (task->platform == "SoundCloud" && task->playlist_items.size() == 1) {
                            task->is_playlist = false;
                            task->total_playlist_items = 0;
                            task->playlist_items.clear();
                        } else {
                            task->is_playlist = true;
                            if (task->total_playlist_items == 0) {
                                task->total_playlist_items = static_cast<int>(task->playlist_items.size());
                            }
                        }
                    }
                }
                
                // For playlists, file_path may be empty - that's OK, items are in playlist_item_file_paths
                if (!file_path.empty()) {
                    task->file_path = file_path;
                    // Extract filename from path
                    size_t last_slash = file_path.find_last_of("/\\");
                    if (last_slash != std::string::npos) {
                        task->filename = file_path.substr(last_slash + 1);
                    } else {
                        task->filename = file_path;
                    }
                    
                    // Always get file size from actual file (not from progress info)
                    // This ensures we get the correct size of the final converted file (e.g., mp3)
                    // Validate path before using
                    if (!ValidationUtils::isValidPath(file_path)) {
                        task->status = "error";
                        task->error_message = "Invalid file path";
                        return;
                    }
                    
                    int64_t file_size = getFileSize(file_path);
                    if (file_size >= 0) {
                        task->file_size = file_size;
                        
                        // For single files: calculate bitrate from actual file size and duration (same approach as for playlists)
                        // This gives more accurate bitrate than JSON abr field (which is source stream bitrate, not final file bitrate)
                        // The final file bitrate may differ due to conversion, re-encoding, or container overhead
                        // IMPORTANT: Use the same calculation method as for playlist items to ensure consistency
                        if (!task->is_playlist) {
                            // file_size is already set above from getFileSize(file_path)
                            
                            // Calculate bitrate from file size and duration (same as processPlaylistItemsMetadata does for playlists)
                            if (task->metadata.bitrate == 0 && task->metadata.duration > 0 && task->file_size > 0) {
                                task->metadata.bitrate = AudioUtils::calculateBitrate(task->file_size, task->metadata.duration);
                                std::cout << "[DEBUG] Calculated bitrate from file for single file (same as playlist): " << task->metadata.bitrate 
                                          << " kbps (size=" << task->file_size << " bytes, duration=" << task->metadata.duration << "s)" << std::endl;
                            } else if (task->metadata.bitrate == 0) {
                                if (task->metadata.duration == 0) {
                                    std::cout << "[DEBUG] WARNING: Cannot calculate bitrate for single file - duration is 0" << std::endl;
                                } else if (task->file_size == 0) {
                                    std::cout << "[DEBUG] WARNING: Cannot calculate bitrate for single file - file_size is 0" << std::endl;
                                }
                            }
                        }
                    
                            // For playlist items, save metadata to the specific item
                            // Get metadata for this specific item from file
                            if (task->is_playlist && task->current_playlist_item >= 0) {
                                int item_idx = task->current_playlist_item;
                                if (item_idx >= 0 && item_idx < static_cast<int>(task->playlist_items.size())) {
                                    int64_t item_file_size = getFileSize(file_path);
                                    if (item_file_size >= 0) {
                                        task->playlist_items[item_idx].file_size = item_file_size;
                                    }
                            
                            // REMOVED: getVideoInfo call for playlist items - metadata already obtained via getPlaylistItems and JSON during download
                            // Use metadata from task if available, otherwise calculate bitrate from file size and duration
                            if (task->metadata.duration > 0 && task->playlist_items[item_idx].duration == 0) {
                                task->playlist_items[item_idx].duration = task->metadata.duration;
                            }
                            if (task->metadata.bitrate > 0 && task->playlist_items[item_idx].bitrate == 0) {
                                task->playlist_items[item_idx].bitrate = task->metadata.bitrate;
                            }
                            
                            // Calculate bitrate from file size and duration if not already set
                            if (task->playlist_items[item_idx].bitrate == 0 && 
                                task->playlist_items[item_idx].duration > 0 && 
                                task->playlist_items[item_idx].file_size > 0) {
                                task->playlist_items[item_idx].bitrate = AudioUtils::calculateBitrate(
                                    task->playlist_items[item_idx].file_size,
                                    task->playlist_items[item_idx].duration
                                );
                                std::cout << "[DEBUG] Calculated bitrate for playlist item " << item_idx 
                                          << " from file size and duration: " << task->playlist_items[item_idx].bitrate 
                                          << " kbps (size=" << task->playlist_items[item_idx].file_size 
                                          << ", duration=" << task->playlist_items[item_idx].duration << "s)" << std::endl;
                            }
                        }
                    }
                    
                    } else if (task->is_playlist) {
                        // For playlists with empty path, use first item's path or playlist name
                        if (!task->playlist_item_file_paths.empty()) {
                            // Use first item's path
                            task->file_path = task->playlist_item_file_paths.begin()->second;
                            size_t last_slash = task->file_path.find_last_of("/\\");
                            if (last_slash != std::string::npos) {
                                task->filename = task->file_path.substr(last_slash + 1);
                            } else {
                                task->filename = task->file_path;
                            }
                        } else if (!task->playlist_name.empty()) {
                            // Fallback to playlist name
                            task->filename = task->playlist_name;
                        }
                    }
                    
                    // For single files: try to get duration from file if not already set from JSON
                    // This is a fallback in case duration wasn't extracted from JSON during download
                    if (!task->is_playlist && !task->file_path.empty() && task->metadata.duration == 0) {
                        // Try to get duration from file metadata using ffprobe or similar
                        // For now, we'll rely on duration from JSON, but this is a placeholder for future enhancement
                        std::cout << "[DEBUG] Duration not set from JSON, will try to get from file metadata if available" << std::endl;
                    }
                    
                    // For single files: calculate bitrate from file size and duration (same approach as for playlists)
                    // This ensures bitrate is calculated from the actual downloaded file, not from JSON
                    // Note: Bitrate should already be recalculated above when file_stat was read,
                    // but this is a fallback in case the file wasn't found earlier
                    if (!task->is_playlist && !task->file_path.empty()) {
                        // Ensure file_size is set from actual file
                        if (task->file_size == 0) {
                            task->file_size = AudioUtils::getFileSize(task->file_path);
                            if (task->file_size > 0) {
                                std::cout << "[DEBUG] Got file_size from file for single file: " << task->file_size << " bytes" << std::endl;
                            }
                        }
                        
                        // Calculate bitrate from file size and duration (same as for playlists)
                        if (task->metadata.bitrate == 0 && task->metadata.duration > 0 && task->file_size > 0) {
                            task->metadata.bitrate = AudioUtils::calculateBitrate(task->file_size, task->metadata.duration);
                            std::cout << "[DEBUG] Calculated bitrate from file for single file: " << task->metadata.bitrate 
                                      << " kbps (size=" << task->file_size << " bytes, duration=" << task->metadata.duration << "s)" << std::endl;
                        } else if (task->metadata.bitrate == 0 && task->metadata.duration == 0) {
                            std::cout << "[DEBUG] WARNING: Cannot calculate bitrate for single file - duration is 0. Duration should have been extracted from JSON during download." << std::endl;
                        } else if (task->metadata.bitrate == 0 && task->file_size == 0) {
                            std::cout << "[DEBUG] WARNING: Cannot calculate bitrate for single file - file_size is 0." << std::endl;
                        }
                    }

                    // REMOVED: enqueueMetadataRefresh call - metadata should already be obtained during download
                    // If metadata is missing after download, it means it's not available, and calling getVideoInfo again won't help
                    // Metadata is already obtained via:
                    // - getVideoInfo in startDownload (for single files)
                    // - getPlaylistItems and JSON during download (for playlists)
                    // - JSON during download (for both)
                    // 
                    // If metadata is still missing, it's likely not available from the source, and repeating the call is wasteful
                }
                
                // Load metadata from file
                loadMetadata(task);
                
                // Add to history asynchronously to avoid blocking GUI
                runBackground([this, task]() {
                    addToHistory(task);
                });
                
            } else {
                // For playlists, check if all items were downloaded successfully
                // If all items have file paths, consider it successful even if yt-dlp returned error code
                bool playlist_success = false;
                if (task->is_playlist && task->total_playlist_items > 0) {
                    int items_with_paths = 0;
                    for (const auto& pair : task->playlist_item_file_paths) {
                        if (!pair.second.empty() && !ValidationUtils::isTemporaryFile(pair.second)) {
                            items_with_paths++;
                        }
                    }
                    // Also check playlist_items for downloaded flag
                    int downloaded_count = 0;
                    for (const auto& item : task->playlist_items) {
                        if (item.downloaded && !item.file_path.empty()) {
                            downloaded_count++;
                        }
                    }
                    // If we have paths for all items or most items are downloaded, consider it successful
                    if (items_with_paths >= task->total_playlist_items || 
                        downloaded_count >= task->total_playlist_items ||
                        (items_with_paths > 0 && downloaded_count == items_with_paths)) {
                        playlist_success = true;
                        std::cout << "[DEBUG] Playlist download successful despite error: " 
                                  << items_with_paths << " items with paths, " 
                                  << downloaded_count << " items marked as downloaded" << std::endl;
                    }
                }
                
                if (playlist_success) {
                    // CRITICAL: Don't overwrite "cancelled" status
                    if (task->status != "cancelled") {
                        task->status = "completed";
                        task->error_message = "";
                    }
                    
                    // Process file paths to ensure all items are marked as downloaded
                    processPlaylistItemFilePaths(task);
                    
                    // Add to history as completed
                    runBackground([this, task]() {
                        addToHistory(task);
                    });
                } else {
                    task->status = "error";
                    task->error_message = error;
                    
                    // Add to history so error task remains visible with Retry button
                    runBackground([this, task]() {
                        addToHistory(task);
                    });
                }
            }
        },
        ytdlp_settings,
        ""  // Empty string means download all playlist items
    );
    
    // Store downloader
    downloaders_.push_back(std::move(downloader));
    active_downloads_++;
}

void App::cancelDownload(DownloadTask* task) {
    // CRITICAL: Validate task pointer before proceeding
    if (!task) {
        std::cerr << "[WARNING] cancelDownload: task is null" << std::endl;
        return;
    }
    
    if (download_manager_) {
        download_manager_->cancelDownload(task);
        return;
    }
    // Fallback to old implementation if download_manager_ is not initialized
    // Make cancellation completely non-blocking by doing it in a separate thread
    runBackground([this, task]() {
        try {
            Downloader* downloader_to_cancel = nullptr;
            bool was_downloading = false;
            
            // Quick lock to get downloader pointer and update status
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                
                // CRITICAL: Re-validate task after acquiring lock (it might have been deleted)
                // Check if task still exists in tasks_ vector
                bool task_exists = false;
                for (const auto& t : tasks_) {
                    if (t.get() == task) {
                        task_exists = true;
                        break;
                    }
                }
                
                if (!task_exists) {
                    std::cout << "[DEBUG] cancelDownload: Task was already deleted" << std::endl;
                    return;
                }
                
                // CRITICAL: Validate task state before accessing
                if (task->status == "cancelled" || task->status == "completed" || task->status == "error") {
                    // Already finished or cancelled
                    std::cout << "[DEBUG] cancelDownload: Task already in final state: " << task->status << std::endl;
                    return;
                }
                
                // Get pointer to downloader and check status before updating
                was_downloading = (task->status == "downloading");
                if (was_downloading && task->downloader_ptr) {
                    downloader_to_cancel = task->downloader_ptr;
                }
                
                // Update status immediately
                task->status = "cancelled";
                task->error_message = "Download cancelled by user";
                // Clear downloader pointer after getting it (will cancel outside lock)
                if (was_downloading && task->downloader_ptr) {
                    task->downloader_ptr = nullptr;
                }
                
                if (was_downloading && active_downloads_ > 0) {
                    active_downloads_--; // Decrement active downloads count
                }
            }
            
            // Cancel downloader outside of lock - this is non-blocking (just sets a flag)
            // CRITICAL: downloader_to_cancel is safe to use here because we only set it
            // if task->downloader_ptr was valid, and we've already cleared task->downloader_ptr
            if (downloader_to_cancel) {
                downloader_to_cancel->cancel();
                std::cout << "[DEBUG] cancelDownload: Cancelled downloader" << std::endl;
            }
            
            // Add cancelled task to history so it's preserved with "cancelled" status
            // This ensures the status is saved correctly and not overwritten later
            runBackground([this, task]() {
                addToHistory(task);
            });
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] cancelDownload: Exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[ERROR] cancelDownload: Unknown exception" << std::endl;
        }
    });
}

void App::clearDownloadList() {
    if (download_manager_) {
        download_manager_->clearDownloadList();
        return;
    }
    // Fallback to old implementation if download_manager_ is not initialized
    bool removed_any = false;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        
        auto it = tasks_.begin();
        while (it != tasks_.end()) {
            std::string status = (*it)->status;
            if (status == "completed" || status == "cancelled" || status == "error" || status == "already_exists") {
                it = tasks_.erase(it);
                removed_any = true;
            } else {
                ++it;
            }
        }
        
        active_downloads_ = 0;
        for (const auto& task : tasks_) {
            if (task->status == "downloading") {
                active_downloads_++;
            }
        }
    }
    // Clear persisted history items and caches as part of clearing the list
    {
        if (history_manager_) {
            history_manager_->clearAll();
        }
        history_urls_.clear();
        persistHistoryItems();
    }
    if (removed_any) {
        rewriteHistoryFromTasks();
    }
}

void App::updateDownloadProgress(DownloadTask* task, const std::string& output) {
    if (download_manager_) {
        download_manager_->updateDownloadProgress(task, output);
        return;
    }
    // Fallback to old implementation if download_manager_ is not initialized
    // Progress is already updated via callback in downloadAsync, so this method can be empty
    // or can be used for additional processing if needed
}

void App::removeTask(size_t index) {
    if (download_manager_) {
        download_manager_->removeTask(index);
        return;
    }
    // Fallback to old implementation if download_manager_ is not initialized
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    if (index >= tasks_.size()) {
        return;
    }
    
    auto& task = tasks_[index];
    
    // If task is downloading, cancel it first
    if (task->status == "downloading") {
        cancelDownload(task.get());
        // Don't wait during shutdown - cancellation will be handled by joinBackgroundThreads()
        if (!shutting_down_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    // Decrement active downloads if needed
    if (task->status == "downloading" && active_downloads_ > 0) {
        active_downloads_--;
    }
    
    // Remove the task
    tasks_.erase(tasks_.begin() + index);
    
}

void App::retryMissingPlaylistItems(DownloadTask* task) {
    if (download_manager_) {
        download_manager_->retryMissingPlaylistItems(task);
        return;
    }
    // Fallback to old implementation if download_manager_ is not initialized
    if (!task->is_playlist || task->total_playlist_items == 0) {
        return;
    }
    
    // Set retry_in_progress for UI feedback
    std::string url = task->url;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (retry_in_progress_.find(url) != retry_in_progress_.end()) {
            return;  // Already retrying
        }
        retry_in_progress_.insert(url);
    }
    std::cout << "[DEBUG] retryMissingPlaylistItems: Started for URL: " << url << std::endl;
    
    // Find missing items
    std::vector<int> missing_indices;  // 0-based indices
    for (int idx = 0; idx < task->total_playlist_items; idx++) {
        bool is_downloaded = false;
        
        // Check if marked as downloaded
        if (idx < static_cast<int>(task->playlist_items.size()) && task->playlist_items[idx].downloaded) {
            is_downloaded = true;
        }
        
        // Check if file exists
        if (!is_downloaded) {
            std::string item_path;
            if (task->playlist_item_file_paths.find(idx) != task->playlist_item_file_paths.end()) {
                item_path = task->playlist_item_file_paths[idx];
            } else {
                // Try to find file by name
                std::string base_dir = settings_->downloads_dir;
                if (settings_->save_playlists_to_separate_folder && !task->playlist_name.empty()) {
                    std::string folder_name = sanitizeFilename(task->playlist_name);
                    base_dir += "/" + folder_name;
                }
                
                std::string display_name;
                if (idx < static_cast<int>(task->playlist_items.size()) && !task->playlist_items[idx].title.empty()) {
                    display_name = task->playlist_items[idx].title;
                } else {
                    display_name = "Item " + std::to_string(idx + 1);
                }
                
                // Use selected format from settings instead of hardcoded .mp3
                std::string selected_format = settings_ ? settings_->selected_format : "mp3";
                std::string file_path = base_dir + "/" + display_name + "." + selected_format;
                if (fileExists(file_path)) {
                    item_path = file_path;
                }
            }
            
            if (!item_path.empty()) {
                if (fileExists(item_path)) {
                    is_downloaded = true;
                }
            }
        }
        
        if (!is_downloaded) {
            missing_indices.push_back(idx);
        }
    }
    
    if (missing_indices.empty()) {
        std::cout << "[DEBUG] retryMissingPlaylistItems: No missing items" << std::endl;
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        retry_in_progress_.erase(url);
        return;
    }
    
    std::cout << "[DEBUG] retryMissingPlaylistItems: Found " << missing_indices.size() << " missing items" << std::endl;
    
    // Build playlist-items string (1-based indices for yt-dlp)
    std::ostringstream items_str;
    for (size_t i = 0; i < missing_indices.size(); i++) {
        if (i > 0) items_str << ",";
        items_str << (missing_indices[i] + 1);  // Convert to 1-based
    }
    
    // Reset status to downloading
    task->status = "downloading";
    task->current_playlist_item = -1;
    
    // Determine output directory
    std::string output_dir = settings_->downloads_dir;
    if (settings_->save_playlists_to_separate_folder && !task->playlist_name.empty()) {
        std::string folder_name = sanitizeFilename(task->playlist_name);
        output_dir = settings_->downloads_dir + "/" + folder_name;
    }
    
    // Create downloader
    auto downloader = std::make_unique<Downloader>();
    Downloader* downloader_ptr = downloader.get();
    task->downloader_ptr = downloader_ptr;
    
    // Create yt-dlp settings
    YtDlpSettings ytdlp_settings = createYtDlpSettings();
    
    // Start download with specific playlist items
    downloader_ptr->downloadAsync(
        task->url,
        output_dir,
        settings_->selected_format,
        settings_->selected_quality,
        settings_->use_proxy ? ValidationUtils::normalizeProxy(settings_->proxy_input) : "",
        std::string(settings_->spotify_api_key),
        std::string(settings_->youtube_api_key),
        std::string(settings_->soundcloud_api_key),
        true,  // download_playlist = true
        [this, task](const Downloader::ProgressInfo& info) {
            // Same progress callback as in startDownload
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            if (task->status == "cancelled") {
                return;
            }
            task->progress = info.progress;
            if (info.status.empty() || info.status == "downloading") {
                task->status = "downloading";
            } else {
                task->status = info.status;
            }
            
            if (info.is_playlist) {
                task->is_playlist = true;
                task->current_playlist_item = info.current_item_index;
                // Set total_playlist_items from info.total_items, but fallback to playlist_items.size() if 0
                if (info.total_items > 0) {
                    task->total_playlist_items = info.total_items;
                } else if (!task->playlist_items.empty() && task->total_playlist_items == 0) {
                    task->total_playlist_items = static_cast<int>(task->playlist_items.size());
                }
                task->current_item_title = info.current_item_title;
                
                if (!info.playlist_name.empty() && task->playlist_name.empty()) {
                    task->playlist_name = info.playlist_name;
                }
            }
        },
        [this, task](const std::string& file_path, const std::string& error) {
            // Same complete callback as in startDownload
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            active_downloads_--;
            
            if (error.empty()) {
                if (task->is_playlist && task->current_playlist_item >= 0 && !file_path.empty()) {
                    int item_idx = task->current_playlist_item;
                    // CRITICAL: Normalize file path for Windows compatibility (use backslashes)
                    std::string normalized_file_path = PathUtils::normalizePath(file_path);
                    
                    // Check if this is an intermediate format that will be converted
                    std::string target_format = settings_->selected_format;
                    if (ValidationUtils::isIntermediateFormat(normalized_file_path, target_format)) {
                        // Try to find the final converted file
                        size_t last_dot = normalized_file_path.find_last_of('.');
                        if (last_dot != std::string::npos) {
                            std::string base_path = normalized_file_path.substr(0, last_dot);
                            std::string final_path = base_path + "." + target_format;
                            if (fileExists(final_path)) {
                                normalized_file_path = final_path;
                            } else {
                                // Don't save intermediate file path
                                normalized_file_path.clear();
                            }
                        }
                    }
                    
                    if (!normalized_file_path.empty() && !ValidationUtils::isTemporaryFile(normalized_file_path)) {
                        task->playlist_item_file_paths[item_idx] = normalized_file_path;
                        if (item_idx >= 0 && item_idx < static_cast<int>(task->playlist_items.size())) {
                            task->playlist_items[item_idx].downloaded = true;
                            task->playlist_items[item_idx].file_path = normalized_file_path;
                        }
                    }
                }
                
                // complete_cb is called when yt-dlp process has finished
                // For playlists, this means all available items have been processed
                // We should NOT check downloaded_count >= total_playlist_items because:
                // 1. yt-dlp may skip unavailable items
                // 2. total_playlist_items may not match actually downloaded items
                // 3. If process finished, the download is complete regardless of item count
                // CRITICAL: Don't overwrite "cancelled" status
                if (task->status != "cancelled") {
                    task->status = "completed";
                }
                
                if (task->is_playlist) {
                    // Count downloaded items for logging only
                    int downloaded_count = 0;
                    for (const auto& item : task->playlist_items) {
                        if (item.downloaded) {
                            downloaded_count++;
                        }
                    }
                    
                    // Determine output directory
                    std::string output_dir = settings_->downloads_dir;
                    if (settings_->save_playlists_to_separate_folder && !task->playlist_name.empty()) {
                        std::string folder_name = sanitizeFilename(task->playlist_name);
                        output_dir = settings_->downloads_dir + "/" + folder_name;
                    }
                    
                    // Fill file_path and file_size for all playlist items
                    // First, use paths from playlist_item_file_paths
                    downloaded_count += processPlaylistItemFilePaths(task);
                    
                    // For items without file_path, try to find files in output directory
                    // This handles cases where yt-dlp didn't provide file_path for each item
                    for (size_t i = 0; i < task->playlist_items.size(); i++) {
                        if (task->playlist_items[i].file_path.empty() && task->playlist_items[i].downloaded) {
                            // Try to find file by matching title or index
                            // This is a fallback - ideally file_path should be set during download
                            std::string search_title = sanitizeFilename(task->playlist_items[i].title);
                            
#ifdef _WIN32
                            // Use Unicode version (FindFirstFileW) for proper support of Russian and other non-ASCII filenames
                            int dir_size_needed = MultiByteToWideChar(CP_UTF8, 0, output_dir.c_str(), -1, NULL, 0);
                            if (dir_size_needed > 0) {
                                std::vector<wchar_t> dir_wide(dir_size_needed);
                                MultiByteToWideChar(CP_UTF8, 0, output_dir.c_str(), -1, dir_wide.data(), dir_size_needed);
                                std::wstring search_pattern = std::wstring(dir_wide.data()) + L"\\*";
                                
                                WIN32_FIND_DATAW find_data;
                                HANDLE find_handle = FindFirstFileW(search_pattern.c_str(), &find_data);
                                if (find_handle != INVALID_HANDLE_VALUE) {
                                    do {
                                        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                            // Convert filename from wide string to UTF-8
                                            int filename_size = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, NULL, 0, NULL, NULL);
                                            if (filename_size > 0) {
                                                std::vector<char> filename_utf8(filename_size);
                                                WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, filename_utf8.data(), filename_size, NULL, NULL);
                                                std::string file_name_utf8(filename_utf8.data());
                                                std::string file_path = PathUtils::joinPath(output_dir, file_name_utf8);
                                                
                                                // Check if filename contains title (case-insensitive)
                                                std::string file_name_lower = file_name_utf8;
                                                std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);
                                                std::string search_title_lower = search_title;
                                                std::transform(search_title_lower.begin(), search_title_lower.end(), search_title_lower.begin(), ::tolower);
                                                
                                                if (file_name_lower.find(search_title_lower) != std::string::npos) {
                                                    // Check if this file is not already assigned to another item
                                                    bool already_assigned = false;
                                                    for (size_t j = 0; j < task->playlist_items.size(); j++) {
                                                        if (j != i && task->playlist_items[j].file_path == file_path) {
                                                            already_assigned = true;
                                                            break;
                                                        }
                                                    }
                                                    if (!already_assigned) {
                                                        task->playlist_items[i].file_path = file_path;
                                                        task->playlist_item_file_paths[static_cast<int>(i)] = file_path;
                                                        int64_t file_size = getFileSize(file_path);
                                                        if (file_size >= 0) {
                                                            task->playlist_items[i].file_size = file_size;
                                                        }
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    } while (FindNextFileW(find_handle, &find_data));
                                    FindClose(find_handle);
                                }
                            }
#else
                            DIR* dir = opendir(output_dir.c_str());
                            if (dir != nullptr) {
                                struct dirent* entry;
                                while ((entry = readdir(dir)) != nullptr) {
                                    if (entry->d_name[0] == '.') continue;
                                    
                                    std::string file_path = output_dir + "/" + entry->d_name;
                                    if (isRegularFile(file_path)) {
                                        std::string file_name = entry->d_name;
                                        // Check if filename contains title (case-insensitive)
                                        std::string file_name_lower = file_name;
                                        std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);
                                        std::string search_title_lower = search_title;
                                        std::transform(search_title_lower.begin(), search_title_lower.end(), search_title_lower.begin(), ::tolower);
                                        
                                        if (file_name_lower.find(search_title_lower) != std::string::npos) {
                                            // Check if this file is not already assigned to another item
                                            bool already_assigned = false;
                                            for (size_t j = 0; j < task->playlist_items.size(); j++) {
                                                if (j != i && task->playlist_items[j].file_path == file_path) {
                                                    already_assigned = true;
                                                    break;
                                                }
                                            }
                                            if (!already_assigned) {
                                                task->playlist_items[i].file_path = file_path;
                                                task->playlist_item_file_paths[static_cast<int>(i)] = file_path;
                                                int64_t item_file_size = getFileSize(file_path);
                                                if (item_file_size >= 0) {
                                                    task->playlist_items[i].file_size = item_file_size;
                                                }
                                                
                                                // Calculate bitrate if duration is available
                                                if (task->playlist_items[i].bitrate == 0 && 
                                                    task->playlist_items[i].duration > 0 && 
                                                    task->playlist_items[i].file_size > 0) {
                                                    task->playlist_items[i].bitrate = AudioUtils::calculateBitrate(
                                                        task->playlist_items[i].file_size,
                                                        task->playlist_items[i].duration
                                                    );
                                                }
                                                break;
                                            }
                                        }
                                    }
                                }
                                closedir(dir);
                            }
#endif
                        }
                    }
                    
                    // Final pass: calculate bitrate for all items that have file_size and duration but no bitrate
                    for (size_t i = 0; i < task->playlist_items.size(); i++) {
                        if (task->playlist_items[i].bitrate == 0 && 
                            task->playlist_items[i].duration > 0 && 
                            task->playlist_items[i].file_size > 0) {
                            task->playlist_items[i].bitrate = AudioUtils::calculateBitrate(
                                task->playlist_items[i].file_size,
                                task->playlist_items[i].duration
                            );
                        }
                    }
                    
                }
                
                // Save metadata for playlist item
                if (task->is_playlist && task->current_playlist_item >= 0) {
                    // Validate file path before using
                    if (!ValidationUtils::isValidPath(file_path)) {
                    } else {
                        int64_t file_size = getFileSize(file_path);
                        if (file_size >= 0) {
                            int item_idx = task->current_playlist_item;
                            if (item_idx >= 0 && item_idx < static_cast<int>(task->playlist_items.size())) {
                                task->playlist_items[item_idx].file_size = file_size;
                                task->playlist_items[item_idx].duration = task->metadata.duration;
                                task->playlist_items[item_idx].bitrate = task->metadata.bitrate;
                                
                                if (task->playlist_items[item_idx].bitrate == 0 && 
                                    task->playlist_items[item_idx].duration > 0 && 
                                    task->playlist_items[item_idx].file_size > 0) {
                                    task->playlist_items[item_idx].bitrate = AudioUtils::calculateBitrate(
                                        task->playlist_items[item_idx].file_size,
                                        task->playlist_items[item_idx].duration
                                    );
                                }
                            }
                        } else {
                        }
                    }
                }
            } else {
                task->status = "error";
                task->error_message = error;
                
                // Add to history so error task remains visible with Retry button
                runBackground([this, task]() {
                    addToHistory(task);
                });
            }
        },
        ytdlp_settings,
        items_str.str()  // Pass playlist items string
    );
    
    downloaders_.push_back(std::move(downloader));
    active_downloads_++;
    
    // Clear retry state - download has started
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        retry_in_progress_.erase(url);
    }
    std::cout << "[DEBUG] retryMissingPlaylistItems: Download started, retry state cleared" << std::endl;
}

void App::retryMissingFromHistory(const std::string& url) {
    if (url.empty()) {
        return;
    }
    
    // Check if already retrying this URL
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (retry_in_progress_.find(url) != retry_in_progress_.end()) {
            return;  // Already retrying
        }
        retry_in_progress_.insert(url);
    }
    
    // Run retry logic in background thread so UI can show spinner
    std::string url_copy = url;  // Copy for lambda capture
    runBackground([this, url_copy]() {
        std::cout << "[DEBUG] retryMissingFromHistory: Starting retry for URL: " << url_copy << std::endl;
        
        // Get playlist info first
        Downloader::PlaylistInfo playlist_info = Downloader::getPlaylistItems(
            url_copy,
            settings_->use_proxy ? ValidationUtils::normalizeProxy(settings_->proxy_input) : "",
            createYtDlpSettings()
        );
        
        // Check for errors from getPlaylistItems (e.g., bot detection)
        if (!playlist_info.error_message.empty()) {
            std::cout << "[DEBUG] retryMissingFromHistory: getPlaylistItems returned error: " << playlist_info.error_message << std::endl;
            // Create an error task so user can see the error in UI
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            auto task = std::make_unique<DownloadTask>(url_copy);
            task->status = "error";
            task->error_message = playlist_info.error_message;
            task->is_playlist = true;
            tasks_.push_back(std::move(task));
            retry_in_progress_.erase(url_copy);
            return;
        }
        
        if (playlist_info.items.empty()) {
            std::cout << "[DEBUG] retryMissingFromHistory: Failed to get playlist items" << std::endl;
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            retry_in_progress_.erase(url_copy);
            return;
        }
        
        std::string playlist_name = playlist_info.playlist_name;
        std::string thumbnail_url = playlist_info.thumbnail_url;
        
        // Convert PlaylistItemInfo to PlaylistItem
        std::vector<PlaylistItem> playlist_items;
        for (const auto& info : playlist_info.items) {
            PlaylistItem item;
            item.title = info.title;
            item.id = info.id;
            item.duration = info.duration;
            item.index = info.index;
            playlist_items.push_back(item);
        }
        
        std::cout << "[DEBUG] retryMissingFromHistory: Got " << playlist_items.size() << " items, playlist: " << playlist_name << std::endl;
        
        // CRITICAL: Save existing file data from history BEFORE deleting it
        // This preserves information about already downloaded files
        std::map<int, PlaylistItem> existing_files_map;
        if (history_manager_) {
            const auto& history_items = getHistoryItems();
            for (const auto& h : history_items) {
                if (h.url == url_copy && h.is_playlist) {
                    // Found the old history item - save file data from each playlist item
                    for (const auto& old_item : h.playlist_items) {
                        if (old_item.downloaded && !old_item.file_path.empty()) {
                            existing_files_map[old_item.index] = old_item;
                            std::cout << "[DEBUG] retryMissingFromHistory: Saved existing file data for item " << old_item.index 
                                      << ": " << old_item.file_path << " (" << old_item.file_size << " bytes)" << std::endl;
                        }
                    }
                    break;
                }
            }
        }
        std::cout << "[DEBUG] retryMissingFromHistory: Preserved " << existing_files_map.size() << " existing files from history" << std::endl;
        
        // Restore file data to new playlist_items
        for (size_t i = 0; i < playlist_items.size(); i++) {
            auto it = existing_files_map.find(static_cast<int>(i));
            if (it != existing_files_map.end()) {
                // Copy file data from old history item
                playlist_items[i].file_path = it->second.file_path;
                playlist_items[i].file_size = it->second.file_size;
                playlist_items[i].filename = it->second.filename;
                playlist_items[i].bitrate = it->second.bitrate;
                playlist_items[i].downloaded = true;
                std::cout << "[DEBUG] retryMissingFromHistory: Restored file data for item " << i << ": " << it->second.file_path << std::endl;
            }
        }
        
        // Determine output directory
        std::string output_dir = settings_->downloads_dir;
        if (settings_->save_playlists_to_separate_folder && !playlist_name.empty()) {
            std::string folder_name = sanitizeFilename(playlist_name);
            output_dir = settings_->downloads_dir + "/" + folder_name;
        }
        
        // Check which items are missing
        std::vector<int> missing_indices;
        std::string selected_format = settings_ ? settings_->selected_format : "mp3";
        
        for (size_t i = 0; i < playlist_items.size(); i++) {
            std::string sanitized_title = sanitizeFilename(playlist_items[i].title);
            std::string expected_path = output_dir + "/" + sanitized_title + "." + selected_format;
            
            if (!fileExists(expected_path)) {
                missing_indices.push_back(static_cast<int>(i));
                std::cout << "[DEBUG] retryMissingFromHistory: Item " << i << " missing: " << sanitized_title << std::endl;
            }
        }
        
        if (missing_indices.empty()) {
            std::cout << "[DEBUG] retryMissingFromHistory: No missing items found" << std::endl;
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            retry_in_progress_.erase(url_copy);
            return;
        }
        
        std::cout << "[DEBUG] retryMissingFromHistory: Found " << missing_indices.size() << " missing items" << std::endl;
        
        // Build playlist-items string (1-based indices for yt-dlp)
        std::ostringstream items_str;
        for (size_t i = 0; i < missing_indices.size(); i++) {
            if (i > 0) items_str << ",";
            items_str << (missing_indices[i] + 1);  // Convert to 1-based
        }
        
        // Remove old history item and persist changes
        std::cout << "[DEBUG] retryMissingFromHistory: Deleting old history item..." << std::endl;
        deleteUrlFromHistory(url_copy);
        if (history_manager_) {
            history_manager_->removeDeletedUrl(url_copy);
            history_manager_->persistHistoryItems();  // Save to file immediately
        }
        
        // Remove OLD task with same URL from tasks_ to avoid duplicates
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            history_urls_.erase(url_copy);
            
            // Find and remove old task with same URL
            tasks_.erase(
                std::remove_if(tasks_.begin(), tasks_.end(),
                    [&url_copy](const std::unique_ptr<DownloadTask>& t) {
                        return t && t->url == url_copy;
                    }),
                tasks_.end()
            );
            std::cout << "[DEBUG] retryMissingFromHistory: Removed old task and URL from history_urls_" << std::endl;
        }
        
        // Create a new task
        DownloadTask* task_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            auto task = std::make_unique<DownloadTask>(url_copy);
            detectPlatform(url_copy, task->platform);
            task->is_playlist = true;
            task->playlist_name = playlist_name;
            task->total_playlist_items = static_cast<int>(playlist_items.size());
            task->playlist_items = playlist_items;
            task->thumbnail_url = thumbnail_url;
            task->status = "downloading";
            task_ptr = task.get();
            tasks_.push_back(std::move(task));
            std::cout << "[DEBUG] retryMissingFromHistory: Created new task with status='downloading', tasks_.size()=" << tasks_.size() << std::endl;
        }
        
        // Create downloader and start download
        auto dl = std::make_unique<Downloader>();
        Downloader* dl_ptr = dl.get();
        task_ptr->downloader_ptr = dl_ptr;
        
        YtDlpSettings ytdlp_settings = createYtDlpSettings();
        
        std::cout << "[DEBUG] retryMissingFromHistory: Starting download for items: " << items_str.str() << std::endl;
        
        dl_ptr->downloadAsync(
            url_copy,
            output_dir,
            settings_->selected_format,
            settings_->selected_quality,
            settings_->use_proxy ? ValidationUtils::normalizeProxy(settings_->proxy_input) : "",
            std::string(settings_->spotify_api_key),
            std::string(settings_->youtube_api_key),
            std::string(settings_->soundcloud_api_key),
            true,  // download_playlist = true
            [this, task_ptr](const Downloader::ProgressInfo& info) {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                if (task_ptr->status == "cancelled") return;
                task_ptr->progress = info.progress;
                if (info.current_item_index >= 0) {
                    task_ptr->current_playlist_item = info.current_item_index;
                }
                if (!info.current_item_title.empty()) {
                    task_ptr->current_item_title = info.current_item_title;
                }
            },
            [this, task_ptr](const std::string& file_path, const std::string& error) {
                {
                    std::lock_guard<std::mutex> lock(tasks_mutex_);
                    active_downloads_--;
                    
                    if (error.empty() && task_ptr->status != "cancelled") {
                        task_ptr->status = "completed";
                        task_ptr->progress = 1.0f;
                    } else if (!error.empty() && task_ptr->status != "cancelled") {
                        task_ptr->status = "error";
                        task_ptr->error_message = error;
                    }
                }
                
                // Add to history AFTER releasing the lock to avoid deadlock
                // (addToHistory will acquire tasks_mutex_ internally)
                addToHistory(task_ptr);
            },
            ytdlp_settings,
            items_str.str()  // Pass playlist items string
        );
        
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            downloaders_.push_back(std::move(dl));
            active_downloads_++;
            retry_in_progress_.erase(url_copy);
        }
        
        std::cout << "[DEBUG] retryMissingFromHistory: Download started" << std::endl;
    });
}

bool App::isRetryInProgress(const std::string& url) const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    return retry_in_progress_.find(url) != retry_in_progress_.end();
}

void App::clearRetryInProgress(const std::string& url) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    retry_in_progress_.erase(url);
}

void App::detectPlatform(const std::string& url, std::string& platform) {
    std::string url_lower = url;
    std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);
    
    if (url_lower.find("youtube.com") != std::string::npos || 
        url_lower.find("youtu.be") != std::string::npos) {
        platform = "YouTube";
    } else if (url_lower.find("soundcloud.com") != std::string::npos) {
        platform = "SoundCloud";
    } else if (url_lower.find("spotify.com") != std::string::npos) {
        platform = "Spotify";
    } else if (url_lower.find("tiktok.com") != std::string::npos) {
        platform = "TikTok";
    } else if (url_lower.find("instagram.com") != std::string::npos) {
        platform = "Instagram";
    } else {
        platform = "Unknown";
    }
    
}

void App::openDownloadsFolder() {
    if (file_manager_) {
        file_manager_->openDownloadsFolder(settings_->downloads_dir);
    }
}

void App::openFileLocation(const std::string& file_path) {
    if (file_manager_) {
        file_manager_->openFileLocation(file_path);
    }
}

void App::startFileDrag(const std::string& file_path) {
    if (file_manager_) {
        file_manager_->startFileDrag(window_manager_ ? window_manager_->getWindow() : nullptr, file_path);
    }
}

void App::selectDownloadsFolder() {
    if (file_manager_) {
        file_manager_->selectDownloadsFolder(window_manager_ ? window_manager_->getWindow() : nullptr, [this](const std::string& new_dir) {
            // Update on main thread
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                settings_->downloads_dir = new_dir;
            }
            saveSettings(); // Save the new directory
        });
    }
}

void App::checkServiceAvailability(bool force_check, bool is_startup) {
    // Delegate to ServiceChecker (eliminates code duplication)
    if (service_checker_) {
        service_checker_->checkAvailability(force_check, is_startup);
    }
}

void App::updateYtDlp() {
    if (ytdlp_update_in_progress_) {
        return;
    }
    
    ytdlp_update_in_progress_ = true;
    ytdlp_update_status_ = "Updating yt-dlp... This may take a moment.";
    
    runBackground([this]() {
        if (shutting_down_) {
            ytdlp_update_in_progress_ = false;
            return;
        }
        
        std::string log_output;
        bool ok = Downloader::updateYtDlp(log_output);
        
        if (shutting_down_) {
            ytdlp_update_in_progress_ = false;
            return;
        }
        
        // Update status message on main state (no heavy work here)
        if (ok) {
            ytdlp_update_status_ = "yt-dlp updated successfully.";
            // Check and save new version after successful update
            std::string new_version = Downloader::getYtDlpVersion();
            if (!new_version.empty() && new_version != "Unknown") {
                settings_->ytdlp_version = new_version;
                settings_->ytdlp_version_present = true;
                saveSettings();  // Save updated version to config file
            }
        } else {
            if (log_output.empty()) {
                ytdlp_update_status_ = "yt-dlp update failed.";
            } else {
                ytdlp_update_status_ = "yt-dlp update failed:\n" + log_output;
            }
        }
        ytdlp_update_in_progress_ = false;
    });
}

// formatFileSize moved to AudioUtils::formatFileSize

void App::processPlaylistItemsMetadata(std::vector<PlaylistItem>& playlist_items) {
    // Get selected format from settings to determine if extension is final or temporary
    std::string selected_format = settings_ ? settings_->selected_format : "mp3";
    
    // Determine playlist directory if needed (for finding files without file_path)
    std::string playlist_dir;
    bool playlist_dir_determined = false;
    
    for (size_t i = 0; i < playlist_items.size(); i++) {
        auto& item = playlist_items[i];
        
        // First, check if file_path is set and file exists
        if (!item.file_path.empty()) {
            // Extract filename from file_path only if it's the final converted file
            // Check if extension matches selected format (if opus is selected, .opus is final)
            if (item.filename.empty()) {
                std::string file_path = item.file_path;
                size_t last_dot = file_path.find_last_of('.');
                if (last_dot != std::string::npos) {
                    std::string ext = file_path.substr(last_dot);
                    // Check if extension matches selected format (final format)
                    bool is_final_format = (ext == "." + selected_format);
                    // Also allow common final formats even if not selected (mp3, flac, etc.)
                    if (!is_final_format) {
                        is_final_format = (ext == ".mp3" || ext == ".flac" || ext == ".m4a" || ext == ".ogg");
                    }
                    // If extension is temporary (not final), don't set filename yet
                    // Temporary formats: .opus, .webm, or any supported format that doesn't match selected format
                    // A file is temporary if it has a different extension than the selected format
                    // and it's one of the supported formats (mp3, m4a, flac, ogg)
                    bool is_temporary = (!is_final_format && 
                                         ((ext == ".opus" || ext == ".webm") ||
                                          ((ext == ".mp3" || ext == ".m4a" || ext == ".flac" || ext == ".ogg") &&
                                           ext != "." + selected_format)));
                    
                    if (!is_temporary) {
                        // Extension is final format, set filename
                        size_t last_slash = file_path.find_last_of("/\\");
                        if (last_slash != std::string::npos) {
                            item.filename = file_path.substr(last_slash + 1);
                        } else {
                            item.filename = file_path;
                        }
                    }
                    // If extension is temporary, filename will be set later when file_path is updated to final file
                }
            }
            
            if (fileExists(item.file_path)) {
                item.downloaded = true;
                if (item.file_size == 0) {
                    int64_t file_size = getFileSize(item.file_path);
                    if (file_size >= 0) {
                        item.file_size = file_size;
                        std::cout << "[DEBUG] processPlaylistItemsMetadata: Set file_size for item " << i 
                                  << " from file_path: " << item.file_size << " bytes" << std::endl;
                    }
                }
            }
        }
        // If file_size is still 0, try to get it from file_path
        if (item.file_size == 0 && !item.file_path.empty()) {
            item.file_size = AudioUtils::getFileSize(item.file_path);
            if (item.file_size > 0) {
                std::cout << "[DEBUG] processPlaylistItemsMetadata: Got file_size for item " << i 
                          << " via AudioUtils::getFileSize: " << item.file_size << " bytes" << std::endl;
            }
        }
        
        // If file_path is empty but we have duration, try to find file in playlist directory
        // This handles cases where file_path wasn't set during download (e.g., for items 6-11)
        if (item.file_path.empty() && item.duration > 0 && !playlist_dir_determined) {
            // Try to determine playlist directory from first item that has file_path
            for (size_t j = 0; j < playlist_items.size(); j++) {
                if (!playlist_items[j].file_path.empty()) {
                    size_t last_slash = playlist_items[j].file_path.find_last_of("/\\");
                    if (last_slash != std::string::npos) {
                        playlist_dir = playlist_items[j].file_path.substr(0, last_slash);
                        playlist_dir_determined = true;
                        std::cout << "[DEBUG] processPlaylistItemsMetadata: Determined playlist directory: " << playlist_dir << std::endl;
                        break;
                    }
                }
            }
            // If still not determined, try to get from settings
            if (!playlist_dir_determined) {
                playlist_dir = settings_->downloads_dir;
                // Note: We don't have playlist_name here, so we can't add subfolder
                // But we can try to find files in downloads_dir
                playlist_dir_determined = true;
            }
        }
        
        // If file_path is empty, try to find file in playlist directory by matching title
        if (item.file_path.empty() && item.duration > 0 && playlist_dir_determined) {
            std::string search_title = sanitizeFilename(item.title);
            
#ifdef _WIN32
            // Use Unicode version for proper support of non-ASCII filenames
            int dir_size = MultiByteToWideChar(CP_UTF8, 0, playlist_dir.c_str(), -1, NULL, 0);
            if (dir_size > 0) {
                std::wstring wide_dir(dir_size, 0);
                MultiByteToWideChar(CP_UTF8, 0, playlist_dir.c_str(), -1, &wide_dir[0], dir_size);
                wide_dir.resize(dir_size - 1);
                std::wstring search_pattern = wide_dir + L"\\*";
                
                WIN32_FIND_DATAW find_data;
                HANDLE find_handle = FindFirstFileW(search_pattern.c_str(), &find_data);
                if (find_handle != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            int filename_size = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, NULL, 0, NULL, NULL);
                            if (filename_size > 0) {
                                std::vector<char> filename_utf8(filename_size);
                                WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, filename_utf8.data(), filename_size, NULL, NULL);
                                std::string file_name_utf8(filename_utf8.data());
                                std::string file_path = PathUtils::joinPath(playlist_dir, file_name_utf8);
                                
                                // Check if filename contains title (case-insensitive)
                                std::string file_name_lower = file_name_utf8;
                                std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);
                                std::string search_title_lower = search_title;
                                std::transform(search_title_lower.begin(), search_title_lower.end(), search_title_lower.begin(), ::tolower);
                                
                                if (file_name_lower.find(search_title_lower) != std::string::npos) {
                                    // Check if this file is not already assigned to another item
                                    bool already_assigned = false;
                                    for (size_t j = 0; j < playlist_items.size(); j++) {
                                        if (j != i && playlist_items[j].file_path == file_path) {
                                            already_assigned = true;
                                            break;
                                        }
                                    }
                                    if (!already_assigned) {
                                        item.file_path = file_path;
                                        // Extract filename from file_path (same as for single files)
                                        item.filename = file_name_utf8;
                                        std::wstring wide_path = wide_dir + L"\\" + find_data.cFileName;
                                        // Convert wide path to UTF-8 for helper function
                                        int path_size = WideCharToMultiByte(CP_UTF8, 0, wide_path.c_str(), -1, NULL, 0, NULL, NULL);
                                        if (path_size > 0) {
                                            std::string file_path_utf8(path_size, 0);
                                            WideCharToMultiByte(CP_UTF8, 0, wide_path.c_str(), -1, &file_path_utf8[0], path_size, NULL, NULL);
                                            file_path_utf8.resize(path_size - 1);
                                            int64_t file_size = getFileSize(file_path_utf8);
                                            if (file_size >= 0) {
                                                item.file_size = file_size;
                                                item.downloaded = true;
                                                std::cout << "[DEBUG] processPlaylistItemsMetadata: Found file for item " << i 
                                                          << " by title match: " << file_path << " (size=" << item.file_size << " bytes, filename=" << item.filename << ")" << std::endl;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } while (FindNextFileW(find_handle, &find_data));
                    FindClose(find_handle);
                }
            }
#else
            DIR* dir = opendir(playlist_dir.c_str());
            if (dir != nullptr) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    if (entry->d_name[0] == '.') continue;
                    
                    std::string file_path = playlist_dir + "/" + entry->d_name;
                    if (isRegularFile(file_path)) {
                        std::string file_name = entry->d_name;
                        // Check if filename contains title (case-insensitive)
                        std::string file_name_lower = file_name;
                        std::transform(file_name_lower.begin(), file_name_lower.end(), file_name_lower.begin(), ::tolower);
                        std::string search_title_lower = search_title;
                        std::transform(search_title_lower.begin(), search_title_lower.end(), search_title_lower.begin(), ::tolower);
                        
                        if (file_name_lower.find(search_title_lower) != std::string::npos) {
                            // Check if this file is not already assigned to another item
                            bool already_assigned = false;
                            for (size_t j = 0; j < playlist_items.size(); j++) {
                                if (j != i && playlist_items[j].file_path == file_path) {
                                    already_assigned = true;
                                    break;
                                }
                            }
                            if (!already_assigned) {
                                item.file_path = file_path;
                                // Extract filename from file_path (same as for single files)
                                size_t last_slash = file_path.find_last_of("/\\");
                                if (last_slash != std::string::npos) {
                                    item.filename = file_path.substr(last_slash + 1);
                                } else {
                                    item.filename = file_path;
                                }
                                int64_t item_file_size = getFileSize(file_path);
                                if (item_file_size >= 0) {
                                    item.file_size = item_file_size;
                                    item.downloaded = true;
                                }
                                std::cout << "[DEBUG] processPlaylistItemsMetadata: Found file for item " << i 
                                          << " by title match: " << file_path << " (size=" << item.file_size << " bytes, filename=" << item.filename << ")" << std::endl;
                                break;
                            }
                        }
                    }
                }
                closedir(dir);
            }
#endif
        }
        
        // Calculate bitrate if we have file_size and duration but no bitrate
        if (item.bitrate == 0 && item.duration > 0 && item.file_size > 0) {
            item.bitrate = AudioUtils::calculateBitrate(item.file_size, item.duration);
            std::cout << "[DEBUG] processPlaylistItemsMetadata: Calculated bitrate for item " << i 
                      << ": " << item.bitrate << " kbps (size=" << item.file_size 
                      << ", duration=" << item.duration << ")" << std::endl;
        } else if (item.bitrate == 0 && (item.duration == 0 || item.file_size == 0)) {
            std::cout << "[DEBUG] processPlaylistItemsMetadata: Cannot calculate bitrate for item " << i 
                      << " - duration=" << item.duration << ", file_size=" << item.file_size 
                      << ", file_path=" << (item.file_path.empty() ? "EMPTY" : item.file_path) << std::endl;
        }
    }
}

int App::processPlaylistItemFilePaths(DownloadTask* task) {
    if (!task || !task->is_playlist) return 0;
    
    int newly_downloaded = 0;
#ifdef _WIN32
    std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Processing " + std::to_string(task->playlist_item_file_paths.size()) 
                          + " file paths for playlist with " + std::to_string(task->playlist_items.size()) + " items";
    writeConsoleUtf8(debug_msg + "\n");
#else
    std::cout << "[DEBUG] processPlaylistItemFilePaths: Processing " << task->playlist_item_file_paths.size() 
              << " file paths for playlist with " << task->playlist_items.size() << " items" << std::endl;
#endif
    
    for (const auto& pair : task->playlist_item_file_paths) {
        if (pair.first >= 0 && pair.first < static_cast<int>(task->playlist_items.size())) {
#ifdef _WIN32
            std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Processing item " + std::to_string(pair.first) 
                                  + ", file_path=" + (pair.second.empty() ? "EMPTY" : pair.second);
            writeConsoleUtf8(debug_msg + "\n");
#else
            std::cout << "[DEBUG] processPlaylistItemFilePaths: Processing item " << pair.first 
                      << ", file_path=" << (pair.second.empty() ? "EMPTY" : pair.second) << std::endl;
#endif
            
            // CRITICAL: Skip temporary files (.part, .f*.part, etc.) to prevent them from being marked as downloaded
            if (!pair.second.empty() && ValidationUtils::isTemporaryFile(pair.second)) {
#ifdef _WIN32
                std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Skipping temporary file for item " + std::to_string(pair.first) 
                                      + ": " + pair.second;
                writeConsoleUtf8(debug_msg + "\n");
#else
                std::cout << "[DEBUG] processPlaylistItemFilePaths: Skipping temporary file for item " << pair.first 
                          << ": " << pair.second << std::endl;
#endif
                continue;
            }
            
            // Don't set downloaded here - wait until we verify the file exists
            // downloaded will be set in the file checking section below if file is found
            // CRITICAL: Set file_path for this item FIRST if not already set
            // This must be done before checking file_size, as file_path is needed for stat()
            if (task->playlist_items[pair.first].file_path.empty() && !pair.second.empty()) {
                task->playlist_items[pair.first].file_path = pair.second;
                // DON'T set filename here - it may have wrong extension (.opus) before conversion
                // filename will be set after conversion when file_path is updated to final file (.mp3)
                // This allows UI to show title during download and filename after conversion
#ifdef _WIN32
                std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Set file_path for item " + std::to_string(pair.first) 
                                      + ": " + pair.second + ", filename: " + task->playlist_items[pair.first].filename;
                writeConsoleUtf8(debug_msg + "\n");
#else
                std::cout << "[DEBUG] processPlaylistItemFilePaths: Set file_path for item " << pair.first 
                          << ": " << pair.second << ", filename: " << task->playlist_items[pair.first].filename << std::endl;
#endif
            } else if (!task->playlist_items[pair.first].file_path.empty() && task->playlist_items[pair.first].filename.empty()) {
                // Only extract filename if file_path points to final converted file
                // Get selected format from settings to determine if extension is final
                std::string selected_format = settings_ ? settings_->selected_format : "mp3";
                std::string file_path = task->playlist_items[pair.first].file_path;
                size_t last_dot = file_path.find_last_of('.');
                if (last_dot != std::string::npos) {
                    std::string ext = file_path.substr(last_dot);
                    // Check if extension matches selected format (final format)
                    bool is_final_format = (ext == "." + selected_format);
                    // Also allow common final formats even if not selected (mp3, flac, etc.)
                    if (!is_final_format) {
                        is_final_format = (ext == ".mp3" || ext == ".flac" || ext == ".m4a" || ext == ".ogg");
                    }
                    // If extension is temporary (not final), don't set filename yet
                    bool is_temporary = (!is_final_format && (ext == ".opus" || ext == ".webm" || 
                                                               (ext == ".m4a" && selected_format != "m4a") ||
                                                               (ext == ".ogg" && selected_format != "ogg")));
                    
                    if (!is_temporary) {
                        // Extension is final format, set filename
                        size_t last_slash = file_path.find_last_of("/\\");
                        if (last_slash != std::string::npos) {
                            task->playlist_items[pair.first].filename = file_path.substr(last_slash + 1);
                        } else {
                            task->playlist_items[pair.first].filename = file_path;
                        }
                    }
                    // If extension is temporary, filename will be set later when file_path is updated to final file
                }
            }
            // Fill file_size for this item if not already set
            // Use file_path from playlist_items if available, otherwise use pair.second
            std::string file_path_to_check = task->playlist_items[pair.first].file_path.empty() ? pair.second : task->playlist_items[pair.first].file_path;
            std::string actual_file_path = file_path_to_check;
            bool found = false;
            int64_t file_size = -1;
            int64_t file_mtime = -1;
            if (task->playlist_items[pair.first].file_size == 0 && !file_path_to_check.empty()) {
                
                // Try original path first
                if (getFileMetadata(file_path_to_check, file_size, file_mtime)) {
                    actual_file_path = file_path_to_check;
                    found = true;
                }
                
                if (!found) {
                    // If original path doesn't exist, try with different extensions
                    // This handles cases where yt-dlp downloaded as .opus but file was saved as .mp3
                    std::string base_path = file_path_to_check;
                    size_t last_dot = base_path.find_last_of('.');
                    if (last_dot != std::string::npos) {
                        base_path = base_path.substr(0, last_dot);
                    }
                    
                    // Try common audio extensions
                    const char* extensions[] = {".mp3", ".m4a", ".opus", ".ogg", ".flac"};
                    for (const char* ext : extensions) {
                        std::string test_path = base_path + ext;
                        int64_t test_file_size = -1;
                        int64_t test_file_mtime = -1;
                        if (getFileMetadata(test_path, test_file_size, test_file_mtime)) {
                            file_size = test_file_size;
                            file_mtime = test_file_mtime;
                            actual_file_path = test_path;
                            found = true;
#ifdef _WIN32
                            std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Found file with different extension for item " 
                                                  + std::to_string(pair.first) + ": " + actual_file_path + " (original was " + pair.second + ")";
                            writeConsoleUtf8(debug_msg + "\n");
#else
                            std::cout << "[DEBUG] processPlaylistItemFilePaths: Found file with different extension for item " 
                                      << pair.first << ": " << actual_file_path << " (original was " << pair.second << ")" << std::endl;
#endif
                            break;
                        }
                    }
                    
                    if (!found) {
                        // If still not found, try to find file by playlist index pattern
                        // This handles cases where Unicode characters in path are incorrectly decoded
                        // Search for files matching pattern: "XX - *.mp3" where XX is playlist index
                        std::string playlist_dir;
                        size_t last_slash = file_path_to_check.find_last_of("/\\");
                        if (last_slash != std::string::npos) {
                            playlist_dir = file_path_to_check.substr(0, last_slash);
                        } else {
                            // Determine playlist directory from task
                            std::string base_dir = settings_->downloads_dir;
                            if (settings_->save_playlists_to_separate_folder && !task->playlist_name.empty()) {
                                std::string folder_name = sanitizeFilename(task->playlist_name);
                                base_dir = settings_->downloads_dir + "/" + folder_name;
                            }
                            playlist_dir = base_dir;
                        }
                        
                        // Format: "XX - " where XX is 2-digit playlist index (1-based)
                        int playlist_index_1based = pair.first + 1;
                        std::string pattern_prefix = (playlist_index_1based < 10 ? "0" : "") + std::to_string(playlist_index_1based) + " - ";
                        
#ifdef _WIN32
                        // Use Unicode version for proper support of Russian and other non-ASCII filenames
                        // Convert directory to wide string properly (UTF-8 to UTF-16)
                        int dir_size = MultiByteToWideChar(CP_UTF8, 0, playlist_dir.c_str(), -1, NULL, 0);
                        std::wstring wide_dir(dir_size, 0);
                        MultiByteToWideChar(CP_UTF8, 0, playlist_dir.c_str(), -1, &wide_dir[0], dir_size);
                        wide_dir.resize(dir_size - 1); // Remove null terminator
                        
                        int prefix_size = MultiByteToWideChar(CP_UTF8, 0, pattern_prefix.c_str(), -1, NULL, 0);
                        std::wstring wide_prefix(prefix_size, 0);
                        MultiByteToWideChar(CP_UTF8, 0, pattern_prefix.c_str(), -1, &wide_prefix[0], prefix_size);
                        wide_prefix.resize(prefix_size - 1);
                        
                        int format_size = MultiByteToWideChar(CP_UTF8, 0, settings_->selected_format.c_str(), -1, NULL, 0);
                        std::wstring wide_format(format_size, 0);
                        MultiByteToWideChar(CP_UTF8, 0, settings_->selected_format.c_str(), -1, &wide_format[0], format_size);
                        wide_format.resize(format_size - 1);
                        
                        std::wstring search_pattern = wide_dir + L"\\" + wide_prefix + L"*." + wide_format;
                        
                        // Debug: convert pattern back to UTF-8 for logging
                        int pattern_log_size = WideCharToMultiByte(CP_UTF8, 0, search_pattern.c_str(), -1, NULL, 0, NULL, NULL);
                        std::string pattern_log(pattern_log_size, 0);
                        WideCharToMultiByte(CP_UTF8, 0, search_pattern.c_str(), -1, &pattern_log[0], pattern_log_size, NULL, NULL);
                        pattern_log.resize(pattern_log_size - 1);
                        std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Searching for pattern: " + pattern_log;
                        writeConsoleUtf8(debug_msg + "\n");
                        
                        WIN32_FIND_DATAW find_data;
                        HANDLE find_handle = FindFirstFileW(search_pattern.c_str(), &find_data);
                        if (find_handle != INVALID_HANDLE_VALUE) {
                            writeConsoleUtf8("[DEBUG] processPlaylistItemFilePaths: FindFirstFileW succeeded, iterating files...\n");
                            do {
                                if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                    std::wstring wide_filename = find_data.cFileName;
                                    // Convert wide string to UTF-8
                                    int filename_size = WideCharToMultiByte(CP_UTF8, 0, wide_filename.c_str(), -1, NULL, 0, NULL, NULL);
                                    std::string filename(filename_size, 0);
                                    WideCharToMultiByte(CP_UTF8, 0, wide_filename.c_str(), -1, &filename[0], filename_size, NULL, NULL);
                                    filename.resize(filename_size - 1); // Remove null terminator
                                    
                                    // Use Windows API directly with wide strings for proper Unicode support
                                    std::wstring wide_found_path = wide_dir + L"\\" + wide_filename;
                                    
                                    // Convert back to UTF-8 for storage
                                    int path_size = WideCharToMultiByte(CP_UTF8, 0, wide_found_path.c_str(), -1, NULL, 0, NULL, NULL);
                                    if (path_size > 0) {
                                        std::string found_path_utf8(path_size, 0);
                                        WideCharToMultiByte(CP_UTF8, 0, wide_found_path.c_str(), -1, &found_path_utf8[0], path_size, NULL, NULL);
                                        found_path_utf8.resize(path_size - 1);
                                        
                                        int64_t found_file_size = -1;
                                        int64_t found_file_mtime = -1;
                                        if (getFileMetadata(found_path_utf8, found_file_size, found_file_mtime)) {
                                            actual_file_path = found_path_utf8;
                                            file_size = found_file_size;
                                            file_mtime = found_file_mtime;
                                            found = true;
                                            std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Found file by pattern for item " 
                                                                  + std::to_string(pair.first) + ": " + actual_file_path;
                                            writeConsoleUtf8(debug_msg + "\n");
                                            break;
                                        }
                                    }
                                }
                            } while (FindNextFileW(find_handle, &find_data));
                            FindClose(find_handle);
                        } else {
                            DWORD error = GetLastError();
                            std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: FindFirstFileW failed with error " + std::to_string(error) + " for pattern";
                            writeConsoleUtf8(debug_msg + "\n");
                        }
#else
                        // Unix: use opendir/readdir
                        DIR* dir = opendir(playlist_dir.c_str());
                        if (dir) {
                            struct dirent* entry;
                            while ((entry = readdir(dir)) != nullptr) {
                                std::string filename = entry->d_name;
                                if (filename.find(pattern_prefix) == 0 && 
                                    filename.length() > (pattern_prefix.length() + settings_->selected_format.length() + 1) &&
                                    filename.substr(filename.length() - settings_->selected_format.length() - 1) == "." + settings_->selected_format) {
                                    std::string found_path = playlist_dir + "/" + filename;
                                    int64_t found_file_size = -1;
                                    int64_t found_file_mtime = -1;
                                    if (getFileMetadata(found_path, found_file_size, found_file_mtime)) {
                                        file_size = found_file_size;
                                        file_mtime = found_file_mtime;
                                        actual_file_path = found_path;
                                        found = true;
                                        std::cout << "[DEBUG] processPlaylistItemFilePaths: Found file by pattern for item " 
                                                  << pair.first << ": " << actual_file_path << std::endl;
                                        break;
                                    }
                                }
                            }
                            closedir(dir);
                        }
#endif
                        
                        if (!found) {
#ifdef _WIN32
                            std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Cannot stat file for item " + std::to_string(pair.first) 
                                                  + ": " + file_path_to_check + " (tried with different extensions and pattern search)";
                            writeConsoleUtf8(debug_msg + "\n");
#else
                            std::cout << "[DEBUG] processPlaylistItemFilePaths: Cannot stat file for item " << pair.first 
                                      << ": " << file_path_to_check << " (tried with different extensions and pattern search)" << std::endl;
#endif
                        }
                    }
                }
                
                // If we found the file, update file_size, file_path, and downloaded flag
                if (found && file_size >= 0) {
                    task->playlist_items[pair.first].file_size = file_size;
                    if (!task->playlist_items[pair.first].downloaded) {
                        task->playlist_items[pair.first].downloaded = true;  // Mark as downloaded
                        newly_downloaded++;  // Increment counter only if it wasn't already marked
                    }
                    // Update file_path to the actual path if it's different from what we have
                    if (actual_file_path != file_path_to_check) {
                        task->playlist_items[pair.first].file_path = actual_file_path;
                        // Also update the map
                        task->playlist_item_file_paths[pair.first] = actual_file_path;
                        // CRITICAL: Update filename after file_path is updated to final converted file
                        // This ensures filename has the correct extension (e.g., .mp3 instead of .opus)
                        size_t last_slash = actual_file_path.find_last_of("/\\");
                        if (last_slash != std::string::npos) {
                            task->playlist_items[pair.first].filename = actual_file_path.substr(last_slash + 1);
                        } else {
                            task->playlist_items[pair.first].filename = actual_file_path;
                        }
#ifdef _WIN32
                        std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Updated file_path for item " + std::to_string(pair.first) 
                                              + " from " + file_path_to_check + " to " + actual_file_path + ", filename: " + task->playlist_items[pair.first].filename;
                        writeConsoleUtf8(debug_msg + "\n");
#else
                        std::cout << "[DEBUG] processPlaylistItemFilePaths: Updated file_path for item " << pair.first 
                                  << " from " << file_path_to_check << " to " << actual_file_path << std::endl;
#endif
                    }
#ifdef _WIN32
                    std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Set file_size for item " + std::to_string(pair.first) 
                                          + ": " + std::to_string(task->playlist_items[pair.first].file_size) + " bytes, downloaded=true";
                    writeConsoleUtf8(debug_msg + "\n");
#else
                    std::cout << "[DEBUG] processPlaylistItemFilePaths: Set file_size for item " << pair.first 
                              << ": " << task->playlist_items[pair.first].file_size << " bytes, downloaded=true" << std::endl;
#endif
                    
                    // Calculate bitrate if duration is available
                    if (task->playlist_items[pair.first].bitrate == 0 && 
                        task->playlist_items[pair.first].duration > 0 && 
                        task->playlist_items[pair.first].file_size > 0) {
                        task->playlist_items[pair.first].bitrate = AudioUtils::calculateBitrate(
                            task->playlist_items[pair.first].file_size,
                            task->playlist_items[pair.first].duration
                        );
#ifdef _WIN32
                        std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Calculated bitrate for item " + std::to_string(pair.first) 
                                              + ": " + std::to_string(task->playlist_items[pair.first].bitrate) + " kbps";
                        writeConsoleUtf8(debug_msg + "\n");
#else
                        std::cout << "[DEBUG] processPlaylistItemFilePaths: Calculated bitrate for item " << pair.first 
                                  << ": " << task->playlist_items[pair.first].bitrate << " kbps" << std::endl;
#endif
                    }
                }
            }
        } else {
#ifdef _WIN32
            std::string debug_msg = "[DEBUG] processPlaylistItemFilePaths: Invalid index " + std::to_string(pair.first) 
                                  + " (playlist_items.size()=" + std::to_string(task->playlist_items.size()) + ")";
            writeConsoleUtf8(debug_msg + "\n");
#else
            std::cout << "[DEBUG] processPlaylistItemFilePaths: Invalid index " << pair.first 
                      << " (playlist_items.size()=" << task->playlist_items.size() << ")" << std::endl;
#endif
        }
    }
    
#ifdef _WIN32
    std::string final_debug_msg = "[DEBUG] processPlaylistItemFilePaths: Processed " + std::to_string(newly_downloaded) + " newly downloaded items";
    writeConsoleUtf8(final_debug_msg + "\n");
#else
    std::cout << "[DEBUG] processPlaylistItemFilePaths: Processed " << newly_downloaded << " newly downloaded items" << std::endl;
#endif
    return newly_downloaded;
}

bool App::checkExistingPlaylistFiles(DownloadTask* task, const Downloader::PlaylistInfo& playlist_info) {
    // CRITICAL: Initial validation must be done with lock protection
    // to ensure task is not deleted while we check it
    std::string playlist_name;
    size_t playlist_items_size = 0;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (!task || !task->is_playlist || task->playlist_items.empty()) {
            return false;
        }
        playlist_name = task->playlist_name;
        playlist_items_size = task->playlist_items.size();
    }
    
    auto start_time = std::chrono::steady_clock::now();
    std::cout << "[DEBUG] checkExistingPlaylistFiles: Checking for existing files for playlist: " << playlist_name 
              << " (" << playlist_items_size << " items)" << std::endl;
    
    // Determine playlist folder path (cross-platform)
    std::string output_dir = settings_->downloads_dir;
    if (settings_->save_playlists_to_separate_folder && !playlist_name.empty()) {
        std::string folder_name = sanitizeFilename(playlist_name);
        output_dir = PathUtils::joinPath(settings_->downloads_dir, folder_name);
    }
    
    // Check if folder exists
    if (!isDirectory(output_dir)) {
        std::cout << "[DEBUG] checkExistingPlaylistFiles: Folder does not exist: " << output_dir << std::endl;
        return false;
    }
    
    // Collect all files in the directory
    std::vector<std::string> found_files;
    
    // OPTIMIZATION: Limit the number of files to check (max 1000 files)
    // This prevents performance issues with very large directories
    // Increased limit to handle larger playlists
    const size_t MAX_FILES_TO_CHECK = 1000;
    size_t files_collected = 0;
    
#ifdef _WIN32
    // Use Unicode version (FindFirstFileW) for proper support of Russian and other non-ASCII filenames
    int dir_size_needed = MultiByteToWideChar(CP_UTF8, 0, output_dir.c_str(), -1, NULL, 0);
    if (dir_size_needed > 0) {
        std::vector<wchar_t> dir_wide(dir_size_needed);
        MultiByteToWideChar(CP_UTF8, 0, output_dir.c_str(), -1, dir_wide.data(), dir_size_needed);
        std::wstring search_pattern = std::wstring(dir_wide.data()) + L"\\*";
        
        WIN32_FIND_DATAW find_data;
        HANDLE find_handle = FindFirstFileW(search_pattern.c_str(), &find_data);
        if (find_handle != INVALID_HANDLE_VALUE) {
            do {
                if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    // Convert filename from wide string to UTF-8
                    int filename_size = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, NULL, 0, NULL, NULL);
                    if (filename_size > 0) {
                        std::vector<char> filename_utf8(filename_size);
                        WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, filename_utf8.data(), filename_size, NULL, NULL);
                        std::string file_name_utf8(filename_utf8.data());
                        std::string file_path = PathUtils::joinPath(output_dir, file_name_utf8);
                        // Skip temporary files
                        if (!ValidationUtils::isTemporaryFile(file_path)) {
                            found_files.push_back(file_path);
                            files_collected++;
                            if (files_collected >= MAX_FILES_TO_CHECK) {
                                std::cout << "[DEBUG] checkExistingPlaylistFiles: Reached file limit (" << MAX_FILES_TO_CHECK 
                                          << "), stopping file collection" << std::endl;
                                break;
                            }
                        }
                    }
                }
            } while (FindNextFileW(find_handle, &find_data));
            FindClose(find_handle);
        }
    }
#else
    DIR* dir = opendir(output_dir.c_str());
    if (dir != nullptr) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            
            std::string file_path = PathUtils::joinPath(output_dir, entry->d_name);
            if (isRegularFile(file_path)) {
                // Skip temporary files
                if (!ValidationUtils::isTemporaryFile(file_path)) {
                    found_files.push_back(file_path);
                    files_collected++;
                    if (files_collected >= MAX_FILES_TO_CHECK) {
                        std::cout << "[DEBUG] checkExistingPlaylistFiles: Reached file limit (" << MAX_FILES_TO_CHECK 
                                  << "), stopping file collection" << std::endl;
                        break;
                    }
                }
            }
        }
        closedir(dir);
    }
#endif
    
    if (found_files.empty()) {
        std::cout << "[DEBUG] checkExistingPlaylistFiles: No files found in directory" << std::endl;
        return false;
    }
    
    std::cout << "[DEBUG] checkExistingPlaylistFiles: Found " << found_files.size() << " files in directory" << std::endl;
    
    // Match files to playlist items by title (fuzzy matching)
    // OPTIMIZATION: Pre-process filenames to avoid repeated extraction and lowercasing
    struct FileInfo {
        std::string path;
        std::string name;
        std::string name_lower;
    };
    
    std::vector<FileInfo> file_infos;
    file_infos.reserve(found_files.size());
    
    // Pre-process all filenames once
    for (const auto& file_path : found_files) {
        FileInfo info;
        info.path = file_path;
        
        // Extract filename from path (cross-platform)
#ifdef _WIN32
        size_t last_slash = file_path.find_last_of("\\/");
#else
        size_t last_slash = file_path.find_last_of("/");
#endif
        if (last_slash != std::string::npos) {
            info.name = file_path.substr(last_slash + 1);
        } else {
            info.name = file_path;
        }
        
        info.name_lower = info.name;
        std::transform(info.name_lower.begin(), info.name_lower.end(), info.name_lower.begin(), ::tolower);
        file_infos.push_back(std::move(info));
    }
    
    int matched_count = 0;
    std::set<size_t> used_file_indices;  // Track which files have been assigned (by index)
    
    // Pre-process playlist item titles (with lock protection)
    std::vector<std::pair<std::string, std::string>> item_titles;  // (sanitized, lowercased)
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (!task || !task->is_playlist || task->playlist_items.empty()) {
            std::cout << "[DEBUG] checkExistingPlaylistFiles: Task was deleted or invalid during title preprocessing" << std::endl;
            return false;
        }
        item_titles.reserve(task->playlist_items.size());
        for (const auto& item : task->playlist_items) {
            std::string search_title = sanitizeFilename(item.title);
            std::string search_title_lower = search_title;
            std::transform(search_title_lower.begin(), search_title_lower.end(), search_title_lower.begin(), ::tolower);
            item_titles.push_back({search_title, search_title_lower});
        }
    }
    
    // Match items to files (optimized: pre-processed data)
    // CRITICAL: Use lock to protect task from being deleted while we access it
    // Re-validate playlist_items_size before matching (task might have changed)
    size_t matching_items_size = 0;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (!task || !task->is_playlist) {
            std::cout << "[DEBUG] checkExistingPlaylistFiles: Task was deleted or invalid before matching" << std::endl;
            return false;
        }
        matching_items_size = task->playlist_items.size();
    }
    
    for (size_t i = 0; i < matching_items_size; i++) {
        const auto& title_pair = item_titles[i];
        const std::string& search_title_lower = title_pair.second;
        
        // Try to find matching file
        for (size_t file_idx = 0; file_idx < file_infos.size(); file_idx++) {
            // Skip if file already assigned to another item
            if (used_file_indices.find(file_idx) != used_file_indices.end()) {
                continue;
            }
            
            const auto& file_info = file_infos[file_idx];
            
            // Check if filename contains title (case-insensitive)
            if (file_info.name_lower.find(search_title_lower) != std::string::npos || 
                search_title_lower.find(file_info.name_lower) != std::string::npos) {
                // Match found! Use lock to safely update task
                std::string item_title_for_log;
                {
                    std::lock_guard<std::mutex> lock(tasks_mutex_);
                    // Verify task is still valid before updating
                    if (!task || !task->is_playlist || i >= task->playlist_items.size()) {
                        std::cout << "[DEBUG] checkExistingPlaylistFiles: Task was deleted during file matching, stopping" << std::endl;
                        return false;
                    }
                    task->playlist_items[i].file_path = file_info.path;
                    task->playlist_items[i].downloaded = true;
                    task->playlist_item_file_paths[static_cast<int>(i)] = file_info.path;
                    item_title_for_log = task->playlist_items[i].title;
                }
                used_file_indices.insert(file_idx);
                matched_count++;
                
                // Get file size and metadata
                int64_t file_size = getFileSize(file_info.path);
                int duration = 0;
                int bitrate = 0;
                
                // Update file size and calculate bitrate with lock protection
                if (file_size > 0) {
                    std::lock_guard<std::mutex> lock(tasks_mutex_);
                    if (!task || !task->is_playlist || i >= task->playlist_items.size()) {
                        std::cout << "[DEBUG] checkExistingPlaylistFiles: Task was deleted during file size update" << std::endl;
                        return false;
                    }
                    task->playlist_items[i].file_size = file_size;
                    duration = task->playlist_items[i].duration;
                    if (task->playlist_items[i].bitrate == 0 && duration > 0 && file_size > 0) {
                        bitrate = AudioUtils::calculateBitrate(file_size, duration);
                        task->playlist_items[i].bitrate = bitrate;
                    }
                }
                
                std::cout << "[DEBUG] checkExistingPlaylistFiles: Matched item " << i << " (" << item_title_for_log 
                          << ") to file: " << file_info.path << std::endl;
                break;
            }
        }
        
        // Don't exit early - check all items to ensure all files are matched
        // This ensures that all existing files are properly detected, not just the first 50%
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    // CRITICAL: Get playlist_items.size() with lock protection for final check
    size_t final_items_size = 0;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (!task || !task->is_playlist) {
            std::cout << "[DEBUG] checkExistingPlaylistFiles: Task was deleted before final check" << std::endl;
            return false;
        }
        final_items_size = task->playlist_items.size();
    }
    
    std::cout << "[DEBUG] checkExistingPlaylistFiles: Matched " << matched_count << " out of " 
              << final_items_size << " items in " << elapsed_ms << " ms" << std::endl;
    
    // If we found at least some files (threshold: 50% or more), mark as already exists
    if (matched_count > 0 && matched_count * 2 >= static_cast<int>(final_items_size)) {
        std::cout << "[DEBUG] checkExistingPlaylistFiles: Found existing files (" << matched_count 
                  << " out of " << final_items_size << "), will create history entry" << std::endl;
        
        // CRITICAL: All task modifications must be done with lock protection
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            // Verify task is still valid before modifying
            if (!task || !task->is_playlist) {
                std::cout << "[DEBUG] checkExistingPlaylistFiles: Task was deleted before marking as already_exists" << std::endl;
                return false;
            }
            
            // Set thumbnail URL from playlist_info if available
            if (!playlist_info.thumbnail_url.empty()) {
                task->thumbnail_url = playlist_info.thumbnail_url;
                std::cout << "[DEBUG] checkExistingPlaylistFiles: Set thumbnail_url: " << task->thumbnail_url << std::endl;
            }
            
            // Mark task as already exists
            task->status = "already_exists";
            task->progress = 1.0f;
            
            // CRITICAL: Prevent duplicate entries - mark URL as in history immediately
            // This prevents addDownloadTask from creating a duplicate task
            history_urls_.insert(task->url);
            std::cout << "[DEBUG] checkExistingPlaylistFiles: Added URL to history_urls_ to prevent duplicates: " << task->url << std::endl;
        }
        
        // Mark task as already exists (caller should add to history after releasing lock)
        std::cout << "[DEBUG] checkExistingPlaylistFiles: Found existing files, task will be marked as already_exists" << std::endl;
        return true;
    }
    
    return false;
}

// formatDuration moved to AudioUtils::formatDuration

std::string App::sanitizeFilename(const std::string& name) {
    // Delegate to ValidationUtils for consistency
    return ValidationUtils::sanitizeFilename(name);
}

std::string App::normalizeProxy(const std::string& proxy) {
    // Use ValidationUtils instead of duplicating logic
    return ValidationUtils::normalizeProxy(proxy);
}

YtDlpSettings App::createYtDlpSettings() const {
    // Use Settings class to create YtDlpSettings
    return settings_->createYtDlpSettings();
}

// Sync methods removed - all settings fields migrated to Settings class

void App::loadMetadata(DownloadTask* task) {
    if (metadata_manager_) {
        metadata_manager_->loadMetadata(task);
    }
}

std::string App::getConfigPath() {
    return PlatformUtils::getConfigPath();
}

void App::loadSettings() {
    std::cout << "[DEBUG] App::loadSettings: Loading settings from config file..." << std::endl;
    // Load settings through Settings class
    settings_->load();
    std::cout << "[DEBUG] App::loadSettings: Settings loaded successfully" << std::endl;
    // Settings fields removed - using settings_->* directly, no sync needed
}

void App::saveSettings() {
    // Settings fields removed - using settings_->* directly, no sync needed
    settings_->save();
    
    // Update service checker proxy if proxy settings changed
    if (service_checker_) {
        if (settings_->use_proxy && !settings_->proxy_input.empty()) {
            std::cout << "[DEBUG] App::saveSettings: Updating proxy for service checker: " << settings_->proxy_input << std::endl;
            service_checker_->setProxy(settings_->proxy_input);
        } else {
            std::cout << "[DEBUG] App::saveSettings: Clearing proxy for service checker" << std::endl;
            service_checker_->setProxy("");  // Clear proxy if disabled
        }
    }
}

std::string App::getHistoryPath() {
    return PlatformUtils::getHistoryPath();
}

static void ensureFilePath(App* app, DownloadTask* task, const std::string& downloads_dir) {
    if (!task || !app) return;
    if (!task->file_path.empty()) return;
    if (task->filename.empty()) return;
    std::string fallback_name = app->sanitizeFilename(task->filename);
    if (fallback_name.empty()) return;
    task->file_path = PathUtils::normalizePath(PathUtils::joinPath(downloads_dir, fallback_name));
}

static std::string playlistFolderPath(App* app, const DownloadTask* task, const std::string& downloads_dir, bool save_playlists_to_separate_folder) {
    std::string playlist_folder_path = downloads_dir;
    if (save_playlists_to_separate_folder && task && !task->playlist_name.empty()) {
        std::string folder_name = app ? app->sanitizeFilename(task->playlist_name) : task->playlist_name;
        playlist_folder_path = PathUtils::joinPath(downloads_dir, folder_name);
    }
    return PathUtils::normalizePath(playlist_folder_path);
}

static void normalizeTaskAfterDownload(App* app, DownloadTask* task, const std::string& downloads_dir, bool save_playlists_to_separate_folder) {
    if (!task || !app) return;
    // Collapse single-item playlists into single file
    if (task->is_playlist && task->total_playlist_items <= 1) {
        // Copy file_path from playlist_item_file_paths[0] if available
        if (task->file_path.empty() && !task->playlist_item_file_paths.empty()) {
            auto it = task->playlist_item_file_paths.find(0);
            if (it != task->playlist_item_file_paths.end() && !it->second.empty()) {
                task->file_path = it->second;
                std::cout << "[DEBUG] Copied file_path from playlist_item_file_paths[0]: " << task->file_path << std::endl;
            }
        }
        // Also try to get from playlist_items[0].file_path
        if (task->file_path.empty() && !task->playlist_items.empty() && !task->playlist_items[0].file_path.empty()) {
            task->file_path = task->playlist_items[0].file_path;
            std::cout << "[DEBUG] Copied file_path from playlist_items[0].file_path: " << task->file_path << std::endl;
        }
        task->is_playlist = false;
        task->total_playlist_items = 0;
    }
    if (!task->is_playlist) {
        ensureFilePath(app, task, downloads_dir);
        
        // CRITICAL: If file_path is a temporary file, try to find the final converted file
        if (!task->file_path.empty() && ValidationUtils::isTemporaryFile(task->file_path)) {
            std::cout << "[DEBUG] normalizeTaskAfterDownload: File is temporary, searching for final file: " << task->file_path << std::endl;
            
            // Try to find final file by removing .part extension and trying different formats
            std::string base_path = task->file_path;
            
            // Remove .part extension
            size_t part_pos = base_path.find(".part");
            if (part_pos != std::string::npos) {
                base_path = base_path.substr(0, part_pos);
            }
            
            // Remove .f*.part pattern (e.g., .f1.part)
            size_t fpart_pos = base_path.find(".f");
            if (fpart_pos != std::string::npos) {
                size_t next_dot = base_path.find(".", fpart_pos + 2);
                if (next_dot != std::string::npos) {
                    base_path = base_path.substr(0, fpart_pos);
                }
            }
            
            // Try to find final file with different extensions
            std::string final_path;
            const char* extensions[] = {".mp3", ".m4a", ".opus", ".ogg", ".flac", ".webm", ".mkv"};
            for (const char* ext : extensions) {
                // Remove current extension if exists
                std::string test_path = base_path;
                size_t last_dot = test_path.find_last_of(".");
                if (last_dot != std::string::npos) {
                    test_path = test_path.substr(0, last_dot);
                }
                test_path += ext;
                
                if (fileExists(test_path) && !ValidationUtils::isTemporaryFile(test_path)) {
                    final_path = test_path;
                    std::cout << "[DEBUG] normalizeTaskAfterDownload: Found final file: " << final_path << std::endl;
                    break;
                }
            }
            
            if (!final_path.empty()) {
                task->file_path = final_path;
            } else {
                std::cout << "[DEBUG] normalizeTaskAfterDownload: Could not find final file, clearing file_path" << std::endl;
                task->file_path.clear();
            }
        }
        
        if (task->file_size == 0 && !task->file_path.empty()) {
            task->file_size = AudioUtils::getFileSize(task->file_path);
        }
        if (task->metadata.bitrate == 0 && task->metadata.duration > 0 && task->file_size > 0) {
            task->metadata.bitrate = AudioUtils::calculateBitrate(task->file_size, task->metadata.duration);
        }
    } else {
        // For playlists ensure folder path is available via helper when saving
        (void)save_playlists_to_separate_folder; // silence unused when not needed
    }
}
void App::startMetadataWorker() {
    if (metadata_manager_) {
        metadata_manager_->startMetadataWorker();
    }
}

void App::enqueueMetadataRefresh(DownloadTask* task) {
    if (metadata_manager_) {
        metadata_manager_->enqueueMetadataRefresh(task);
    }
}

void App::runBackground(std::function<void()> fn) {
    if (!fn) return;
    // Don't create new background threads during shutdown
    if (shutting_down_) {
        std::cout << "[DEBUG] Skipping background thread creation (shutdown)" << std::endl;
        return;
    }
    std::lock_guard<std::mutex> lock(background_threads_mutex_);
    background_threads_.emplace_back(std::move(fn));
}

void App::joinBackgroundThreads() {
    std::vector<std::thread> to_join;
    {
        std::lock_guard<std::mutex> lock(background_threads_mutex_);
        to_join.swap(background_threads_);
    }
    // During shutdown, detach threads instead of joining to avoid blocking
    // This prevents hanging if threads are downloading thumbnails or doing network operations
    if (shutting_down_) {
        for (auto& t : to_join) {
            if (t.joinable()) {
                std::cout << "[DEBUG] Detaching background thread during shutdown" << std::endl;
                t.detach();
            }
        }
    } else {
        // Normal operation: join threads
        for (auto& t : to_join) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
}

void App::loadHistory() {
    if (history_manager_) {
        std::cout << "[DEBUG] App::loadHistory: Loading history from file..." << std::endl;
        history_manager_->loadHistory();
        rebuildHistoryViewTasks();
        // Update history_urls_ from loaded history
        {
            const auto& items = history_manager_->getHistoryItems();
            history_urls_.clear();
            size_t url_count = 0;
            for (const auto& item : items) {
                if (!item.url.empty()) {
                    history_urls_.insert(item.url);
                    url_count++;
                }
            }
            std::cout << "[DEBUG] App::loadHistory: Loaded " << items.size() << " history items, " 
                      << url_count << " unique URLs" << std::endl;
        }
    } else {
        std::cout << "[DEBUG] App::loadHistory: HistoryManager not initialized" << std::endl;
    }
}

void App::rebuildHistoryViewTasks() {
    if (history_manager_) {
        std::cout << "[DEBUG] App::rebuildHistoryViewTasks: Rebuilding history view tasks..." << std::endl;
        history_manager_->rebuildHistoryViewTasks();
        // Update history_urls_ from rebuilt tasks
        const auto& tasks = history_manager_->getHistoryViewTasks();
        history_urls_.clear();
        size_t url_count = 0;
        for (const auto& task : tasks) {
            if (task && !task->url.empty()) {
                history_urls_.insert(task->url);
                url_count++;
            }
        }
        std::cout << "[DEBUG] App::rebuildHistoryViewTasks: Rebuilt " << tasks.size() << " view tasks, " 
                  << url_count << " unique URLs added to history_urls_" << std::endl;
    } else {
        std::cout << "[DEBUG] App::rebuildHistoryViewTasks: HistoryManager not initialized" << std::endl;
    }
}

void App::reloadHistoryCacheFromFile() {
    if (history_manager_) {
        history_manager_->reloadHistoryCacheFromFile();
        rebuildHistoryViewTasks();
    }
}

// Accessor methods for history data
const std::vector<HistoryItem>& App::getHistoryItems() const {
    static thread_local std::vector<HistoryItem> cached_items;
    if (history_manager_) {
        cached_items = history_manager_->getHistoryItems();
        return cached_items;
    }
    static std::vector<HistoryItem> empty;
    return empty;
}

std::vector<std::unique_ptr<DownloadTask>> App::getHistoryViewTasks() const {
    if (history_manager_) {
        return history_manager_->getHistoryViewTasks();
    }
    return {};  // Return empty vector
}

bool App::isUrlDeleted(const std::string& url) const {
    if (history_manager_) {
        return history_manager_->isUrlDeleted(url);
    }
    return false;
}

void App::deleteUrlFromHistory(const std::string& url) {
    if (history_manager_) {
        history_manager_->deleteUrl(url);
    }
}

void App::persistHistoryItems() {
    if (history_manager_) {
        history_manager_->persistHistoryItems();
    }
}

void App::saveHistory() {
    std::string history_path = getHistoryPath();

    // Load existing history to preserve older items
    std::vector<HistoryItem> existing_items;
    {
        std::ifstream in(history_path);
        if (in.is_open()) {
            try {
                json doc;
                in >> doc;
                if (doc.contains("items") && doc["items"].is_array()) {
                    existing_items = doc["items"].get<std::vector<HistoryItem>>();
                }
            } catch (...) {
                // ignore parse errors
            }
        }
    }

    // Build map by URL to avoid duplicates
    std::unordered_map<std::string, HistoryItem> by_url;
    for (const auto& h : existing_items) {
        if (!h.url.empty()) {
            by_url[h.url] = h;
        }
    }

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& task : tasks_) {
            if (!task) continue;
            // Skip in-progress; persist completed/error/cancelled/already_exists
            if (task->status == "queued" || task->status == "downloading") continue;

            normalizeTaskAfterDownload(this, task.get(), settings_->downloads_dir, settings_->save_playlists_to_separate_folder);
            bool treat_as_playlist = task->is_playlist && task->total_playlist_items > 1;
            HistoryItem h;
            // Generate or preserve ID
            if (by_url.find(task->url) != by_url.end()) {
                h.id = by_url[task->url].id;  // Preserve existing ID
            } else {
                // Generate unique ID
                h.id = HistoryUtils::generateHistoryId(task->url, task.get());
            }
            h.url = task->url;
            h.status = task->status;
            h.timestamp = time(nullptr);
            h.platform = task->platform;
            h.filename = task->filename;
            h.title = task->metadata.title;
            h.artist = task->metadata.artist;
            h.duration = task->metadata.duration;
            h.bitrate = task->metadata.bitrate;
            h.file_size = task->file_size;
            h.is_playlist = treat_as_playlist;
            h.playlist_name = task->playlist_name;
            h.total_playlist_items = task->total_playlist_items;

            if (treat_as_playlist) {
                h.filepath = playlistFolderPath(this, task.get(), settings_->downloads_dir, settings_->save_playlists_to_separate_folder);
                // CRITICAL: Process playlist_item_file_paths FIRST to ensure all file_paths are set
                // This is especially important for the last item
                processPlaylistItemFilePaths(task.get());
                h.playlist_items = task->playlist_items;
                // Process playlist items to ensure file_size and bitrate are set
                processPlaylistItemsMetadata(h.playlist_items);
                // CRITICAL: Normalize all file paths in playlist items for Windows compatibility
                for (auto& item : h.playlist_items) {
                    if (!item.file_path.empty()) {
                        item.file_path = PathUtils::normalizePath(item.file_path);
                    }
                }
            } else {
                task->is_playlist = false;
                task->total_playlist_items = 0;
                ensureFilePath(this, task.get(), settings_->downloads_dir);
                h.filepath = task->file_path;
                if (h.file_size == 0 && !h.filepath.empty()) {
                    int64_t file_size = AudioUtils::getFileSize(h.filepath);
                    if (file_size > 0) {
                        h.file_size = file_size;
                        task->file_size = file_size;
                    }
                }
                if (h.bitrate == 0 && h.duration > 0 && h.file_size > 0) {
                    h.bitrate = AudioUtils::calculateBitrate(h.file_size, h.duration);
                }
            }

            if (h.url.empty()) continue;
            if (!h.is_playlist && h.filepath.empty()) continue;

            by_url[h.url] = h;
        }
    }

    // Write back
    json out;
    out["items"] = json::array();
    for (const auto& kv : by_url) {
        out["items"].push_back(kv.second);
    }

    std::ofstream out_file(history_path);
    if (!out_file.is_open()) {
        return;
    }
    out_file << out.dump(2, ' ', false) << "\n";
    // Update HistoryManager with new items
    if (history_manager_) {
        history_manager_->reloadHistoryCacheFromFile();
    rebuildHistoryViewTasks();
    }
}

void App::rewriteHistoryFromTasks() {
    std::string history_path = getHistoryPath();
    std::lock_guard<std::mutex> lock(tasks_mutex_);

    // Load existing history items to preserve them
    std::map<std::string, HistoryItem> existing_items;
    if (history_manager_) {
        const auto& existing_history = history_manager_->getHistoryItems();
        for (const auto& item : existing_history) {
            existing_items[item.url] = item;  // Store by URL to preserve thumbnails and other data
        }
    }

    json out;
    out["items"] = json::array();

    for (auto& task : tasks_) {
        if (!task) continue;
        if (task->status == "queued" || task->status == "downloading") continue;
        normalizeTaskAfterDownload(this, task.get(), settings_->downloads_dir, settings_->save_playlists_to_separate_folder);
        bool treat_as_playlist = task->is_playlist && task->total_playlist_items > 1;
        HistoryItem h;
        // Generate or preserve ID
        auto existing_it = existing_items.find(task->url);
        if (existing_it != existing_items.end()) {
            h.id = existing_it->second.id;  // Preserve existing ID
        } else {
            // Generate unique ID: timestamp + URL hash + task pointer
            int64_t now = time(nullptr);
            std::hash<std::string> hasher;
            size_t url_hash = hasher(task->url);
            h.id = std::to_string(now) + "_" + std::to_string(url_hash) + "_" + std::to_string(reinterpret_cast<uintptr_t>(task.get()));
        }
        h.url = task->url;
        h.status = task->status;
        h.timestamp = time(nullptr);
        h.platform = task->platform;
        h.filename = task->filename;
        h.title = task->metadata.title;
        h.artist = task->metadata.artist;
        h.duration = task->metadata.duration;
        h.bitrate = task->metadata.bitrate;
        h.file_size = task->file_size;
        h.is_playlist = treat_as_playlist;
        h.playlist_name = task->playlist_name;
        h.total_playlist_items = task->total_playlist_items;
        
        // Preserve thumbnail_base64 from existing history if available
        auto existing_it2 = existing_items.find(task->url);
        if (existing_it2 != existing_items.end()) {
            h.thumbnail_base64 = existing_it2->second.thumbnail_base64;
            std::cout << "[DEBUG] addToHistory: Preserved existing thumbnail_base64 for URL=" << task->url << std::endl;
        } else if (!task->thumbnail_url.empty() && h.thumbnail_base64.empty()) {
            // For "already_exists" status, we need to load thumbnail if it's not in history yet
            // This will be done asynchronously in the thumbnail download section below
            std::cout << "[DEBUG] addToHistory: Will download thumbnail for already_exists playlist: " << task->thumbnail_url << std::endl;
        }
        // Note: We don't download thumbnails here synchronously to avoid blocking UI thread
        // Thumbnails should be loaded asynchronously via addToHistory() which is called after download completes
        // If thumbnail_base64 is empty but thumbnail_url exists, it will be loaded later by addToHistory()

        if (treat_as_playlist) {
            h.filepath = playlistFolderPath(this, task.get(), settings_->downloads_dir, settings_->save_playlists_to_separate_folder);
            // CRITICAL: Process playlist_item_file_paths FIRST to ensure all file_paths are set
            // This is especially important for the last item
            processPlaylistItemFilePaths(task.get());
            h.playlist_items = task->playlist_items;
            // Process playlist items to ensure file_size and bitrate are set
            processPlaylistItemsMetadata(h.playlist_items);
            // CRITICAL: Normalize all file paths in playlist items for Windows compatibility
            for (auto& item : h.playlist_items) {
                if (!item.file_path.empty()) {
                    item.file_path = PathUtils::normalizePath(item.file_path);
                }
            }
        } else {
            task->is_playlist = false;
            task->total_playlist_items = 0;
            ensureFilePath(this, task.get(), settings_->downloads_dir);
            h.filepath = task->file_path;
            if (h.file_size == 0 && !h.filepath.empty()) {
                int64_t file_size = AudioUtils::getFileSize(h.filepath);
                if (file_size > 0) {
                    h.file_size = file_size;
                    task->file_size = file_size;
                }
            }
            if (h.bitrate == 0 && h.duration > 0 && h.file_size > 0) {
                h.bitrate = AudioUtils::calculateBitrate(h.file_size, h.duration);
            }
        }

        if (h.url.empty()) continue;
        if (!h.is_playlist && h.filepath.empty()) continue;

        out["items"].push_back(h);
        // Remove from existing_items so we know which items to keep
        existing_items.erase(task->url);
    }
    
    // Add remaining existing items that are not in current tasks (preserve old history)
    for (const auto& pair : existing_items) {
        const auto& existing_item = pair.second;
        // Only preserve items that are not deleted and have valid data
        if (!existing_item.url.empty() && (existing_item.is_playlist || !existing_item.filepath.empty())) {
            out["items"].push_back(existing_item);
        }
    }

    std::ofstream file(history_path);
    if (!file.is_open()) {
        std::cout << "[DEBUG][rewriteHistory] cannot open " << history_path << std::endl;
        return;
    }
    // Use ensure_ascii=false to preserve UTF-8 characters (Russian, etc.)
    file << out.dump(2, ' ', false) << "\n";
    file.close();
    
    // Update HistoryManager with new items asynchronously to avoid blocking UI
    if (history_manager_) {
        runBackground([this]() {
            if (history_manager_) {
                history_manager_->reloadHistoryCacheFromFile();
                rebuildHistoryViewTasks();
            }
        });
    }
}

// Placeholder thumbnail base64 (small gray image)
static const std::string PLACEHOLDER_THUMBNAIL_BASE64 = 
    "/9j/4AAQSkZJRgABAQEASABIAAD/2wBDAAoHBwgHBgoICAgLCgoLDhgQDg0NDh0VFhEYIx8lJCIfIiEmKzcvJik0KSEiMEExNDk7Pj4+JS5ESUM8SDc9Pjv/2wBDAQoLCw4NDhwQEBw7KCIoOzs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozv/wAARCAA8ADwDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDxmiiigAooqR7eaOJJXjZUf7rEcGgCOiiigAooooAK0dMjgjgmvbmISpGVRUJwCSf8M0+WysJYJ/scshktl3MXxiQdyPSmY2+Hs/3rkZ/BTQBDqFotvqDwRElCQU+h5Fal7L58eoWuSUttvlj024U4qK5QPrGnd96RZ/PFNQl73VB/eR//AEIUAULKwmvpCkW0BRlmY4A+pqGeCS2maGUYdDgirsT+XoM2ODLMFJ9gM0/Uo/OubTHWaGPJ9+lAGZRW3LbWMz3VlDbhJLZSUlBJLleuay4bG6uELwwO6g4yozzQBJpt2lpcEyoXikUpIo6kGpL66tzbQ2loXMUZLMzjBZj7VQooA6C3ha5udLux/qo0xI2Pu7CSc/hiqmmuJr65HUyxvj+f9KoR3lxFbvBHMyxSfeUHg0tjc/Y7yOfGQp5HqCMH9DQBZcbNBjGOXuCfyAq1a3FlLFa3FxLsksxho8cygdAKqand283lQ2gcQRA439SScmqFAFgXsyXrXaNtkLFuPerQ8QaggxFKsK/3Y1AFZtFABRRRQAUUUUAFFFFABRRRQB//2Q==";

void App::createHistoryItemImmediately(DownloadTask* task, const std::string& platform) {
    if (!task || !history_manager_) {
        return;
    }
    
    // CRITICAL: Check both history_urls_ and history items to prevent duplicates
    // This prevents race conditions where DownloadManager checks before item is added
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        // Check if URL is already in history_urls_ (fast check)
        if (history_urls_.find(task->url) != history_urls_.end()) {
            std::cout << "[DEBUG] createHistoryItemImmediately: URL already in history_urls_, skipping" << std::endl;
            return;
        }
    }
    
    // Check if item already exists in history (slower but more thorough)
    const auto& history_items = getHistoryItems();
    for (const auto& item : history_items) {
        if (item.url == task->url) {
            std::cout << "[DEBUG] createHistoryItemImmediately: Item already exists in history, skipping" << std::endl;
            // Also add to history_urls_ to prevent future duplicates
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                history_urls_.insert(task->url);
            }
            return;
        }
    }
    
    // Create history item with minimal information
    HistoryItem h;
    // Generate unique ID
    h.id = HistoryUtils::generateHistoryId(task->url, task);
    h.url = task->url;
    h.status = task->status;  // Will be "queued" initially
    h.timestamp = time(nullptr);
    h.platform = platform;
    h.is_playlist = task->is_playlist;
    h.playlist_name = task->playlist_name;
    h.total_playlist_items = task->total_playlist_items;
    
    // Don't set placeholder thumbnail here - it will be added only if we fail to get real thumbnail
    // thumbnail_base64 will be empty initially, and placeholder will be used in UI if needed
    h.thumbnail_base64 = "";
    
    // CRITICAL: Add URL to history_urls_ BEFORE adding to history
    // This prevents race conditions where another thread checks history_urls_ before item is added
    // We do this in a single atomic operation with the check above
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        // Double-check after acquiring lock (another thread might have added it)
        if (history_urls_.find(task->url) != history_urls_.end()) {
            std::cout << "[DEBUG] createHistoryItemImmediately: URL already in history_urls_ (double-check), skipping" << std::endl;
            return;
        }
        history_urls_.insert(task->url);
    }
    
    // Add to history immediately
    // HistoryManager::addHistoryItem will update existing item if URL matches (prevents duplicates)
    history_manager_->addHistoryItem(h);
    // Persist asynchronously to avoid blocking UI
    if (!shutting_down_) {
        runBackground([this]() {
            if (!shutting_down_ && history_manager_) {
                history_manager_->persistHistoryItems();
            }
        });
        std::cout << "[DEBUG] createHistoryItemImmediately: Created history item with placeholder for URL=" << task->url << std::endl;
    }
}

void App::addToHistory(DownloadTask* task) {
    if (!task) {
        std::cout << "[DEBUG] addToHistory: task is null" << std::endl;
        return;
    }
    
    std::cout << "[DEBUG] addToHistory: Called for URL=" << task->url << ", status=" << task->status << std::endl;
    
    // Skip adding to history during shutdown
    if (shutting_down_) {
        return;
    }
    
    // Persist statuses except in-progress
    if (task->status == "queued" || task->status == "downloading") {
        return;
    }
    
    bool is_already_exists = (task->status == "already_exists");
    bool is_completed = (task->status == "completed");
    bool is_cancelled = (task->status == "cancelled");
    bool url_was_deleted = isUrlDeleted(task->url);
    
    // Skip if this URL was explicitly deleted by the user (except for already_exists, completed, and cancelled)
    if (url_was_deleted && !is_already_exists && !is_completed && !is_cancelled) {
        return;
    }
    
    // For completed status, remove from deleted_urls_ since user successfully re-downloaded
    if (url_was_deleted && is_completed && history_manager_) {
        history_manager_->removeDeletedUrl(task->url);
    }
    
    // Check history outside tasks_mutex_ to avoid deadlock
    bool already_in_history = false;
    if (history_manager_) {
        const auto& history_items = getHistoryItems();
        for (const auto& h : history_items) {
            if (h.url == task->url) {
                already_in_history = true;
                break;
            }
        }
    }
    
    // Add URL to history_urls_ set (quick lock)
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        history_urls_.insert(task->url);
    }
    
    // Always update/add to history (HistoryManager will update existing item if URL matches)
    if (history_manager_) {
        // Normalize task before adding to history
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            normalizeTaskAfterDownload(this, task, settings_->downloads_dir, settings_->save_playlists_to_separate_folder);
        }
        
        // Get history items BEFORE locking tasks_mutex_ to avoid deadlock
        std::vector<HistoryItem> history_items_copy;
        if (history_manager_) {
            history_items_copy = history_manager_->getHistoryItems();
        }
        
        // Read task data while holding lock (to ensure consistency)
        bool treat_as_playlist;
        HistoryItem h;
        {
            std::lock_guard<std::mutex> lock(tasks_mutex_);
            treat_as_playlist = task->is_playlist && task->total_playlist_items > 1;
            // Generate or preserve ID - check if item already exists in history
            auto existing_it = std::find_if(history_items_copy.begin(), history_items_copy.end(),
                [&task](const HistoryItem& item) { return item.url == task->url; });
            if (existing_it != history_items_copy.end() && !existing_it->id.empty()) {
                h.id = existing_it->id;  // Preserve existing ID
            } else {
                // Generate unique ID
                h.id = HistoryUtils::generateHistoryId(task->url, task);
            }
            h.url = task->url;
            h.status = task->status;
            h.timestamp = time(nullptr);
            h.platform = task->platform;
            h.filename = task->filename;
            h.title = task->metadata.title;
            h.artist = task->metadata.artist;
            h.duration = task->metadata.duration;
            h.bitrate = task->metadata.bitrate;
            h.file_size = task->file_size;
            h.is_playlist = treat_as_playlist;
            h.playlist_name = task->playlist_name;
            h.total_playlist_items = task->total_playlist_items;
            
            if (treat_as_playlist) {
                // Process playlist_item_file_paths while holding lock
                processPlaylistItemFilePaths(task);
                h.playlist_items = task->playlist_items;
            } else {
                ensureFilePath(this, task, settings_->downloads_dir);
                h.filepath = task->file_path;
            }
        }
        
        // Preserve thumbnail_base64 from existing history if available
        for (const auto& existing_item : history_items_copy) {
            if (existing_item.url == task->url && !existing_item.thumbnail_base64.empty()) {
                h.thumbnail_base64 = existing_item.thumbnail_base64;
                break;
            }
        }
        
        if (treat_as_playlist) {
            h.filepath = playlistFolderPath(this, task, settings_->downloads_dir, settings_->save_playlists_to_separate_folder);
            // Process playlist items to ensure file_size and bitrate are set (no mutex needed)
            processPlaylistItemsMetadata(h.playlist_items);
            // CRITICAL: Normalize all file paths in playlist items for Windows compatibility
            // Also extract filename from file_path for each item (same as for single files)
            // Get selected format from settings to determine if extension is final
            std::string selected_format = settings_ ? settings_->selected_format : "mp3";
            for (auto& item : h.playlist_items) {
                if (!item.file_path.empty()) {
                    item.file_path = PathUtils::normalizePath(item.file_path);
                    // Extract filename from file_path only if it's the final format
                    // Check if extension matches selected format (if opus is selected, .opus is final)
                    if (item.filename.empty()) {
                        std::string file_path = item.file_path;
                        size_t last_dot = file_path.find_last_of('.');
                        if (last_dot != std::string::npos) {
                            std::string ext = file_path.substr(last_dot);
                            // Check if extension matches selected format (final format)
                            bool is_final_format = (ext == "." + selected_format);
                            // Also allow common final formats even if not selected
                            if (!is_final_format) {
                                is_final_format = (ext == ".mp3" || ext == ".flac" || ext == ".m4a" || ext == ".ogg");
                            }
                            // If extension is temporary (not final), don't set filename yet
                            bool is_temporary = (!is_final_format && (ext == ".opus" || ext == ".webm" || 
                                                                       (ext == ".m4a" && selected_format != "m4a") ||
                                                                       (ext == ".ogg" && selected_format != "ogg")));
                            
                            if (!is_temporary) {
                                // Extension is final format, set filename
                                size_t last_slash = file_path.find_last_of("/\\");
                                if (last_slash != std::string::npos) {
                                    item.filename = file_path.substr(last_slash + 1);
                                } else {
                                    item.filename = file_path;
                                }
                            }
                        } else {
                            // No extension, set filename anyway
                            size_t last_slash = file_path.find_last_of("/\\");
                            if (last_slash != std::string::npos) {
                                item.filename = file_path.substr(last_slash + 1);
                            } else {
                                item.filename = file_path;
                            }
                        }
                    }
                }
            }
        } else {
            // Normalize filepath for single files
            if (!h.filepath.empty()) {
                h.filepath = PathUtils::normalizePath(h.filepath);
            }
            // CRITICAL: Reject temporary files (.part, .f*.part, etc.) to prevent them from being saved to history
            if (!h.filepath.empty() && ValidationUtils::isTemporaryFile(h.filepath)) {
                std::cout << "[DEBUG] addToHistory: Rejecting temporary file: " << h.filepath << std::endl;
                std::cout << "[DEBUG] addToHistory: Will wait for conversion to complete before saving to history" << std::endl;
                // Clear filepath to prevent saving incomplete file to history
                h.filepath.clear();
                // Need to update task->file_path, but we're outside lock - skip for now
                // The filepath is already cleared in h, which is what matters for history
            }
        }
        
        // For playlists, always add to history (even if filepath is empty, as it's a folder)
        // For single files, require filepath to be non-empty AND not a temporary file
        bool should_add = !h.url.empty() && (h.is_playlist || (!h.filepath.empty() && !ValidationUtils::isTemporaryFile(h.filepath)));
        
        std::cout << "[DEBUG] addToHistory: should_add=" << should_add 
                  << ", h.url.empty()=" << h.url.empty() 
                  << ", h.is_playlist=" << h.is_playlist 
                  << ", h.filepath.empty()=" << h.filepath.empty() 
                  << ", treat_as_playlist=" << treat_as_playlist
                  << ", task->is_playlist=" << task->is_playlist
                  << ", task->total_playlist_items=" << task->total_playlist_items << std::endl;
        
        if (should_add) {
            // Download thumbnail and convert to base64 asynchronously (for both single files and playlists)
            std::cout << "[DEBUG] addToHistory: URL=" << task->url << ", platform=" << task->platform << ", is_playlist=" << treat_as_playlist << std::endl;
            std::cout << "[DEBUG] addToHistory: task->thumbnail_url=" << (task->thumbnail_url.empty() ? "EMPTY" : task->thumbnail_url) << std::endl;
#ifdef _WIN32
            writeConsoleUtf8("[DEBUG] addToHistory: h.is_playlist=" + std::to_string(h.is_playlist) + ", h.filepath=" + (h.filepath.empty() ? "EMPTY" : h.filepath) + "\n");
#else
            std::cout << "[DEBUG] addToHistory: h.is_playlist=" << h.is_playlist << ", h.filepath=" << (h.filepath.empty() ? "EMPTY" : h.filepath) << std::endl;
#endif
            
            // First, add item to history (without thumbnail) to ensure it exists
            history_manager_->addHistoryItem(h);
            // CRITICAL: During shutdown, skip persistHistoryItems() to avoid blocking
            // History will be saved once at the end of cleanup()
            if (!shutting_down_) {
                history_manager_->persistHistoryItems();
                std::cout << "[DEBUG] addToHistory: Item added to history first (without thumbnail)" << std::endl;
            } else {
                std::cout << "[DEBUG] addToHistory: Item added to history (skipping persist during shutdown)" << std::endl;
            }
            
            // For playlists: verify data exists before attempting to save thumbnail
            bool can_save_thumbnail = true;
            if (h.is_playlist) {
                // Verify playlist has required data for thumbnail saving
                if (h.url.empty() || h.playlist_name.empty() || h.total_playlist_items == 0) {
                    std::cout << "[DEBUG] addToHistory: Playlist missing required data for thumbnail (url=" << h.url.empty() 
                              << ", playlist_name=" << h.playlist_name.empty() 
                              << ", total_items=" << h.total_playlist_items << "), skipping thumbnail only" << std::endl;
                    can_save_thumbnail = false;
                } else {
                    std::cout << "[DEBUG] addToHistory: Playlist data verified - URL=" << h.url 
                              << ", name=" << h.playlist_name 
                              << ", items=" << h.total_playlist_items << std::endl;
                }
            }
            
            if (!task->thumbnail_url.empty() && can_save_thumbnail) {
                // Capture values by copy to avoid issues with task pointer
                std::string url_copy = h.url;
                std::string thumbnail_url_copy = task->thumbnail_url;
                bool is_playlist_copy = h.is_playlist;
                
                runBackground([this, url_copy, thumbnail_url_copy, is_playlist_copy]() mutable {
                    // Skip thumbnail download during shutdown
                    if (shutting_down_) {
                        std::cout << "[DEBUG] addToHistory: Skipping thumbnail download (shutdown in progress)" << std::endl;
                        return;
                    }
                    
                    // Verify item exists in history before updating thumbnail
                    {
                        std::lock_guard<std::mutex> lock(tasks_mutex_);
                        if (!history_manager_) {
                            std::cout << "[DEBUG] addToHistory: history_manager_ is null, skipping thumbnail download" << std::endl;
                            return;
                        }
                        
                        // Check if thumbnail already exists in history to avoid duplicate downloads
                        const auto& history_items = getHistoryItems();
                        bool thumbnail_already_exists = false;
                        for (const auto& item : history_items) {
                            if (item.url == url_copy) {
                                if (!item.thumbnail_base64.empty()) {
                                    thumbnail_already_exists = true;
                                    std::cout << "[DEBUG] addToHistory: Thumbnail already exists in history for URL=" << url_copy << ", skipping duplicate download" << std::endl;
                                    break;
                                }
                            }
                        }
                        if (thumbnail_already_exists) {
                            return;
                        }
                        
                        // Verify the item exists in history before downloading thumbnail (for both single files and playlists)
                        bool found = false;
                        for (const auto& item : history_items) {
                            if (item.url == url_copy) {
                                if (is_playlist_copy) {
                                    // For playlists: verify required data exists
                                    if (item.url.empty() || item.playlist_name.empty() || item.total_playlist_items == 0) {
                                        std::cout << "[DEBUG] addToHistory: Playlist item found but missing required data, skipping thumbnail" << std::endl;
                                        return;
                                    }
                                    std::cout << "[DEBUG] addToHistory: Playlist item verified in history, proceeding with thumbnail download" << std::endl;
                                } else {
                                    // For single files: just verify URL matches
                                    std::cout << "[DEBUG] addToHistory: Single file item verified in history, proceeding with thumbnail download" << std::endl;
                                }
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            std::cout << "[DEBUG] addToHistory: Item not found in history, skipping thumbnail download" << std::endl;
                            return;
                        }
                    }
                    
                    // Now download thumbnail (without proxy - thumbnails should download directly)
                    // Method 1: Try simple URLDownloadToFile first, then fallback to WinHttp
                    std::cout << "[DEBUG] Downloading thumbnail from: " << thumbnail_url_copy << std::endl;
                    std::string thumbnail_base64 = ThumbnailDownloader::downloadThumbnailAsBase64(thumbnail_url_copy, false);
                    if (!thumbnail_base64.empty()) {
                        std::cout << "[DEBUG] Thumbnail downloaded and encoded, size: " << thumbnail_base64.length() << " bytes" << std::endl;
                        
                        // Update history item with thumbnail - use URL as unique identifier for reliable matching
                        {
                            std::lock_guard<std::mutex> lock(tasks_mutex_);
                            if (history_manager_) {
                                // Get existing item and update thumbnail_base64
                                // Use URL as unique identifier to ensure reliable matching
                                const auto& history_items = getHistoryItems();
                                bool thumbnail_updated = false;
                                for (const auto& item : history_items) {
                                    // CRITICAL: Match by URL exactly to ensure thumbnail is assigned to correct item
                                    if (item.url == url_copy) {
                                        // Additional verification for playlists
                                        if (is_playlist_copy) {
                                            if (!item.is_playlist || item.url.empty() || item.playlist_name.empty()) {
                                                std::cout << "[DEBUG] addToHistory: Playlist item mismatch, skipping thumbnail update for URL=" << url_copy << std::endl;
                                                continue;
                                            }
                                        } else {
                                            // For single files, verify it's not a playlist
                                            if (item.is_playlist) {
                                                std::cout << "[DEBUG] addToHistory: Item type mismatch (expected single, got playlist), skipping thumbnail update for URL=" << url_copy << std::endl;
                                                continue;
                                            }
                                        }
                                        
                                        HistoryItem updated_item = item;
                                        updated_item.thumbnail_base64 = thumbnail_base64;
                                        history_manager_->addHistoryItem(updated_item);
                                        thumbnail_updated = true;
                                        std::cout << "[DEBUG] addToHistory: Updated thumbnail_base64 for " << (is_playlist_copy ? "playlist" : "item") << " URL=" << url_copy << std::endl;
                                        break;
                                    }
                                }
                                if (!thumbnail_updated) {
                                    std::cout << "[DEBUG] addToHistory: WARNING - Could not find history item with URL=" << url_copy << " to update thumbnail" << std::endl;
                                }
                            }
                        }
                        // Persist outside the lock to avoid blocking UI thread
                        if (history_manager_) {
                            history_manager_->persistHistoryItems();
                            std::cout << "[DEBUG] History persisted with thumbnail_base64=SET" << std::endl;
                            // Rebuild history view to update UI with new thumbnail
                            rebuildHistoryViewTasks();
                        }
                    } else {
                        std::cout << "[DEBUG] Failed to download thumbnail" << std::endl;
                    }
                });
            } else {
                // No thumbnail URL, already added to history above
                std::cout << "[DEBUG] No thumbnail URL, item already added to history without thumbnail" << std::endl;
            }
            // addHistoryItem already calls rebuildHistoryViewTasks() internally
        }
    }
}


void App::cleanup() {
    std::cout << "[DEBUG] cleanup: Starting application cleanup..." << std::endl;
    shutting_down_ = true;
    
    // Start watchdog timer - force exit after 5 seconds if cleanup hangs
    std::thread watchdog_thread([this]() {
        const int CLEANUP_TIMEOUT_SECONDS = 5;
        std::cout << "[DEBUG] cleanup: Watchdog started (timeout: " << CLEANUP_TIMEOUT_SECONDS << "s)" << std::endl;
        
        for (int i = 0; i < CLEANUP_TIMEOUT_SECONDS * 10; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!shutting_down_) {
                // Cleanup completed normally
                std::cout << "[DEBUG] cleanup: Watchdog - cleanup completed normally" << std::endl;
                return;
            }
        }
        
        // Timeout reached - force exit
        std::cout << "[ERROR] cleanup: Watchdog timeout! Force terminating application..." << std::endl;
        std::_Exit(1);  // Force immediate exit without cleanup
    });
    watchdog_thread.detach();
    
    // IMMEDIATELY save history first (most important data)
    std::cout << "[DEBUG] cleanup: Saving history immediately..." << std::endl;
    if (history_manager_) {
        history_manager_->persistHistoryItems();
        std::cout << "[DEBUG] cleanup: History saved" << std::endl;
    }
    
    // MetadataManager will stop its worker in its destructor

    // CRITICAL: Notify service checker about shutdown FIRST to stop any running checks
    // This should be done early to prevent new processes from starting
    if (service_checker_) {
        std::cout << "[DEBUG] cleanup: Notifying ServiceChecker about shutdown..." << std::endl;
        service_checker_->setShuttingDown(true);
    }
    
    // Join ad-hoc background threads (with timeout to avoid blocking)
    std::cout << "[DEBUG] cleanup: Joining background threads..." << std::endl;
    joinBackgroundThreads();
    std::cout << "[DEBUG] cleanup: Background threads joined/detached" << std::endl;
    
    // Save settings (may have been changed and not saved yet)
    std::cout << "[DEBUG] cleanup: Saving settings..." << std::endl;
    saveSettings();
    std::cout << "[DEBUG] cleanup: Settings saved" << std::endl;
    
    // Force terminate any remaining processes
    if (service_checker_) {
        std::cout << "[DEBUG] cleanup: Force terminating any remaining yt-dlp processes..." << std::endl;
        service_checker_->terminateActiveProcess();
        std::cout << "[DEBUG] cleanup: Process termination completed" << std::endl;
    }
    
    // Cleanup WindowManager (handles ImGui and SDL cleanup)
    std::cout << "[DEBUG] cleanup: Cleaning up WindowManager..." << std::endl;
    if (window_manager_) {
        window_manager_->cleanup();
    }
    std::cout << "[DEBUG] cleanup: WindowManager cleaned up" << std::endl;
    
    std::cout << "[DEBUG] cleanup: Application cleanup completed" << std::endl;
    
    // Signal watchdog that cleanup completed
    shutting_down_ = false;
    
    // Clear pointers
    // window_ and renderer_ removed - using window_manager_->getWindow()/getRenderer() directly
}


void App::createDebugTestData() {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    
    // Create test single file (completed)
    {
        auto task = std::make_unique<DownloadTask>("https://www.youtube.com/watch?v=TEST_SINGLE_FILE");
        task->platform = "YouTube";
        task->status = "completed";
        task->progress = 1.0f;
        task->filename = "Test Song - Artist Name.mp3";
        task->file_path = settings_->downloads_dir + "/Test Song - Artist Name.mp3";
        task->file_size = 5242880; // 5 MB
        task->is_playlist = false;
        task->metadata_loaded = true;
        task->metadata.title = "Test Song";
        task->metadata.artist = "Artist Name";
        task->metadata.duration = 240; // 4 minutes
        task->metadata.bitrate = 320;
        
        tasks_.push_back(std::move(task));
    }
    
    // Create test playlist with 5 items (completed)
    {
        auto task = std::make_unique<DownloadTask>("https://www.youtube.com/playlist?list=TEST_PLAYLIST");
        task->platform = "YouTube";
        task->status = "completed";
        task->progress = 1.0f;
        task->is_playlist = true;
        task->total_playlist_items = 5;
        task->current_playlist_item = -1; // All items completed
        task->current_item_title = "";
        task->playlist_name = "Test Playlist"; // Set playlist name for "Open Folder" button
        
        // Initialize playlist items
        task->playlist_items.resize(5);
        std::vector<std::string> test_titles = {
            "First Track - Artist One",
            "Second Track - Artist Two",
            "Third Track - Artist Three",
            "Fourth Track - Artist Four",
            "Fifth Track - Artist Five"
        };
        
        for (int i = 0; i < 5; i++) {
            task->playlist_items[i].index = i;
            task->playlist_items[i].title = test_titles[i];
            task->playlist_items[i].downloaded = true;
            
            // Set file paths
            std::string file_path = settings_->downloads_dir;
            if (settings_->save_playlists_to_separate_folder && !task->playlist_name.empty()) {
                std::string folder_name = sanitizeFilename(task->playlist_name);
                file_path += "/" + folder_name;
            }
            file_path += "/" + test_titles[i] + ".mp3";
            task->playlist_item_file_paths[i] = file_path;
        }
        
        // Set metadata for the playlist task
        task->metadata_loaded = true;
        task->metadata.title = "Test Playlist";
        task->metadata.artist = "Various Artists";
        task->metadata.duration = 1200; // 20 minutes total
        task->metadata.bitrate = 320;
        
        tasks_.push_back(std::move(task));
    }
}

