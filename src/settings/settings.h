#pragma once

#include <string>
#include <chrono>
#include "../downloader.h"

class Settings {
public:
    Settings();
    ~Settings();
    
    // Load settings from config file
    void load();
    
    // Save settings to config file
    void save();
    
    // Get config file path
    std::string getConfigPath() const;
    
    // Download settings
    std::string downloads_dir;
    std::string selected_format;  // "mp3", "m4a", "flac"
    std::string selected_quality; // "best", "320k", "256k", "192k", "128k"
    bool use_proxy;
    std::string proxy_input;
    bool save_playlists_to_separate_folder;
    
    // API keys
    char spotify_api_key[256];
    char youtube_api_key[256];
    char soundcloud_api_key[256];
    
    // yt-dlp configuration settings
    bool ytdlp_use_sleep_intervals_playlist;
    bool ytdlp_use_cookies_for_playlists;
    bool ytdlp_use_cookies_file;
    std::string ytdlp_cookies_file_path;
    bool ytdlp_use_sleep_requests;
    int ytdlp_playlist_sleep_interval;
    int ytdlp_playlist_max_sleep_interval;
    int ytdlp_playlist_sleep_requests;
    int ytdlp_selected_browser_index;
    bool ytdlp_use_socket_timeout;
    int ytdlp_socket_timeout;
    bool ytdlp_use_fragment_retries;
    int ytdlp_fragment_retries;
    bool ytdlp_use_concurrent_fragments;
    int ytdlp_concurrent_fragments;
    
    // UI settings
    bool show_settings_panel;
    
    // yt-dlp version cache
    std::string ytdlp_version;
    bool ytdlp_version_present;
    
    // Create YtDlpSettings from current settings
    YtDlpSettings createYtDlpSettings() const;
    
private:
    void loadDefaults();
};

