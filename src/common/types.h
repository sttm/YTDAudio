#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>

struct AudioMetadata {
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    int year;
    int track;
    int duration;  // in seconds
    int bitrate;   // in kbps
    
    AudioMetadata() : year(0), track(0), duration(0), bitrate(0) {}
};

struct PlaylistItem {
    std::string title;
    std::string url;
    std::string id;  // Unique identifier (video ID, etc.)
    int index;
    bool downloaded;
    std::string file_path;  // Path to downloaded file
    std::string filename;   // Filename extracted from file_path (same as for single files)
    int duration;  // Duration in seconds
    int bitrate;  // Bitrate in kbps
    int64_t file_size;  // File size in bytes
    
    PlaylistItem() : index(0), downloaded(false), duration(0), bitrate(0), file_size(0) {}
};

struct HistoryItem {
    std::string id;  // Unique identifier for reliable deletion (UUID or timestamp-based)
    std::string url;
    std::string status;
    std::string filename;
    std::string filepath;
    std::string title;
    std::string artist;
    std::string platform;
    bool is_playlist = false;
    std::string playlist_name;
    int total_playlist_items = 0;
    int duration = 0;
    int bitrate = 0;
    int64_t file_size = 0;
    int64_t timestamp = 0;
    std::string thumbnail_base64;  // Base64 encoded thumbnail image
    std::vector<PlaylistItem> playlist_items;
};

// Forward declaration
class Downloader;

struct DownloadTask {
    std::string url;
    std::string platform;
    std::string status;  // "queued", "downloading", "completed", "error", "already_exists", "cancelled"
    float progress;      // 0.0 to 1.0
    std::string filename;
    std::string error_message;
    std::string file_path;
    int64_t file_size;
    AudioMetadata metadata;
    bool metadata_loaded;
    Downloader* downloader_ptr;  // Pointer to downloader for cancellation
    
    // Playlist information
    bool is_playlist;
    std::vector<PlaylistItem> playlist_items;
    int current_playlist_item;  // Index of currently downloading item (-1 if not downloading playlist)
    int total_playlist_items;
    std::string current_item_title;  // Title of currently downloading item
    std::string playlist_name;  // Name of the playlist (e.g., "Butterfly Dawn")
    std::map<int, std::string> playlist_item_renames;  // Map of item index to custom name
    std::map<int, std::string> playlist_item_file_paths;  // Map of item index to file path
    
    // Thumbnail
    std::string thumbnail_url;  // URL of thumbnail image
    
    // Time tracking for timeout
    std::chrono::steady_clock::time_point created_at;  // When task was created
    
    DownloadTask(const std::string& u) 
        : url(u), status("queued"), progress(0.0f), file_size(0), metadata_loaded(false), 
          downloader_ptr(nullptr), is_playlist(false), current_playlist_item(-1), 
          total_playlist_items(0), created_at(std::chrono::steady_clock::now()) {}
};

