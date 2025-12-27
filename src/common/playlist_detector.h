#ifndef PLAYLIST_DETECTOR_H
#define PLAYLIST_DETECTOR_H

#include <string>
#include "url_parser.h"

class PlaylistDetector {
public:
    struct PlaylistInfo {
        bool is_playlist;
        bool is_soundcloud_set;
        bool is_youtube_playlist;
        std::string platform;
        
        PlaylistInfo() : is_playlist(false), is_soundcloud_set(false), is_youtube_playlist(false) {}
    };
    
    // Detect playlist from URL and platform
    static PlaylistInfo detectFromUrl(const std::string& url, const std::string& platform);
    
    // Platform-specific detection
    static bool isSoundCloudSet(const std::string& url);
    static bool isYouTubePlaylist(const std::string& url);
    static bool isPlaylistUrl(const std::string& url, const std::string& platform);
    
    // Check if URL looks like a playlist (quick check before async loading)
    static bool looksLikePlaylist(const std::string& url, const std::string& platform);
    
private:
    // Helper: Check if sets/ is in URL path (not query parameters)
    static bool isSetsInPath(const std::string& url);
};

#endif // PLAYLIST_DETECTOR_H

