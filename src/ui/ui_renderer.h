#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include "../common/types.h"

// Forward declaration
class App;

class UIRenderer {
public:
    UIRenderer(App* app_context);
    ~UIRenderer();
    
    // Main rendering functions
    void renderUI();
    void renderDownloadList();
    void renderSettings();
    void renderProgressBar(float progress, const std::string& status);
    
    // Icon drawing functions
    void drawYouTubeIcon(const ImVec2& pos, float size);
    void drawSoundCloudIcon(const ImVec2& pos, float size);
    
private:
    App* app_context_;  // Pointer to App for accessing data
    
    // Thumbnail sizing constants
    static constexpr float THUMBNAIL_WIDTH_SINGLE = 70.0f;      // Width for single file thumbnails
    static constexpr float THUMBNAIL_WIDTH_PLAYLIST = 70.0f;    // Width for playlist thumbnails
    static constexpr float THUMBNAIL_MIN_HEIGHT = 50.0f;        // Minimum height for thumbnails
    static constexpr float THUMBNAIL_MAX_HEIGHT = 80.0f;        // Maximum height for thumbnails
    
    // Helper functions
    ImVec4 getPlatformColor(const std::string& platform);
    void drawPlatformIconInline(const std::string& platform);
    // formatFileSize and formatDuration moved to AudioUtils namespace
    std::string truncateUrl(const std::string& url, size_t max_len = 50);
    
    // Thumbnail loading and caching
    struct ThumbnailData {
        void* texture;  // SDL_Texture*
        int width;
        int height;
    };
    ThumbnailData* loadThumbnailFromBase64(const std::string& base64_data);
    void drawThumbnail(const std::string& thumbnail_base64, float max_width, float column_height, const std::string& platform = "");
    
    // Cache for thumbnails
    std::map<std::string, ThumbnailData> thumbnail_cache_;  // Maps base64 hash to ThumbnailData
    std::mutex thumbnail_cache_mutex_;  // Protect thumbnail cache access
};

