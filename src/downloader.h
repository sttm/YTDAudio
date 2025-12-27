#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>

// yt-dlp settings structure
struct YtDlpSettings {
    bool use_sleep_intervals_playlist = false;
    bool use_cookies_for_playlists = false;
    bool use_cookies_file = false;  // Use cookies file instead of browser
    std::string cookies_file_path;  // Path to cookies file
    bool use_sleep_requests = false;
    int playlist_sleep_interval = 1;
    int playlist_max_sleep_interval = 1;
    int playlist_sleep_requests = 1;
    std::string selected_browser = "firefox";  // Browser name for cookies
    bool use_socket_timeout = false;  // Whether to use custom socket timeout
    int socket_timeout = 120;  // Socket timeout in seconds for downloads
    bool use_fragment_retries = false;  // Whether to use custom fragment retries
    int fragment_retries = 10;  // Number of retries for HLS fragments
    bool use_concurrent_fragments = false;  // Whether to use concurrent fragments
    int concurrent_fragments = 2;  // Number of parallel fragments for HLS downloads (default: 2, max: 4)
};

class Downloader {
public:
    struct ProgressInfo {
        float progress;      // 0.0 to 1.0
        std::string status;   // Current status message
        int64_t downloaded;  // Bytes downloaded
        int64_t total;        // Total bytes
        int speed;            // Download speed in bytes/sec
        
        // Playlist information
        bool is_playlist;
        int current_item_index;  // Current item being downloaded (0-based)
        int total_items;         // Total items in playlist
        std::string current_item_title;  // Title of current item
        std::string playlist_name;  // Name of the playlist (e.g., "Butterfly Dawn")
        std::string thumbnail_url;  // Thumbnail URL from first playlist item
        
        // Metadata for current item
        int duration;  // Duration in seconds
        int bitrate;   // Bitrate in kbps
        std::string current_file_path;  // File path for current playlist item
        
        ProgressInfo() : progress(0.0f), downloaded(0), total(0), speed(0), 
                        is_playlist(false), current_item_index(-1), total_items(0),
                        duration(0), bitrate(0) {}
    };
    
    struct VideoInfo {
        std::string title;
        std::string artist;
        std::string album;
        std::string duration;
        std::string filename;
        std::string filepath;
        std::string thumbnail_url;  // URL of thumbnail image
        int bitrate;  // in kbps
        VideoInfo() : bitrate(0) {}
    };
    
    using ProgressCallback = std::function<void(const ProgressInfo&)>;
    using CompleteCallback = std::function<void(const std::string& file_path, const std::string& error)>;
    
    Downloader();
    ~Downloader();
    
    // Start download in background thread
    void downloadAsync(
        const std::string& url,
        const std::string& output_dir,
        const std::string& format,
        const std::string& quality,
        const std::string& proxy,
        const std::string& spotify_api_key,
        const std::string& youtube_api_key,
        const std::string& soundcloud_api_key,
        bool download_playlist,
        ProgressCallback progress_cb,
        CompleteCallback complete_cb,
        const YtDlpSettings& settings = YtDlpSettings(),
        const std::string& playlist_items = ""  // Optional: specific playlist items to download (e.g., "1,3,5" or "11")
    );
    
    // Cancel current download
    void cancel();
    
    // Check if yt-dlp is available
    static bool checkYtDlpAvailable();
    
    // Get yt-dlp version
    static std::string getYtDlpVersion();
    
    // Update yt-dlp in place (e.g. bundle or system) using `yt-dlp -U`
    // Returns true on success and fills log_output with combined stdout/stderr
    static bool updateYtDlp(std::string& log_output);
    
    // Find yt-dlp executable path (checks bundle first, then system)
    static std::string findYtDlpPath();
    
    // Get expected filename from URL (without downloading)
    static std::string getExpectedFilename(
        const std::string& url,
        const std::string& output_dir,
        const std::string& format
    );
    
    // Get video info using --print-json (for metadata and accurate file path)
    static VideoInfo getVideoInfo(
        const std::string& url,
        const std::string& output_dir,
        const std::string& format,
        const std::string& proxy = "",
        const YtDlpSettings& settings = YtDlpSettings()
    );
    
    // Get playlist items info BEFORE downloading (using yt-dlp JSON output)
    struct PlaylistItemInfo {
        std::string title;
        std::string id;
        std::string url;
        int index;
        int duration;               // Duration in seconds (if available)
        std::string duration_string; // Human-readable duration (e.g. "4:00")
        PlaylistItemInfo() : index(-1), duration(0) {}
    };
    
    // Result structure for getPlaylistItems - includes both items and playlist name
    struct PlaylistInfo {
        std::vector<PlaylistItemInfo> items;
        std::string playlist_name;
        std::string thumbnail_url;  // Thumbnail URL from first playlist item
        std::string error_message;  // Error message from yt-dlp (if any)
        PlaylistInfo() {}
    };
    
    static PlaylistInfo getPlaylistItems(
        const std::string& url,
        const std::string& proxy,
        const YtDlpSettings& settings = YtDlpSettings()
    );
    
    // Get playlist name/title BEFORE downloading (deprecated - use getPlaylistItems instead)
    static std::string getPlaylistName(
        const std::string& url,
        const std::string& proxy
    );
    
private:
    std::thread download_thread_;
    std::shared_ptr<std::atomic<bool>> cancel_flag_;  // Shared pointer to allow safe access from detached threads
    
    void downloadThread(
        const std::string& url,
        const std::string& output_dir,
        const std::string& format,
        const std::string& quality,
        const std::string& proxy,
        const std::string& spotify_api_key,
        const std::string& youtube_api_key,
        const std::string& soundcloud_api_key,
        bool download_playlist,
        ProgressCallback progress_cb,
        CompleteCallback complete_cb,
        const YtDlpSettings& settings,
        const std::string& playlist_items,
        std::shared_ptr<std::atomic<bool>> cancel_flag
    );
    
    ProgressInfo parseProgress(const std::string& line);
    ProgressInfo parseJsonProgress(const std::string& json_line);  // Parse JSON event from yt-dlp
    int64_t parseSizeUnit(const std::string& unit);
    std::string buildYtDlpCommand(
        const std::string& url,
        const std::string& output_dir,
        const std::string& format,
        const std::string& quality,
        const std::string& proxy,
        const std::string& spotify_api_key,
        const std::string& youtube_api_key,
        const std::string& soundcloud_api_key,
        bool download_playlist,
        const YtDlpSettings& settings = YtDlpSettings(),
        const std::string& playlist_items = ""  // Optional: specific playlist items to download (e.g., "1,3,5" or "11")
    );
    
    // Build yt-dlp command as a vector of arguments (for use with CreateProcessW on Windows)
    // This avoids all cmd.exe escaping issues
    std::vector<std::string> buildYtDlpArguments(
        const std::string& url,
        const std::string& output_dir,
        const std::string& format,
        const std::string& quality,
        const std::string& proxy,
        const std::string& spotify_api_key,
        const std::string& youtube_api_key,
        const std::string& soundcloud_api_key,
        bool download_playlist,
        const YtDlpSettings& settings = YtDlpSettings(),
        const std::string& playlist_items = ""  // Optional: specific playlist items to download
    );
};

