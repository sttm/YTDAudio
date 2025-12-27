#include "settings.h"
#include "platform/platform_utils.h"
#include "common/browser_utils.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <climits>

// Helper function to parse boolean value
static bool parseBool(const std::string& value) {
    return (value == "1" || value == "true");
}

// Helper function to parse integer with bounds
static int parseInt(const std::string& value, int min_val = 0, int max_val = INT_MAX) {
    try {
        int val = std::stoi(value);
        return std::max(min_val, std::min(max_val, val));
    } catch (...) {
        return min_val;
    }
}

Settings::Settings() {
    loadDefaults();
}

Settings::~Settings() {
}

void Settings::loadDefaults() {
    downloads_dir = ".";
    selected_format = "mp3";
    selected_quality = "best";
    use_proxy = false;
    proxy_input = "";
    save_playlists_to_separate_folder = true;  // Save playlists to separate folder by default
    show_settings_panel = false;
    
    // Initialize API keys
    spotify_api_key[0] = '\0';
    youtube_api_key[0] = '\0';
    soundcloud_api_key[0] = '\0';
    
    // yt-dlp defaults
    ytdlp_use_sleep_intervals_playlist = false;
    ytdlp_use_cookies_for_playlists = false;
    ytdlp_use_cookies_file = false;
    ytdlp_cookies_file_path = "";
    ytdlp_use_sleep_requests = false;
    ytdlp_playlist_sleep_interval = 1;
    ytdlp_playlist_max_sleep_interval = 1;
    ytdlp_playlist_sleep_requests = 1;
    ytdlp_selected_browser_index = 0;
    ytdlp_use_socket_timeout = false;
    ytdlp_socket_timeout = 120;
    ytdlp_use_fragment_retries = false;
    ytdlp_fragment_retries = 10;
    ytdlp_use_concurrent_fragments = false;
    ytdlp_concurrent_fragments = 2;
    // Removed: ytdlp_single_video_sleep_interval (no longer used)
    
    ytdlp_version = "";
    ytdlp_version_present = false;
}

std::string Settings::getConfigPath() const {
    return PlatformUtils::getConfigPath();
}

void Settings::load() {
    std::string config_path = getConfigPath();
    std::cout << "[DEBUG] Settings::load: Loading settings from: " << config_path << std::endl;
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cout << "[DEBUG] Settings::load: Config file not found, using defaults" << std::endl;
        return;
    }
    std::cout << "[DEBUG] Settings::load: Config file opened successfully" << std::endl;
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;
        
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Helper lambda to safely copy string to char array
        auto copyToCharArray = [](const std::string& src, char* dst, size_t max_len) {
            size_t len = std::min(src.length(), max_len - 1);
            src.copy(dst, len);
            dst[len] = '\0';
        };
        
        if (key == "format") {
            selected_format = value;
        } else if (key == "quality") {
            selected_quality = value;
        } else if (key == "use_proxy") {
            use_proxy = parseBool(value);
        } else if (key == "proxy") {
            proxy_input = value;
        } else if (key == "downloads_dir") {
            downloads_dir = value;
        } else if (key == "spotify_api_key") {
            copyToCharArray(value, spotify_api_key, sizeof(spotify_api_key));
        } else if (key == "youtube_api_key") {
            copyToCharArray(value, youtube_api_key, sizeof(youtube_api_key));
        } else if (key == "soundcloud_api_key") {
            copyToCharArray(value, soundcloud_api_key, sizeof(soundcloud_api_key));
        } else if (key == "save_playlists_to_separate_folder") {
            save_playlists_to_separate_folder = parseBool(value);
        } else if (key == "ytdlp_version") {
            ytdlp_version = value;
            ytdlp_version_present = !ytdlp_version.empty();
        } else if (key == "ytdlp_use_sleep_intervals_playlist") {
            ytdlp_use_sleep_intervals_playlist = parseBool(value);
        } else if (key == "ytdlp_use_cookies_for_playlists") {
            ytdlp_use_cookies_for_playlists = parseBool(value);
        } else if (key == "ytdlp_use_cookies_file") {
            ytdlp_use_cookies_file = parseBool(value);
        } else if (key == "ytdlp_cookies_file_path") {
            ytdlp_cookies_file_path = value;
        } else if (key == "ytdlp_use_sleep_requests") {
            ytdlp_use_sleep_requests = parseBool(value);
        } else if (key == "ytdlp_playlist_sleep_interval") {
            ytdlp_playlist_sleep_interval = parseInt(value, 0);
        } else if (key == "ytdlp_playlist_max_sleep_interval") {
            ytdlp_playlist_max_sleep_interval = parseInt(value, 0);
        } else if (key == "ytdlp_playlist_sleep_requests") {
            ytdlp_playlist_sleep_requests = parseInt(value, 0);
        } else if (key == "ytdlp_selected_browser_index") {
            ytdlp_selected_browser_index = parseInt(value, 0);
        } else if (key == "ytdlp_use_socket_timeout") {
            ytdlp_use_socket_timeout = parseBool(value);
        } else if (key == "ytdlp_socket_timeout") {
            ytdlp_socket_timeout = parseInt(value, 10, 600);
        } else if (key == "ytdlp_use_fragment_retries") {
            ytdlp_use_fragment_retries = parseBool(value);
        } else if (key == "ytdlp_fragment_retries") {
            ytdlp_fragment_retries = parseInt(value, 1, 50);
        } else if (key == "ytdlp_use_concurrent_fragments") {
            ytdlp_use_concurrent_fragments = parseBool(value);
        } else if (key == "ytdlp_concurrent_fragments") {
            ytdlp_concurrent_fragments = parseInt(value, 1, 4);
        }
    }
    std::cout << "[DEBUG] Settings::load: Settings loaded successfully" << std::endl;
}

void Settings::save() {
    std::string config_path = getConfigPath();
    std::ofstream file(config_path);
    if (!file.is_open()) {
        return;
    }
    
    file << "# YTDAudio Configuration\n";
    file << "# This file is automatically generated\n\n";
    
    // Helper lambda to write boolean values
    auto writeBool = [&file](const std::string& key, bool value) {
        file << key << "=" << (value ? "1" : "0") << "\n";
    };
    
    file << "format=" << selected_format << "\n";
    file << "quality=" << selected_quality << "\n";
    writeBool("use_proxy", use_proxy);
    file << "proxy=" << proxy_input << "\n";
    file << "downloads_dir=" << downloads_dir << "\n";
    file << "spotify_api_key=" << spotify_api_key << "\n";
    file << "youtube_api_key=" << youtube_api_key << "\n";
    file << "soundcloud_api_key=" << soundcloud_api_key << "\n";
    writeBool("save_playlists_to_separate_folder", save_playlists_to_separate_folder);
    
    if (!ytdlp_version.empty()) {
        file << "ytdlp_version=" << ytdlp_version << "\n";
    }
    
    // yt-dlp settings
    writeBool("ytdlp_use_sleep_intervals_playlist", ytdlp_use_sleep_intervals_playlist);
    writeBool("ytdlp_use_cookies_for_playlists", ytdlp_use_cookies_for_playlists);
    writeBool("ytdlp_use_cookies_file", ytdlp_use_cookies_file);
    file << "ytdlp_cookies_file_path=" << ytdlp_cookies_file_path << "\n";
    writeBool("ytdlp_use_sleep_requests", ytdlp_use_sleep_requests);
    file << "ytdlp_playlist_sleep_interval=" << ytdlp_playlist_sleep_interval << "\n";
    file << "ytdlp_playlist_max_sleep_interval=" << ytdlp_playlist_max_sleep_interval << "\n";
    file << "ytdlp_playlist_sleep_requests=" << ytdlp_playlist_sleep_requests << "\n";
    file << "ytdlp_selected_browser_index=" << ytdlp_selected_browser_index << "\n";
    writeBool("ytdlp_use_socket_timeout", ytdlp_use_socket_timeout);
    file << "ytdlp_socket_timeout=" << ytdlp_socket_timeout << "\n";
    writeBool("ytdlp_use_fragment_retries", ytdlp_use_fragment_retries);
    file << "ytdlp_fragment_retries=" << ytdlp_fragment_retries << "\n";
    writeBool("ytdlp_use_concurrent_fragments", ytdlp_use_concurrent_fragments);
    file << "ytdlp_concurrent_fragments=" << ytdlp_concurrent_fragments << "\n";
}

YtDlpSettings Settings::createYtDlpSettings() const {
    YtDlpSettings settings;
    settings.use_sleep_intervals_playlist = ytdlp_use_sleep_intervals_playlist;
    settings.use_cookies_for_playlists = ytdlp_use_cookies_for_playlists;
    settings.use_cookies_file = ytdlp_use_cookies_file;
    settings.cookies_file_path = ytdlp_cookies_file_path;
    settings.use_sleep_requests = ytdlp_use_sleep_requests;
    settings.playlist_sleep_interval = ytdlp_playlist_sleep_interval;
    settings.playlist_max_sleep_interval = ytdlp_playlist_max_sleep_interval;
    settings.playlist_sleep_requests = ytdlp_playlist_sleep_requests;
    settings.use_socket_timeout = ytdlp_use_socket_timeout;
    settings.socket_timeout = ytdlp_socket_timeout;
    settings.use_fragment_retries = ytdlp_use_fragment_retries;
    settings.fragment_retries = ytdlp_fragment_retries;
    settings.use_concurrent_fragments = ytdlp_use_concurrent_fragments;
    settings.concurrent_fragments = ytdlp_concurrent_fragments;
    
    // Get selected browser name from index using BrowserUtils
    if (ytdlp_selected_browser_index >= 0 && ytdlp_selected_browser_index < BrowserUtils::getBrowserCount()) {
        settings.selected_browser = BrowserUtils::getBrowserName(ytdlp_selected_browser_index);
    }
    
    return settings;
}

