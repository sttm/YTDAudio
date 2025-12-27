#include "playlist_detector.h"
#include <algorithm>

PlaylistDetector::PlaylistInfo PlaylistDetector::detectFromUrl(const std::string& url, const std::string& platform) {
    PlaylistInfo info;
    info.platform = platform;
    
    std::string url_lower = url;
    std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);
    
    if (platform == "SoundCloud") {
        // For SoundCloud, check sets/ only in path (not query parameters)
        info.is_soundcloud_set = isSetsInPath(url);
        info.is_playlist = info.is_soundcloud_set;
    } else if (platform == "YouTube") {
        info.is_youtube_playlist = isYouTubePlaylist(url);
        info.is_playlist = info.is_youtube_playlist;
    } else {
        // Generic check
        info.is_playlist = isPlaylistUrl(url, platform);
    }
    
    return info;
}

bool PlaylistDetector::isSoundCloudSet(const std::string& url) {
    return isSetsInPath(url);
}

bool PlaylistDetector::isYouTubePlaylist(const std::string& url) {
    std::string url_lower = url;
    std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);
    return url_lower.find("list=") != std::string::npos;
}

bool PlaylistDetector::isPlaylistUrl(const std::string& url, const std::string& platform) {
    if (platform == "SoundCloud") {
        return isSoundCloudSet(url);
    } else if (platform == "YouTube") {
        return isYouTubePlaylist(url);
    }
    
    // Generic check
    std::string url_lower = url;
    std::transform(url_lower.begin(), url_lower.end(), url_lower.begin(), ::tolower);
    return url_lower.find("list=") != std::string::npos || 
           url_lower.find("playlist") != std::string::npos;
}

bool PlaylistDetector::looksLikePlaylist(const std::string& url, const std::string& platform) {
    return isPlaylistUrl(url, platform);
}

bool PlaylistDetector::isSetsInPath(const std::string& url) {
    // Use UrlParser to check sets/ only in path (before '?')
    // Query parameters like "?in=user/sets/name" don't indicate a playlist
    return UrlParser::hasSetsInPath(url);
}

