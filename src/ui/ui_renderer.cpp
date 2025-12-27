#include "ui_renderer.h"
#include "../app.h"
#include "../platform/platform_utils.h"
#include "../history/history_manager.h"
#include "../common/validation_utils.h"
#include "../common/browser_utils.h"
#include "../common/audio_utils.h"
#include "../common/windows_utils.h"
#include "../settings/settings.h"
#include "../window/window_manager.h"
#include "../service/service_checker.h"
#include "../platform/path_finder.h"
#include <SDL.h>
#ifdef SDL_IMAGE_FOUND
#include <SDL_image.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <map>
#include <iostream>

// Placeholder thumbnail base64 (small gray image)
// Used when real thumbnail is not available
static const std::string PLACEHOLDER_THUMBNAIL_BASE64 = 
    "/9j/4AAQSkZJRgABAQEASABIAAD/2wBDAAoHBwgHBgoICAgLCgoLDhgQDg0NDh0VFhEYIx8lJCIfIiEmKzcvJik0KSEiMEExNDk7Pj4+JS5ESUM8SDc9Pjv/2wBDAQoLCw4NDhwQEBw7KCIoOzs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozv/wAARCAA8ADwDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDxmiiigAooqR7eaOJJXjZUf7rEcGgCOiiigAooooAK0dMjgjgmvbmISpGVRUJwCSf8M0+WysJYJ/scshktl3MXxiQdyPSmY2+Hs/3rkZ/BTQBDqFotvqDwRElCQU+h5Fal7L58eoWuSUttvlj024U4qK5QPrGnd96RZ/PFNQl73VB/eR//AEIUAULKwmvpCkW0BRlmY4A+pqGeCS2maGUYdDgirsT+XoM2ODLMFJ9gM0/Uo/OubTHWaGPJ9+lAGZRW3LbWMz3VlDbhJLZSUlBJLleuay4bG6uELwwO6g4yozzQBJpt2lpcEyoXikUpIo6kGpL66tzbQ2loXMUZLMzjBZj7VQooA6C3ha5udLux/qo0xI2Pu7CSc/hiqmmuJr65HUyxvj+f9KoR3lxFbvBHMyxSfeUHg0tjc/Y7yOfGQp5HqCMH9DQBZcbNBjGOXuCfyAq1a3FlLFa3FxLsksxho8cygdAKqand283lQ2gcQRA439SScmqFAFgXsyXrXaNtkLFuPerQ8QaggxFKsK/3Y1AFZtFABRRRQAUUUUAFFFFABRRRQB//2Q==";

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include "../common/platform_macros.h"
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <unistd.h>  // For access() and F_OK
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

UIRenderer::UIRenderer(App* app_context) : app_context_(app_context) {
}

UIRenderer::~UIRenderer() {
    // Clean up thumbnail cache
    for (auto& pair : thumbnail_cache_) {
        if (pair.second.texture) {
            SDL_DestroyTexture(static_cast<SDL_Texture*>(pair.second.texture));
        }
    }
    thumbnail_cache_.clear();
}

ImVec4 UIRenderer::getPlatformColor(const std::string& platform) {
    if (platform == "YouTube") return ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    if (platform == "SoundCloud") return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
    if (platform == "Spotify") return ImVec4(0.2f, 0.8f, 0.4f, 1.0f);
    return ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
}

void UIRenderer::drawPlatformIconInline(const std::string& platform) {
    if (platform == "YouTube") {
        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        float text_height = ImGui::GetTextLineHeight();
        float icon_size = text_height * 0.8f;
        // Align icon vertically with text baseline
        float text_y_offset = (text_height - icon_size) * 0.5f;
        cursor_pos.y = cursor_pos.y + text_y_offset + 2.0f;
        drawYouTubeIcon(cursor_pos, icon_size);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon_size + 6.0f);
    } else if (platform == "SoundCloud") {
        ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
        float text_height = ImGui::GetTextLineHeight();
        float icon_size = text_height * 0.8f;
        // Align icon vertically with text baseline
        float text_y_offset = (text_height - icon_size) * 0.5f;
        cursor_pos.y = cursor_pos.y + text_y_offset + 2.0f;
        drawSoundCloudIcon(cursor_pos, icon_size);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon_size + 6.0f);
    }
}

// formatFileSize and formatDuration moved to AudioUtils namespace

std::string UIRenderer::truncateUrl(const std::string& url, size_t max_len) {
    if (url.length() <= max_len) return url;
    if (max_len < 4) return url.substr(0, max_len);
    return url.substr(0, max_len - 3) + "...";
}

void UIRenderer::drawYouTubeIcon(const ImVec2& pos, float size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // YouTube icon: Red rounded rectangle with white play triangle
    float rounding = size * 0.15f; // Rounded corners
    ImVec2 rect_min = pos;
    ImVec2 rect_max = ImVec2(pos.x + size, pos.y + size * 0.75f); // 4:3 aspect ratio
    
    // Draw red rounded rectangle background
    draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(255, 0, 0, 255), rounding);
    
    // Draw white play triangle (centered)
    float triangle_size = size * 0.35f;
    ImVec2 center = ImVec2((rect_min.x + rect_max.x) * 0.5f, (rect_min.y + rect_max.y) * 0.5f);
    ImVec2 p1 = ImVec2(center.x - triangle_size * 0.4f, center.y - triangle_size * 0.5f);
    ImVec2 p2 = ImVec2(center.x - triangle_size * 0.4f, center.y + triangle_size * 0.5f);
    ImVec2 p3 = ImVec2(center.x + triangle_size * 0.6f, center.y);
    
    ImVec2 triangle[3] = { p1, p2, p3 };
    draw_list->AddTriangleFilled(p1, p2, p3, IM_COL32(255, 255, 255, 255));
}

void UIRenderer::drawSoundCloudIcon(const ImVec2& pos, float size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // SoundCloud icon: Orange rounded rectangle with white wave pattern
    float rounding = size * 0.15f; // Rounded corners
    ImVec2 rect_min = pos;
    ImVec2 rect_max = ImVec2(pos.x + size, pos.y + size * 0.75f); // 4:3 aspect ratio
    
    // Draw orange rounded rectangle background (SoundCloud orange: #FF5500)
    draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(255, 85, 0, 255), rounding);
    
    // Re-create the original simplified "wave" logo,
    // positioned in a line but slightly below center
    ImVec2 center = ImVec2((rect_min.x + rect_max.x) * 0.5f,
                           (rect_min.y + rect_max.y) * 0.5f);
    
    // Calculate wave radii
    float w1_radius = size * 0.08f;
    float w2_radius = size * 0.12f;
    float w3_radius = size * 0.16f;
    
    // Position waves in a line, below center (like original SoundCloud logo)
    // Use the largest radius to determine the baseline
    float base_y = center.y + size * 0.12f; // Below center
    
    // Wave 1 (smallest, leftmost) - align bottom of circle to baseline
    float w1_x = center.x - size * 0.20f;
    float w1_y = base_y - w1_radius;
    draw_list->AddCircleFilled(ImVec2(w1_x, w1_y), w1_radius, IM_COL32(255, 255, 255, 255));
    
    // Wave 2 (medium, center) - align bottom of circle to baseline
    float w2_x = center.x;
    float w2_y = base_y - w2_radius;
    draw_list->AddCircleFilled(ImVec2(w2_x, w2_y), w2_radius, IM_COL32(255, 255, 255, 255));
    
    // Wave 3 (largest, rightmost) - align bottom of circle to baseline
    float w3_x = center.x + size * 0.20f;
    float w3_y = base_y - w3_radius;
    draw_list->AddCircleFilled(ImVec2(w3_x, w3_y), w3_radius, IM_COL32(255, 255, 255, 255));
    
    // Connect waves with lines to create a continuous wave pattern
    draw_list->AddLine(ImVec2(w1_x + w1_radius, w1_y),
                       ImVec2(w2_x - w2_radius, w2_y),
                       IM_COL32(255, 255, 255, 255),
                       2.0f);
    draw_list->AddLine(ImVec2(w2_x + w2_radius, w2_y),
                       ImVec2(w3_x - w3_radius, w3_y),
                       IM_COL32(255, 255, 255, 255),
                       2.0f);
}

void UIRenderer::renderProgressBar(float progress, const std::string& status) {
    // Left padding is already applied via Indent in renderDownloadList
    // Calculate width with padding on right side only (left is already indented)
    float available_width = ImGui::GetContentRegionAvail().x - 12.0f; // 12px padding on right side
    float progress_height = 15.0f; // Height increased by 5px (was 8.0f)
    
    // Set yellow color for progress bar
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(1.0f, 0.8f, 0.0f, 1.0f)); // Yellow
    ImGui::ProgressBar(progress, ImVec2(available_width, progress_height), nullptr); // No text overlay
    ImGui::PopStyleColor();
    
    // Don't display status here - it's displayed below the progress bar
}

void UIRenderer::renderUI() {
    if (!app_context_) return;
    
    App* app = app_context_;
    
    // Get actual window size from ImGui IO (automatically set by backend)
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 window_size = io.DisplaySize;
    
    // Main window - use full display size (automatically handles DPI scaling)
    ImVec2 exact_size = io.DisplaySize;
    ImVec2 exact_pos = ImVec2(0, 0);
    
    // Set position and size BEFORE Begin
    ImGui::SetNextWindowPos(exact_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(exact_size, ImGuiCond_Always);
    
    // Disable padding for main window to use full space
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    
    // Minimal flags - NO NoResize or NoMove so SetNextWindowSize works
    ImGui::Begin("YTDAudio", nullptr, 
        ImGuiWindowFlags_NoTitleBar | 
        ImGuiWindowFlags_NoCollapse | 
        ImGuiWindowFlags_NoBringToFrontOnFocus | 
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoScrollbar | 
        ImGuiWindowFlags_NoScrollWithMouse);
    
    // Restore padding after Begin
    ImGui::PopStyleVar(4);
    
    // Force window to exact size and position AFTER Begin
    ImGui::SetWindowSize(exact_size, ImGuiCond_Always);
    ImGui::SetWindowPos(exact_pos, ImGuiCond_Always);
    
    // Add small horizontal padding to prevent horizontal scrolling
    const float horizontal_padding = 15.0f;
    ImGui::SetCursorPos(ImVec2(horizontal_padding, 0));
    
    // Create a child window for content with small horizontal padding
    float content_width = exact_size.x - (horizontal_padding * 2);
    float content_height = exact_size.y;
    // Set maximum content size to prevent horizontal scrolling (0.0f means no limit for vertical)
    ImGui::SetNextWindowContentSize(ImVec2(content_width, 0.0f));
    // Allow vertical scrolling with mouse, disable horizontal scrolling, hide scrollbar
    ImGui::BeginChild("ContentArea", ImVec2(content_width, content_height), false, 
        ImGuiWindowFlags_NoBackground | 
        ImGuiWindowFlags_NoScrollbar);
    
    // Title and Settings button row
    ImGui::SetCursorPosY(10); // Small top padding
    float title_width = ImGui::CalcTextSize("YTDAudio v1.0.0").x;
    float title_content_width = ImGui::GetContentRegionAvail().x;
    
    // Service status indicator next to Settings button
    {
        ServiceChecker::ServiceStatus status = app->getServiceChecker()->getStatus();
        float indicator_size = 12.0f;
        float indicator_spacing = 8.0f; // Space between Settings button and indicator
        
        ImVec4 indicator_color;
        const char* tooltip_text = "";
        
        if (status == ServiceChecker::SERVICE_UNCHECKED) {
            indicator_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray
            tooltip_text = "Service status not checked";
        } else if (status == ServiceChecker::SERVICE_CHECKING) {
            indicator_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
            tooltip_text = "Checking service availability...";
        } else if (status == ServiceChecker::SERVICE_AVAILABLE) {
            indicator_color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green
            tooltip_text = "Download services are available";
        } else { // SERVICE_UNAVAILABLE
            indicator_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red
            tooltip_text = "Download services are unavailable";
        }
        
        // Draw indicator circle next to Settings button
        ImVec2 cursor_screen_pos = ImGui::GetCursorScreenPos();
        float indicator_x = cursor_screen_pos.x + horizontal_padding;
        float indicator_y = cursor_screen_pos.y + 15.0f; // Center vertically with button
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(indicator_x, indicator_y),
            indicator_size / 2,
            ImGui::ColorConvertFloat4ToU32(indicator_color)
        );
        
        // Tooltip area (invisible button for hover detection) - only for indicator, not blocking Settings button
        ImVec2 cursor_pos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(horizontal_padding, 10));
        ImGui::InvisibleButton("##status_indicator", ImVec2(indicator_size + 5, 30)); // Only cover indicator area
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", tooltip_text);
            ImGui::EndTooltip();
        }
        ImGui::SetCursorPos(cursor_pos);
    }
    
    // Settings button on the left (after indicator)
    ImGui::SetCursorPosY(10); // Reset Y position
    ImGui::SetCursorPosX(horizontal_padding + 12.0f + 8.0f); // Indicator size + spacing
    bool was_settings_open = app->getSettings()->show_settings_panel;
    if (ImGui::Button("Settings", ImVec2(100, 0))) {
        app->getSettings()->show_settings_panel = !app->getSettings()->show_settings_panel;
        // Save settings when closing panel
        if (was_settings_open && !app->getSettings()->show_settings_panel) {
            app->saveSettings();
        }
    }
    
    ImGui::SameLine();
    
    // Title centered
    float title_x = (title_content_width - title_width) * 0.5f + 100.0f; // Adjust for settings button
    ImGui::SetCursorPosX(title_x);
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Settings sidebar panel (opens from the right)
    if (app->getSettings()->show_settings_panel) {
        float panel_width = 400.0f; // Increased width to compensate for scrollbar
        float panel_x = exact_size.x - panel_width;
        ImGui::SetNextWindowPos(ImVec2(panel_x, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel_width, exact_size.y), ImGuiCond_Always);
        
        bool settings_open = app->getSettings()->show_settings_panel;
        if (ImGui::Begin("Settings", &settings_open, 
            ImGuiWindowFlags_NoTitleBar | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse)) {
            
            app->getSettings()->show_settings_panel = settings_open;
            
            // Add padding only on the left side
            const float left_panel_padding = 0.0f;
            ImGui::SetCursorPos(ImVec2(left_panel_padding, 0));
            
            // Version text and close button in the same row
            ImGui::SetCursorPos(ImVec2(10, 10)); // Left padding for version text
            ImGui::Spacing();
            ImGui::Spacing();
            
            ImGui::TextDisabled("    Statum Project YTDAudio v1.0.0");
            
            // Close button at top (square with X) - no right padding
            float close_button_size = 30.0f;
            ImGui::SetCursorPos(ImVec2(panel_width - close_button_size - 10, 10)); // Small margin from right edge
            if (ImGui::Button("×", ImVec2(close_button_size, close_button_size))) {
                app->getSettings()->show_settings_panel = false;
                app->saveSettings(); // Save settings when closing panel
            }
            
            ImGui::SetCursorPos(ImVec2(left_panel_padding, 40)); // Minimal top padding before content
            ImGui::Spacing();
            ImGui::Separator();
            
            // Make settings scrollable with left padding only
            float settings_height = exact_size.y - 70; // Account for close button and spacing
            // Calculate content width - full width minus left padding and scrollbar (no right padding)
            const float left_padding = 5.0f;
            float content_width = panel_width - left_panel_padding; // Account for left padding and scrollbar only
            ImGui::BeginChild("SettingsContent", ImVec2(content_width, settings_height), false);
            // Add left padding for all content using Indent
            ImGui::Indent(left_padding);
            renderSettings();
            ImGui::Unindent(left_padding);
            ImGui::EndChild();
            
            ImGui::End();
        }
    }
    
    // URL input section
    ImGui::Text("Enter URL:");
    ImGui::Spacing();
    
    // URL input with proper width - paste button, input field, and download button
    float input_window_width = ImGui::GetContentRegionAvail().x;
    float paste_button_width = 80.0f;     // Paste button width
    float download_button_width = 110.0f; // Download button width (slightly smaller to give input more room)
    float spacing = 10.0f;                // Spacing between elements
    // Make input a bit narrower than the theoretical max so text не упирался в рамку
    float input_width = input_window_width - paste_button_width - download_button_width - (spacing * 2) - 8.0f;
    // Ensure input width doesn't exceed available space and is at least reasonable
    if (input_width < 100.0f) {
        // If window is too narrow, make buttons smaller
        paste_button_width = 60.0f;
        download_button_width = 90.0f;
        input_width = input_window_width - paste_button_width - download_button_width - (spacing * 2) - 8.0f;
    }
    if (input_width < 50.0f) input_width = 50.0f; // Absolute minimum
    
    // Paste button
    if (ImGui::Button("Paste", ImVec2(paste_button_width, 0))) {
        const char* clipboard_text = ImGui::GetClipboardText();
        if (clipboard_text != nullptr && strlen(clipboard_text) > 0) {
            size_t text_len = strlen(clipboard_text);
            size_t copy_len = (text_len < sizeof(app->url_input_) - 1) ? text_len : sizeof(app->url_input_) - 1;
            strncpy(app->url_input_, clipboard_text, copy_len);
            app->url_input_[copy_len] = '\0';
        }
    }
    
    ImGui::SameLine();
    ImGui::SetNextItemWidth(input_width);
    bool enter_pressed = ImGui::InputText("##url", app->url_input_, sizeof(app->url_input_), ImGuiInputTextFlags_EnterReturnsTrue);
    
    // Start download on Enter key press
    if (enter_pressed && strlen(app->url_input_) > 0) {
        app->addDownloadTask(app->url_input_);
        app->url_input_[0] = '\0';
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Download", ImVec2(download_button_width, 0))) {
        if (strlen(app->url_input_) > 0) {
            app->addDownloadTask(app->url_input_);
            app->url_input_[0] = '\0';
        }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Download list with proper sizing
    ImGui::Text("Downloads:");
    ImGui::SameLine();
    float clear_button_width = 120.0f;
    float text_width = ImGui::CalcTextSize("Downloads:").x;
    float available_width = ImGui::GetContentRegionAvail().x;
    // Ensure button doesn't go beyond available width
    float button_x = available_width - clear_button_width + text_width + 10.0f;
    if (button_x + clear_button_width > available_width + text_width + 10.0f) {
        button_x = available_width - clear_button_width;
    }
    ImGui::SetCursorPosX(button_x);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f)); // center text vertically/horizontally
    if (ImGui::Button("Clear List", ImVec2(clear_button_width - 15.0f, 24.0f))) {
        ImGui::OpenPopup("##clear_list_confirm");
    }
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
    if (ImGui::BeginPopupModal("##clear_list_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        float text_w = ImGui::CalcTextSize("Clear download list?").x;
        float avail_w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((avail_w - text_w) * 0.5f);
        ImGui::Text("Clear download list?");
        ImGui::Spacing();
        if (ImGui::Button("Yes", ImVec2(80, 0))) {
            app->clearDownloadList();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    ImGui::Spacing();
    
    // Make download list scrollable
    float window_height = ImGui::GetWindowHeight();
    float current_y = ImGui::GetCursorPosY();
    float list_height = window_height - current_y - 10.0f; // Small bottom padding
    if (list_height < 50.0f) list_height = 50.0f; // Minimum height
    
    // Allow vertical scrolling in downloads list, disable horizontal, hide scrollbar
    ImGui::BeginChild("DownloadsList", ImVec2(0, list_height), true, 
        ImGuiWindowFlags_NoScrollbar);
    renderDownloadList();
    ImGui::EndChild();
    
    ImGui::EndChild(); // End ContentArea
    ImGui::End();
}

void UIRenderer::renderDownloadList() {
    if (!app_context_) return;
    
    App* app = app_context_;
    
    // Quickly copy task data and release mutex to avoid blocking updates
    struct TaskSnapshot {
        std::string id;              // Unique identifier for reliable deletion
        std::string url;
        std::string platform;
        std::string status;
        float progress;
        std::string filename;
        std::string error_message;
        std::string file_path;
        int64_t file_size;
        bool is_playlist;
        int total_playlist_items;
        int current_playlist_item;
        std::string current_item_title;
        std::string playlist_name;
        bool metadata_loaded;
        AudioMetadata metadata;
        std::vector<PlaylistItem> playlist_items;
        std::string thumbnail_base64;  // Base64 encoded thumbnail image
        DownloadTask* original_ptr;  // Keep pointer for callbacks
        int history_index = -1;      // Index in history_view_tasks_ (for backward compatibility, but use id for deletion)
        int64_t timestamp = 0;       // For sorting (from created_at or history)
    };
    
    std::vector<TaskSnapshot> active_snapshots;
    std::vector<TaskSnapshot> history_snapshots;
    std::vector<size_t> tasks_to_remove;
    std::vector<int> history_to_remove;  // For backward compatibility (index-based)
    std::vector<std::string> history_to_remove_by_id;  // For reliable deletion (ID-based)
    
    // CRITICAL: Get ALL history data BEFORE locking tasks_mutex_ to avoid deadlock!
    // Both getHistoryItems() and getHistoryViewTasks() lock history_mutex_
    // If we call them inside tasks_mutex_, we get: UI: tasks_mutex_ -> history_mutex_
    // But download thread does: history_mutex_ -> tasks_mutex_ = DEADLOCK!
    std::vector<HistoryItem> history_items_cache;
    if (app->history_manager_) {
        history_items_cache = app->history_manager_->getHistoryItems();
    }
    // Get history view tasks BEFORE locking tasks_mutex_ (use auto to allow move construction)
    auto history_view_tasks_cache = app->getHistoryViewTasks();
    
    {
        std::lock_guard<std::mutex> lock(app->tasks_mutex_);
        // Fast snapshot copy - only essential data for rendering
        active_snapshots.reserve(app->tasks_.size());
        for (size_t i = 0; i < app->tasks_.size(); i++) {
            auto& task = app->tasks_[i];
            TaskSnapshot snap;
            snap.url = task->url;
            snap.platform = task->platform;
            snap.status = task->status;
            snap.progress = task->progress;
            snap.filename = task->filename;
            snap.error_message = task->error_message;
            snap.file_path = task->file_path;
            snap.file_size = task->file_size;
            snap.is_playlist = task->is_playlist;
            snap.total_playlist_items = task->total_playlist_items;
            snap.current_playlist_item = task->current_playlist_item;
            snap.current_item_title = task->current_item_title;
            snap.playlist_name = task->playlist_name;
            snap.metadata_loaded = task->metadata_loaded;
            snap.metadata = task->metadata;
            snap.playlist_items = task->playlist_items;  // Copy vector
            // Try to get thumbnail and id from history if task was already in history
            snap.thumbnail_base64 = "";
            snap.id = "";  // Will be filled from history if available
            // Use pre-fetched history items (no locking needed)
            for (const auto& h : history_items_cache) {
                if (h.url == task->url) {
                    snap.id = h.id;  // Get ID from history if available
                    snap.thumbnail_base64 = h.thumbnail_base64;
                    break;
                }
            }
            snap.original_ptr = task.get();
            snap.history_index = -1;
            // Use current time for active tasks (they should appear first)
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            snap.timestamp = timestamp;
            // Include active tasks (queued/downloading) and recently completed tasks
            // Completed tasks will be shown from tasks_ until they appear in history_view_tasks_
            if (snap.status == "queued" || snap.status == "downloading" || 
                snap.status == "completed" || snap.status == "error" || 
                snap.status == "cancelled" || snap.status == "already_exists") {
                active_snapshots.push_back(std::move(snap));
            }
        }
    } // tasks_mutex_ released here - no more lock held
    
    // Add history tasks from PRE-FETCHED cache (no locks needed!)
    for (size_t idx = 0; idx < history_view_tasks_cache.size(); idx++) {
        const auto& t = history_view_tasks_cache[idx];
        if (!t) continue;
        TaskSnapshot snap;
        snap.url = t->url;
        snap.platform = t->platform;
        snap.status = t->status;
        snap.progress = t->progress;
        snap.filename = t->filename;
        snap.error_message = t->error_message;
        snap.file_path = t->file_path;
        snap.file_size = t->file_size;
        snap.is_playlist = t->is_playlist;
        snap.total_playlist_items = t->total_playlist_items;
        snap.current_playlist_item = t->current_playlist_item;
        snap.current_item_title = t->current_item_title;
        snap.playlist_name = t->playlist_name;
        snap.metadata_loaded = t->metadata_loaded;
        snap.metadata = t->metadata;
        snap.playlist_items = t->playlist_items;
        // Get thumbnail_base64 and id from pre-fetched history (no locking needed)
        for (const auto& h : history_items_cache) {
            if (h.url == t->url) {
                snap.id = h.id;  // Use unique ID for reliable deletion
                snap.thumbnail_base64 = h.thumbnail_base64;
                snap.timestamp = h.timestamp;
                break;
            }
        }
        snap.original_ptr = nullptr; // history-only
        snap.history_index = static_cast<int>(idx);  // Keep for backward compatibility
        history_snapshots.push_back(std::move(snap));
    }
    
    if (active_snapshots.empty() && history_snapshots.empty()) {
        ImGui::TextDisabled("No downloads yet. Enter a URL above to start.");
        return;
    }
    
    // DEBUG: Show snapshot counts
    static int frame_counter = 0;
    // if (frame_counter++ % 60 == 0) {
    //     std::cout << "[DEBUG UI] active_snapshots=" << active_snapshots.size() 
    //               << ", history_snapshots=" << history_snapshots.size() << std::endl;
    //     for (const auto& s : active_snapshots) {
    //         std::cout << "[DEBUG UI]   Active: status=" << s.status << ", url=" << s.url.substr(0, 40) << std::endl;
    //     }
    // }

    std::vector<TaskSnapshot> render_list;
    render_list.reserve(active_snapshots.size() + history_snapshots.size());
    
    // Build set of URLs from history to avoid duplicates
    std::set<std::string> history_urls;
    for (const auto& s : history_snapshots) {
        if (!s.url.empty()) {
            history_urls.insert(s.url);
        }
    }
    
    // Track URLs from active tasks to avoid duplicates within active tasks
    std::set<std::string> active_urls;
    
    // Add active snapshots (excluding those already in history and duplicates within active tasks)
    for (auto& s : active_snapshots) {
        // Skip completed/error/cancelled tasks that are already in history
        if ((s.status == "completed" || s.status == "error" || 
             s.status == "cancelled" || s.status == "already_exists") &&
            history_urls.find(s.url) != history_urls.end()) {
            continue; // Skip duplicate - already in history
        }
        // Skip duplicates within active tasks (keep only first occurrence)
        if (!s.url.empty() && active_urls.find(s.url) != active_urls.end()) {
            continue; // Skip duplicate - already in active tasks
        }
        if (!s.url.empty()) {
            active_urls.insert(s.url);
        }
        render_list.push_back(std::move(s));
    }
    
    // Add history snapshots
    for (auto& s : history_snapshots) {
        render_list.push_back(std::move(s));
    }

    // Sort by timestamp (newest first) - active tasks (queued/downloading) should appear first
    // For same timestamp, prioritize active tasks
    std::sort(render_list.begin(), render_list.end(), [](const TaskSnapshot& a, const TaskSnapshot& b) {
        // Active tasks (queued/downloading) always come first
        bool a_active = (a.status == "queued" || a.status == "downloading");
        bool b_active = (b.status == "queued" || b.status == "downloading");
        if (a_active != b_active) {
            return a_active; // a_active comes first
        }
        // For same type, sort by timestamp (newest first)
        if (a.timestamp != b.timestamp) {
            return a.timestamp > b.timestamp;
        }
        // Fallback: sort by URL for stability
        return a.url < b.url;
    });

    for (size_t i = 0; i < render_list.size(); i++) {
        auto& task = render_list[i];
        
        ImGui::PushID(static_cast<int>(i));
        
        // Card-style container with rounded corners
        // Make cards more compact vertically by reducing ItemSpacing
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0)); // No vertical spacing between cards
        
        // Card background color based on status (mid-dark theme)
        ImVec4 card_bg = ImVec4(0.22f, 0.22f, 0.24f, 0.9f);  // Default card
        if (task.status == "completed") {
            card_bg = ImVec4(0.20f, 0.25f, 0.22f, 0.9f);  // Green tint for completed
        } else if (task.status == "error") {
            card_bg = ImVec4(0.25f, 0.20f, 0.20f, 0.9f);  // Red tint for error
        } else if (task.status == "cancelled") {
            card_bg = ImVec4(0.25f, 0.22f, 0.18f, 0.9f);  // Orange tint for cancelled
        } else if (task.status == "downloading") {
            card_bg = ImVec4(0.22f, 0.22f, 0.26f, 0.9f);  // Blue tint for downloading
        }
        
        ImGui::PushStyleColor(ImGuiCol_ChildBg, card_bg);
        // Fixed-height card: explicit top/bottom padding, height does NOT auto-grow with content
        const float card_top_padding = 10.0f;
        const float card_bottom_padding = 10.0f;
        const float base_card_height_single = 72.0f;   // tuned for single-file layout
        const float base_card_height_playlist = 72.0f; // same height as single-file card to keep bottom padding identical
        float card_height = (task.is_playlist && task.total_playlist_items > 0)
            ? base_card_height_playlist
            : base_card_height_single;
        // Store card start position before BeginChild to position playlist items header immediately after
        float card_start_y = ImGui::GetCursorPosY();
        float card_total_height = card_height + card_top_padding + card_bottom_padding;
        ImGui::BeginChild(("Card##" + std::to_string(i)).c_str(),
                          ImVec2(0, card_total_height),
                          true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        // Check if this is a completed task that will use table layout
        // For "already_exists" status, use the same layout as "completed"
        bool is_completed_or_exists = (task.status == "completed" || task.status == "already_exists");
        bool use_table_layout = (is_completed_or_exists && !(task.is_playlist && task.total_playlist_items > 0));
        bool use_playlist_table_layout = (is_completed_or_exists && task.is_playlist && task.total_playlist_items > 0);
        bool is_table_layout = use_table_layout || use_playlist_table_layout;
        
        // For table layouts, start from the very top of the card (no padding)
        // For other layouts, use normal padding
        if (!is_table_layout) {
            // Top vertical padding inside card (exactly 5px)
            ImGui::Dummy(ImVec2(0, card_top_padding));
        }
        
        // Add delete button in top-right corner
        // Show only when download is finished (completed, cancelled, already exists, or error)
        if (task.status == "completed" ||
            task.status == "cancelled" ||
            task.status == "already_exists" ||
            task.status == "error") {
            float delete_button_size = 15.0f;
            float delete_button_padding = 0.0f;
            ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - delete_button_size - delete_button_padding);
            ImGui::SetCursorPosY(delete_button_padding);
            std::string delete_button_id = "×##delete_" + std::to_string(i);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.1f, 0.1f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            // Center the text in the button and remove button padding for better centering
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            if (ImGui::Button(delete_button_id.c_str(), ImVec2(delete_button_size, delete_button_size))) {
                // Store pointer for active tasks, or ID for history items (more reliable than index)
                if (task.original_ptr) {
                    tasks_to_remove.push_back(reinterpret_cast<size_t>(task.original_ptr));
                } else if (!task.id.empty()) {
                    // Use unique ID for reliable deletion
                    history_to_remove_by_id.push_back(task.id);
                } else if (task.history_index >= 0) {
                    // Fallback to index for backward compatibility
                    history_to_remove.push_back(task.history_index);
                }
            }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }
        
        // For table layouts, start from the very top with minimal padding
        // For other layouts, use normal padding
        if (!is_table_layout) {
            // Add horizontal padding inside card
            ImGui::Dummy(ImVec2(12, 0));
            
            // Apply left padding for all content using Indent
            ImGui::Indent(12.0f);
        }
        // For table layouts, we'll add padding inside the table itself, not before it
        
        // Header: Platform badge + URL
        ImVec4 platform_color = getPlatformColor(task.platform);
        
        // For completed single files and playlists, everything will be in the table with buttons
        // For other statuses, show platform and URL normally
        // Note: use_table_layout and use_playlist_table_layout are already defined above
        
        if (!use_table_layout && !use_playlist_table_layout) {
            // Align icon and text to same baseline
            float text_line_height = ImGui::GetTextLineHeight();
            float frame_padding_y = ImGui::GetStyle().FramePadding.y;
            
            // Draw platform icon if applicable - align with text baseline
            if (!task.platform.empty()) {
                ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
                float icon_size = text_line_height * 0.8f;
                // Align icon to text baseline - move icon down to match text baseline
                // Text baseline is typically lower than center, so we need to move icon down more
                float icon_y_offset = text_line_height * 0.15f; // Move icon down to align with text baseline
                
                if (task.platform == "YouTube") {
                    drawYouTubeIcon(ImVec2(cursor_pos.x, cursor_pos.y + icon_y_offset), icon_size);
                } else if (task.platform == "SoundCloud") {
                    drawSoundCloudIcon(ImVec2(cursor_pos.x, cursor_pos.y + icon_y_offset), icon_size);
                }
                
                // Move cursor after icon
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon_size + 6.0f);
            }
            
            // Platform name - aligned to same line as icon
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(platform_color, "%s", task.platform.c_str());
            ImGui::SameLine(0, 12);
            
            // URL (truncate if too long) - aligned to same line
            ImGui::AlignTextToFramePadding();
            std::string display_url = truncateUrl(task.url, 50);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("%s", display_url.c_str());
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
        }
        
        // Progress bar - only show when actively downloading
        // Hide for queued, completed, error, cancelled, and already_exists statuses
        // DEBUG: Show when we're rendering a downloading task
        if (task.status == "downloading") {
            static int debug_counter = 0;
            if (debug_counter++ % 60 == 0) {  // Print every 60 frames (~1 second)
                std::cout << "[DEBUG UI] Rendering downloading task: " << task.url.substr(0, 50) << "... progress=" << task.progress << std::endl;
            }
            ImGui::Dummy(ImVec2(0, 8.0f)); // Padding above progress bar (8px)
            renderProgressBar(task.progress, task.status);
            ImGui::Dummy(ImVec2(0, 4.0f)); // Small padding below progress bar before items line (4px)
            
            // Under progress bar: show playlist items info, status text, and cancel button in one line
        if (task.is_playlist && task.total_playlist_items > 0 && !use_playlist_table_layout) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f)); // Minimal vertical spacing
                
                // Blue: Total items
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
            ImGui::Text("Playlist: %d items", task.total_playlist_items);
            ImGui::PopStyleColor();
            
                // Yellow: Current item (on same line)
            if (task.current_playlist_item >= 0 && task.current_playlist_item < task.total_playlist_items) {
                ImGui::SameLine(0, 12);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                std::string current_info = "Item " + std::to_string(task.current_playlist_item + 1) + "/" + std::to_string(task.total_playlist_items);
                ImGui::Text("%s", current_info.c_str());
                ImGui::PopStyleColor();
            }
                
                // Status text "Downloading..." on same line
                ImGui::SameLine(0, 12);
                ImGui::TextDisabled("Downloading...");
                
                // Cancel button on the same line, aligned to the right
                ImGui::SameLine();
                float button_width = 100.0f;
                float available_width = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_width - button_width - 12.0f); // Right padding
                std::string cancel_id = "Cancel##cancel_" + std::to_string(i);
                if (ImGui::Button(cancel_id.c_str(), ImVec2(button_width, 20.0f))) {
                    app->cancelDownload(task.original_ptr);
                }
                
                ImGui::PopStyleVar();
                ImGui::Dummy(ImVec2(0, 8.0f)); // Padding below (8px)
            } else {
                // For single files or when not downloading playlist
            ImGui::Dummy(ImVec2(0, 8.0f)); // Padding below progress bar (8px)
        
                // Status and Cancel button for single files
        if (task.status == "downloading" || task.status == "queued") {
            // Status text
            if (task.status == "downloading") {
                ImGui::TextDisabled("Downloading...");
            } else {
                ImGui::TextDisabled("Queued...");
            }
            // Cancel button on the same line, aligned to the right with padding
            ImGui::SameLine();
            float button_width = 100.0f;
            float available_width = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_width - button_width - 12.0f); // Right padding
            std::string cancel_id = "Cancel##cancel_" + std::to_string(i);
            if (ImGui::Button(cancel_id.c_str(), ImVec2(button_width, 20.0f))) {
                app->cancelDownload(task.original_ptr);
            }
            ImGui::Spacing();
                }
            }
        } else if (task.status == "queued") {
            // For queued status (not downloading yet)
            if (task.status == "queued") {
                ImGui::TextDisabled("Queued...");
                // Cancel button on the same line, aligned to the right with padding
                ImGui::SameLine();
                float button_width = 100.0f;
                float available_width = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_width - button_width - 12.0f); // Right padding
                std::string cancel_id = "Cancel##cancel_" + std::to_string(i);
                if (ImGui::Button(cancel_id.c_str(), ImVec2(button_width, 20.0f))) {
                    app->cancelDownload(task.original_ptr);
                }
                ImGui::Spacing();
            }
        }
        
        // Show playlist information for non-downloading playlists (completed, etc.)
        // For completed playlists, this will be shown inside the table
        if (task.is_playlist && task.total_playlist_items > 0 && !use_playlist_table_layout && task.status != "downloading") {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f)); // Minimal vertical spacing
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
            ImGui::Text("Playlist: %d items", task.total_playlist_items);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        
        // Retry button for cancelled downloads is handled in the else if block below
        // (along with error status) to avoid duplication
        
        // Status for completed downloads and already_exists (use same layout)
        if (task.status == "completed" || task.status == "already_exists") {
            // For table layouts, start from the top (no vertical offset)
            // Reset cursor to top of card for table layouts
            if (is_table_layout) {
                ImGui::SetCursorPosY(0); // Start from the very top
                // Add minimal horizontal padding for table content
                ImGui::Dummy(ImVec2(12, 0));
                ImGui::Indent(12.0f);
            }
            
            // Table should fill the entire card height from top to bottom
            // Get actual content height: for table layouts, we start at Y=0
            // The card window height includes card_top_padding + card_bottom_padding in BeginChild size
            // But card_bottom_padding is added at the end, so we need to account for it
            float card_window_height = ImGui::GetWindowHeight();
            // For table layouts: we start at Y=0 (no top padding), but bottom padding is added later
            // So available height is window_height minus bottom_padding
            float full_card_content_height = card_window_height - card_bottom_padding;
            
            // Unified rendering: playlist and single cards use the same layout logic
            // Platform (YouTube/SoundCloud) only affects color and icon, not sizing or positioning
            if (task.is_playlist && task.total_playlist_items > 0) {
                // Playlist card rendering - same for all platforms
                const float button_width = 100.0f;
                const float right_padding = 20.0f;
                const float thumbnail_width = UIRenderer::THUMBNAIL_WIDTH_PLAYLIST;
                
                std::string table_id = "##playlist_completed_layout_" + std::to_string(i);
                if (ImGui::BeginTable(table_id.c_str(), 3, ImGuiTableFlags_NoBordersInBody)) {
                    ImGui::TableSetupColumn("Thumbnail", ImGuiTableColumnFlags_WidthFixed, thumbnail_width);
                    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, button_width + right_padding);
                    
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, full_card_content_height);
                    
                    // Thumbnail column
                    ImGui::TableNextColumn();
                    float thumbnail_start_y = ImGui::GetCursorPosY();
                    float thumbnail_display_width = thumbnail_width;
                    
                    // Text column
                    ImGui::TableNextColumn();
                    float estimated_height = ImGui::GetTextLineHeight() * 3; // Platform+URL, Playlist info, Completed
                    float text_vertical_offset = (full_card_content_height - estimated_height) * 0.5f;
                    if (text_vertical_offset > 0) {
                        ImGui::Dummy(ImVec2(0, text_vertical_offset));
                    }
                    
                    // Platform + URL
                    ImVec4 platform_color = getPlatformColor(task.platform);
                    drawPlatformIconInline(task.platform);
                    ImGui::TextColored(platform_color, "%s", task.platform.c_str());
                    ImGui::SameLine(0, 12);
                    std::string display_url = truncateUrl(task.url, 50);
                    ImVec2 text_pos = ImGui::GetCursorScreenPos();
                    ImVec2 text_size = ImGui::CalcTextSize(display_url.c_str());
                    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
                    bool is_hovered = (mouse_pos.x >= text_pos.x && mouse_pos.x <= text_pos.x + text_size.x &&
                                      mouse_pos.y >= text_pos.y && mouse_pos.y <= text_pos.y + text_size.y);
                    ImVec4 link_color = is_hovered ? ImVec4(0.6f, 0.8f, 1.0f, 1.0f) : ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, link_color);
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
                    if (ImGui::Selectable(display_url.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                        PlatformUtils::openURL(task.url);
                    }
                    ImGui::PopStyleColor(3);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    
                    // Playlist info
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
                    ImGui::Text("Playlist: %d items", task.total_playlist_items);
                    ImGui::PopStyleColor();
                    
                    // Status + playlist name (Completed or Already exists)
                    if (task.status == "completed") {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
                    ImGui::Text("Completed");
                    ImGui::PopStyleColor();
                    } else if (task.status == "already_exists") {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                        ImGui::Text("⚠ Already exists");
                        ImGui::PopStyleColor();
                        // Don't show file size for playlists in "already_exists" status
                    } else if (task.status == "cancelled") {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.0f, 1.0f));
                        ImGui::Text("⚠ Cancelled");
                        ImGui::PopStyleColor();
                    }
                    if (!task.playlist_name.empty()) {
                        ImGui::SameLine(0, 10);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                        ImGui::Text("%s", task.playlist_name.c_str());
                        ImGui::PopStyleColor();
                        
                        // Show "download missing" text if there are missing items
                        if (task.is_playlist && task.total_playlist_items > 0) {
                            std::vector<int> missing_indices;
                            for (int idx = 0; idx < task.total_playlist_items; idx++) {
                                bool is_downloaded = false;
                                if (idx < static_cast<int>(task.playlist_items.size()) && task.playlist_items[idx].downloaded) {
                                    is_downloaded = true;
                                } else if (task.original_ptr && task.original_ptr->playlist_item_file_paths.find(idx) != task.original_ptr->playlist_item_file_paths.end()) {
                                    std::string item_path = task.original_ptr->playlist_item_file_paths[idx];
                                    if (fileExists(item_path)) {
                                        is_downloaded = true;
                                    }
                                }
                                if (!is_downloaded) {
                                    missing_indices.push_back(idx);
                                }
                            }
                            if (!missing_indices.empty()) {
                                ImGui::SameLine(0, 10);
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
                                ImGui::Text("(%d missing)", static_cast<int>(missing_indices.size()));
                                ImGui::PopStyleColor();
                            }
                        }
                    }
                    
                    // Buttons column
                    ImGui::TableNextColumn();
                    float button_height = 20.0f; // Fixed height for better visibility
                    
                    // Calculate how many buttons we'll have
                    std::vector<int> missing_indices;
                    if (task.is_playlist && task.total_playlist_items > 0) {
                        for (int idx = 0; idx < task.total_playlist_items; idx++) {
                            bool is_downloaded = false;
                            if (idx < static_cast<int>(task.playlist_items.size()) && task.playlist_items[idx].downloaded) {
                                is_downloaded = true;
                            } else if (task.original_ptr && task.original_ptr->playlist_item_file_paths.find(idx) != task.original_ptr->playlist_item_file_paths.end()) {
                                std::string item_path = task.original_ptr->playlist_item_file_paths[idx];
                                if (fileExists(item_path)) {
                                    is_downloaded = true;
                                }
                            }
                            if (!is_downloaded) {
                                missing_indices.push_back(idx);
                            }
                        }
                    }
                    
                    // Buttons stacked vertically: Retry on top, Open Folder below
                    bool show_retry = !missing_indices.empty() && (task.status == "completed" || task.status == "cancelled" || task.status == "already_exists");
                    
                    // Calculate total height for vertical centering
                    const float button_spacing_v = 8.0f;
                    float total_buttons_height = button_height;
                    if (show_retry) {
                        total_buttons_height += button_height + button_spacing_v;
                    }
                    float btn_vert_offset = (full_card_content_height - total_buttons_height) * 0.5f;
                    if (btn_vert_offset > 0) {
                        ImGui::Dummy(ImVec2(0, btn_vert_offset));
                    }
                    
                    // Retry button (top) - only if there are missing items
                    if (show_retry) {
                        bool is_retrying = app_context_->isRetryInProgress(task.url);
                        
                        if (is_retrying) {
                            // Show loading spinner instead of button
                            ImGui::BeginDisabled();
                            std::string loading_id = "Loading...##retry_loading_" + std::to_string(i);
                            ImGui::Button(loading_id.c_str(), ImVec2(button_width, button_height));
                            ImGui::EndDisabled();
                        } else {
                            std::string retry_button_id = "Retry##retry_" + std::to_string(i);
                            if (ImGui::Button(retry_button_id.c_str(), ImVec2(button_width, button_height))) {
                                if (task.original_ptr) {
                                    // Active task - use direct pointer
                                    app_context_->retryMissingPlaylistItems(task.original_ptr);
                                } else {
                                    // History item - use URL to re-download
                                    app_context_->retryMissingFromHistory(task.url);
                                }
                            }
                        }
                        ImGui::Dummy(ImVec2(0, button_spacing_v)); // Spacing between buttons
                    }
                    
                    // Open Folder button (bottom)
                    std::string button_id = "Open Folder##playlist_completed_" + std::to_string(i);
                    if (ImGui::Button(button_id.c_str(), ImVec2(button_width, button_height))) {
                        std::string folder_path = app_context_->getSettings()->downloads_dir;
                        if (app_context_->getSettings()->save_playlists_to_separate_folder && !task.playlist_name.empty()) {
                            std::string folder_name = app_context_->sanitizeFilename(task.playlist_name);
                            folder_path += "/" + folder_name;
                        }
                        PlatformUtils::openFolder(folder_path);
                    }
                    
                    // Draw thumbnail
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(thumbnail_start_y);
                    float column_actual_height = full_card_content_height;
                    // Always draw thumbnail - drawThumbnail will use placeholder if thumbnail_base64 is empty
                    drawThumbnail(task.thumbnail_base64, thumbnail_display_width, column_actual_height, task.platform);
                    
                    ImGui::EndTable();
                }
            } else if (!(task.is_playlist && task.total_playlist_items > 0)) {
                // Single file card rendering - same for all platforms
                const float button_width = 100.0f;
                const float button_spacing = 8.0f;
                const float right_padding = 20.0f;
                const float thumbnail_width = UIRenderer::THUMBNAIL_WIDTH_SINGLE;
                
                std::string single_file_table_id = "##single_file_completed_layout_" + std::to_string(i);
                if (ImGui::BeginTable(single_file_table_id.c_str(), 3, ImGuiTableFlags_NoBordersInBody)) {
                    ImGui::TableSetupColumn("Thumbnail", ImGuiTableColumnFlags_WidthFixed, thumbnail_width);
                    ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed, button_width + right_padding);
                    
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, full_card_content_height);
                    
                    // Thumbnail column
                    ImGui::TableNextColumn();
                    float thumbnail_start_y = ImGui::GetCursorPosY();
                    float thumbnail_display_width = thumbnail_width;
                    
                    // Text column
                    ImGui::TableNextColumn();
                    float estimated_height = ImGui::GetTextLineHeight() * 3; // Platform+URL, Completed, Filename
                    float text_vertical_offset = (full_card_content_height - estimated_height) * 0.5f;
                    if (text_vertical_offset > 0) {
                        ImGui::Dummy(ImVec2(0, text_vertical_offset));
                    }
                    
                    // Platform + URL
                    ImVec4 platform_color = getPlatformColor(task.platform);
                    drawPlatformIconInline(task.platform);
                    ImGui::TextColored(platform_color, "%s", task.platform.c_str());
                    ImGui::SameLine(0, 12);
                    std::string display_url = truncateUrl(task.url, 50);
                    ImVec2 text_pos = ImGui::GetCursorScreenPos();
                    ImVec2 text_size = ImGui::CalcTextSize(display_url.c_str());
                    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
                    bool is_hovered = (mouse_pos.x >= text_pos.x && mouse_pos.x <= text_pos.x + text_size.x &&
                                      mouse_pos.y >= text_pos.y && mouse_pos.y <= text_pos.y + text_size.y);
                    ImVec4 link_color = is_hovered ? ImVec4(0.6f, 0.8f, 1.0f, 1.0f) : ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, link_color);
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
                    if (ImGui::Selectable(display_url.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                        PlatformUtils::openURL(task.url);
                    }
                    ImGui::PopStyleColor(3);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    
                    // Completed
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
                    ImGui::Text("Completed");
                    ImGui::PopStyleColor();
                    
                    // Filename + metadata
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                    ImGui::Text("%s", task.filename.c_str());
                    ImGui::PopStyleColor();
                    if (task.metadata.duration > 0) {
                        ImGui::SameLine(0, 10);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));
                        ImGui::Text("%s", AudioUtils::formatDuration(task.metadata.duration).c_str());
                        ImGui::PopStyleColor();
                    }
                    if (task.metadata.bitrate > 0) {
                        ImGui::SameLine(0, 10);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.7f, 1.0f));
                        ImGui::Text("%d kbps", task.metadata.bitrate);
                        ImGui::PopStyleColor();
                    }
                    if (task.file_size > 0) {
                        ImGui::SameLine(0, 10);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                        ImGui::Text("(%s)", AudioUtils::formatFileSize(task.file_size).c_str());
                        ImGui::PopStyleColor();
                    }
                    
                    // Buttons column
                    ImGui::TableNextColumn();
                    float button_height = ImGui::GetFrameHeight() + 5.0f;
                    float buttons_total_height = button_height * 2 + button_spacing;
                    float buttons_vertical_offset = (full_card_content_height - buttons_total_height) * 0.5f;
                    if (buttons_vertical_offset > 0) {
                        ImGui::Dummy(ImVec2(0, buttons_vertical_offset));
                    }
                    
                    std::string open_file_button_id = "Open File##single_file_" + std::to_string(i);
                    if (ImGui::Button(open_file_button_id.c_str(), ImVec2(button_width, button_height))) {
                        app_context_->openFileLocation(task.file_path);
                    }
                    ImGui::Dummy(ImVec2(0, button_spacing));
                    std::string drag_button_id = "Drag##single_file_" + std::to_string(i);
                    ImGui::Button(drag_button_id.c_str(), ImVec2(button_width, button_height));
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0, false)) {
                        app_context_->startFileDrag(task.file_path);
                    }
                    
                    // Draw thumbnail
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(thumbnail_start_y);
                    float column_actual_height = full_card_content_height;
                    // Always draw thumbnail - drawThumbnail will use placeholder if thumbnail_base64 is empty
                    drawThumbnail(task.thumbnail_base64, thumbnail_display_width, column_actual_height, task.platform);
                    
                    ImGui::EndTable();
                }
            }
        } else if (task.status == "already_exists") {
            // For playlists, use table layout with thumbnail (same as completed status)
            bool use_playlist_table = (task.is_playlist && task.total_playlist_items > 0);
            
            if (use_playlist_table) {
                // Playlist card rendering with thumbnail - same layout as completed
                if (is_table_layout) {
                    ImGui::SetCursorPosY(0); // Start from the very top
                    ImGui::Dummy(ImVec2(12, 0));
                    ImGui::Indent(12.0f);
                }
                
                float card_window_height = ImGui::GetWindowHeight();
                float full_card_content_height = card_window_height - card_bottom_padding;
                
                const float button_width = 100.0f;
                const float button_spacing = 12.0f;
                const float right_padding = 20.0f;
                const float thumbnail_width = UIRenderer::THUMBNAIL_WIDTH_PLAYLIST;
                // Width for two buttons: button + spacing + button + padding
                const float buttons_column_width = button_width * 2 + button_spacing + right_padding;
                
                std::string table_id = "##playlist_already_exists_layout_" + std::to_string(i);
                if (ImGui::BeginTable(table_id.c_str(), 3, ImGuiTableFlags_NoBordersInBody)) {
                    ImGui::TableSetupColumn("Thumbnail", ImGuiTableColumnFlags_WidthFixed, thumbnail_width);
                    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, buttons_column_width);
                    
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, full_card_content_height);
                    
                    // Thumbnail column
                    ImGui::TableNextColumn();
                    float thumbnail_start_y = ImGui::GetCursorPosY();
                    float thumbnail_display_width = thumbnail_width;
                    
                    // Text column
                    ImGui::TableNextColumn();
                    float estimated_height = ImGui::GetTextLineHeight() * 3; // Platform+URL, Playlist info, Already exists
                    float text_vertical_offset = (full_card_content_height - estimated_height) * 0.5f;
                    if (text_vertical_offset > 0) {
                        ImGui::Dummy(ImVec2(0, text_vertical_offset));
                    }
                    
                    // Platform + URL
                    ImVec4 platform_color = getPlatformColor(task.platform);
                    drawPlatformIconInline(task.platform);
                    ImGui::TextColored(platform_color, "%s", task.platform.c_str());
                    ImGui::SameLine(0, 12);
                    std::string display_url = truncateUrl(task.url, 50);
                    ImVec2 text_pos = ImGui::GetCursorScreenPos();
                    ImVec2 text_size = ImGui::CalcTextSize(display_url.c_str());
                    ImVec2 mouse_pos = ImGui::GetIO().MousePos;
                    bool is_hovered = (mouse_pos.x >= text_pos.x && mouse_pos.x <= text_pos.x + text_size.x &&
                                      mouse_pos.y >= text_pos.y && mouse_pos.y <= text_pos.y + text_size.y);
                    ImVec4 link_color = is_hovered ? ImVec4(0.6f, 0.8f, 1.0f, 1.0f) : ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, link_color);
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
                    if (ImGui::Selectable(display_url.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, 0))) {
                        PlatformUtils::openURL(task.url);
                    }
                    ImGui::PopStyleColor(3);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    
                    // Playlist info
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
                    ImGui::Text("Playlist: %d items", task.total_playlist_items);
                    ImGui::PopStyleColor();
                    
                    // Already exists status + playlist name
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                    ImGui::Text("⚠ Already exists");
                    ImGui::PopStyleColor();
                    // Don't show file size for playlists in "already_exists" status
                    if (!task.playlist_name.empty()) {
                        ImGui::SameLine(0, 10);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                        ImGui::Text("%s", task.playlist_name.c_str());
                        ImGui::PopStyleColor();
                    }
                    
                    // Calculate missing items for "download missing" text and Retry button
                    std::vector<int> missing_indices;
                    if (task.is_playlist && task.total_playlist_items > 0) {
                        for (int idx = 0; idx < task.total_playlist_items; idx++) {
                            bool is_downloaded = false;
                            if (idx < static_cast<int>(task.playlist_items.size()) && task.playlist_items[idx].downloaded) {
                                is_downloaded = true;
                            } else if (task.original_ptr && task.original_ptr->playlist_item_file_paths.find(idx) != task.original_ptr->playlist_item_file_paths.end()) {
                                std::string item_path = task.original_ptr->playlist_item_file_paths[idx];
                                if (fileExists(item_path)) {
                                    is_downloaded = true;
                                }
                            }
                            if (!is_downloaded) {
                                missing_indices.push_back(idx);
                            }
                        }
                        if (!missing_indices.empty()) {
                            ImGui::SameLine(0, 10);
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
                            ImGui::Text("(%d missing)", static_cast<int>(missing_indices.size()));
                            ImGui::PopStyleColor();
                        }
                    }
                    
                    // Buttons column
                    ImGui::TableNextColumn();
                    float button_height = ImGui::GetFrameHeight() + 5.0f;
                    float buttons_vertical_offset = (full_card_content_height - button_height) * 0.5f;
                    if (buttons_vertical_offset > 0) {
                        ImGui::Dummy(ImVec2(0, buttons_vertical_offset));
                    }
                    
                    std::string open_folder_button_id = "Open Folder##playlist_already_exists_" + std::to_string(i);
                    if (ImGui::Button(open_folder_button_id.c_str(), ImVec2(button_width, button_height))) {
                        std::string folder_path = app_context_->getSettings()->downloads_dir;
                        if (app_context_->getSettings()->save_playlists_to_separate_folder && !task.playlist_name.empty()) {
                            std::string folder_name = app_context_->sanitizeFilename(task.playlist_name);
                            folder_path += "/" + folder_name;
                        }
                        PlatformUtils::openFolder(folder_path);
                    }
                    
                    // Draw thumbnail
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(thumbnail_start_y);
                    float column_actual_height = full_card_content_height;
                    // Always draw thumbnail - drawThumbnail will use placeholder if thumbnail_base64 is empty
                    drawThumbnail(task.thumbnail_base64, thumbnail_display_width, column_actual_height, task.platform);
                    
                    ImGui::EndTable();
                }
            } else {
                // Single file rendering (no table, simple layout)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
            ImGui::Text("⚠ Already exists: %s", task.filename.c_str());
            ImGui::PopStyleColor();
            if (task.file_size > 0) {
                ImGui::SameLine(0, 10);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                ImGui::Text("(%s)", AudioUtils::formatFileSize(task.file_size).c_str());
                ImGui::PopStyleColor();
            } else if (!task.file_path.empty()) {
                // Try to get file size from file system
                int64_t file_size = -1;
                if (fileExistsAndGetSize(task.file_path, file_size) && file_size >= 0) {
                    // Update snapshot's file_size (but this won't persist - that's OK for display)
                    const_cast<TaskSnapshot&>(task).file_size = file_size;
                    // File size was updated, continue with metadata display
                    ImGui::SameLine(0, 10);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    ImGui::Text("(%s)", AudioUtils::formatFileSize(task.file_size).c_str());
                    ImGui::PopStyleColor();
                }
            }
            ImGui::Spacing();
            
            // Position buttons on the right side, vertically stacked
            float button_width = 70.0f; // Max width 70 pixels
            float button_height = ImGui::GetFrameHeight() + 5.0f; // Increased height
            float card_width = ImGui::GetContentRegionAvail().x;
            float right_margin = 12.0f; // Extra right padding for buttons
            float button_x = card_width - button_width - right_margin; // Align with right margin
            
                // Single file: show "Open File" button
            ImGui::SetCursorPosX(button_x);
            std::string open_file_button_id2 = "Open File##single_file_no_table_" + std::to_string(i);
            if (ImGui::Button(open_file_button_id2.c_str(), ImVec2(button_width, button_height))) {
                app_context_->openFileLocation(task.file_path);
            }
            
                // Position "Drag" button below "Open File" button (only for single files)
            ImGui::SetCursorPosX(button_x);
            std::string drag_button_id2 = "Drag##single_file_no_table_" + std::to_string(i);
            ImGui::Button(drag_button_id2.c_str(), ImVec2(button_width, button_height));
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0, false)) {
                app_context_->startFileDrag(task.file_path);
                }
            }
        } else if (task.status == "error" || task.status == "cancelled") {
            // Error / Cancelled status - show message with Retry button
            ImVec4 error_color = (task.status == "error") ? ImVec4(1, 0, 0, 1) : ImVec4(1.0f, 0.7f, 0.0f, 1.0f);
            const char* label = (task.status == "error") ? "✗ Error" : "⚠ Cancelled";
            std::string msg = task.error_message;

            if (msg.size() > 33)
            {
                msg = msg.substr(0, 30) + " error bot detect...";
            }

            ImGui::TextColored(error_color, "%s: %s", label, msg.c_str());
            // ImGui::TextColored(error_color, "%s: %s", label, task.error_message.c_str());
            
            // Show additional hint for cookie-related errors
            if (task.error_message.find("Sign in to confirm") != std::string::npos ||
                task.error_message.find("cookies") != std::string::npos) {
                ImVec4 hint_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f); // Orange/yellow color for hint
                ImGui::TextColored(hint_color, "💡 Use cookies in Settings YTDAudio");
                ImGui::Spacing();
            }
            
            // Retry button for errors/cancelled
            ImGui::SameLine();
            float button_width = 100.0f;
            float available_width = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available_width - button_width - 12.0f);
            std::string retry_id_table = "Retry##retry_table_" + std::to_string(i);
            if (ImGui::Button(retry_id_table.c_str(), ImVec2(button_width, 0))) {
                // Reset task to queued state and restart download
                std::lock_guard<std::mutex> lock(app->tasks_mutex_);
                if (task.original_ptr) {
                    // Active task - just reset status
                    task.original_ptr->status = "queued";
                    task.original_ptr->progress = 0.0f;
                    task.original_ptr->error_message.clear();
                    task.original_ptr->downloader_ptr = nullptr;
                    task.original_ptr->current_playlist_item = -1;
                    task.original_ptr->current_item_title.clear();
                } else {
                    // Task from history - remove from history and create new active task
                    if (task.history_index >= 0 && app->history_manager_) {
                        size_t idx = static_cast<size_t>(task.history_index);
                        if (idx < app->history_manager_->getHistoryItemsCount()) {
                            app->history_manager_->deleteItemByIndex(idx);
                            app->rebuildHistoryViewTasks();
                        }
                    }
                    // Create new active task
                    auto t = std::make_unique<DownloadTask>(task.url);
                    t->platform = task.platform;
                    t->filename = task.filename;
                    t->file_path = task.file_path;
                    t->file_size = task.file_size;
                    t->is_playlist = task.is_playlist;
                    t->playlist_name = task.playlist_name;
                    t->total_playlist_items = task.total_playlist_items;
                    t->metadata = task.metadata;
                    t->playlist_items = task.playlist_items;
                    t->status = "queued";
                    app->tasks_.push_back(std::move(t));
                    // Persist history changes
                    app->persistHistoryItems();
                }
            }
            ImGui::Spacing();
        }

        // Bottom vertical padding inside card (fixed)
        ImGui::Dummy(ImVec2(0, card_bottom_padding));
        
        // Remove left padding before closing card
        ImGui::Unindent(12.0f);
        
        // Close card
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        
        // Show playlist items list if this is a playlist (collapsible) - outside card, with compact but fully clickable header
        // Show list even if items are still loading (total_playlist_items might be 0 initially)
        if (task.is_playlist) {
            // Use total_playlist_items if available, otherwise show placeholder count
            int display_count = task.total_playlist_items > 0 ? task.total_playlist_items : 
                               (task.playlist_items.size() > 0 ? static_cast<int>(task.playlist_items.size()) : 0);
            
            if (display_count > 0) {
                // Position header immediately after card with no spacing
                // Calculate card bottom position (card was created earlier, so we need to get its position)
                // card_start_y and card_total_height are defined earlier in the function
                float card_bottom_y = card_start_y + card_total_height;
                ImGui::SetCursorPosY(card_bottom_y);
                
                // Slightly reduced padding, but keep vertical padding so full header height is clickable
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f)); // Small vertical spacing between items
                // Remove border/separator from collapsing header
                ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(0.0f, 0.0f));
                if (ImGui::CollapsingHeader(("Playlist Items (" + std::to_string(display_count) + ")").c_str(), ImGuiTreeNodeFlags_None)) {
                    // Compact inner list, but keep small vertical padding so rows are easy to click
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 1.0f));
                    
                    // Show items with rename capability and drag button
                    for (int j = 0; j < display_count; j++) {
                    // CRITICAL: PushID for each item to avoid ID conflicts
                    ImGui::PushID(j);
                    
                    // Determine item status: completed only if file actually exists
                    // Current if downloading, error if failed, pending otherwise
                    bool is_current = (j == task.current_playlist_item && task.status == "downloading");
                    
                    // Check if file actually exists on disk
                    bool file_exists = false;
                    std::string item_file_path = "";
                    
                    // Priority order for finding file path:
                    // 1. playlist_items[j].file_path (most reliable, set by processPlaylistItemFilePaths)
                    // 2. playlist_item_file_paths[j] (from map)
                    // 3. Construct from display name (fallback)
                    
                    // First, try to get path from playlist_items[j].file_path
                    if (j < static_cast<int>(task.playlist_items.size()) && !task.playlist_items[j].file_path.empty()) {
                        item_file_path = task.playlist_items[j].file_path;
                    }
                    
                    // Second, try to get path from saved map
                    if (item_file_path.empty() && task.original_ptr && task.original_ptr->playlist_item_file_paths.find(j) != task.original_ptr->playlist_item_file_paths.end()) {
                        item_file_path = task.original_ptr->playlist_item_file_paths[j];
                    }
                    
                    // Third, if not found, try to construct path from display name
                    if (item_file_path.empty()) {
                        std::string base_dir = app->getSettings()->downloads_dir;
                        if (app->getSettings()->save_playlists_to_separate_folder && !task.playlist_name.empty()) {
                            std::string folder_name = app->sanitizeFilename(task.playlist_name);
                            base_dir += "/" + folder_name;
                        }
                        
                        // Try to get display name for file lookup
                        std::string display_name_for_file;
                        if (task.original_ptr && task.original_ptr->playlist_item_renames.find(j) != task.original_ptr->playlist_item_renames.end()) {
                            display_name_for_file = task.original_ptr->playlist_item_renames[j];
                        } else if (j < task.playlist_items.size() && !task.playlist_items[j].title.empty()) {
                            display_name_for_file = task.playlist_items[j].title;
                        }
                        
                        if (!display_name_for_file.empty()) {
                            // Use selected format from settings instead of hardcoded .mp3
                            std::string selected_format = app ? app->getSettings()->selected_format : "mp3";
                            item_file_path = base_dir + "/" + app->sanitizeFilename(display_name_for_file) + "." + selected_format;
                        }
                    }
                    
                    // Verify file actually exists at the determined path
                    if (!item_file_path.empty() && ValidationUtils::isValidPath(item_file_path)) {
                        file_exists = fileExists(item_file_path);
                        
                        // CRITICAL FIX: If file not found and path has .mp4 extension,
                        // try checking with selected audio format extension (e.g., .mp3)
                        // This handles the case where yt-dlp downloads as .mp4 but ffmpeg converts to .mp3
                        if (!file_exists && app) {
                            std::string selected_format = app->getSettings()->selected_format;
                            size_t last_dot = item_file_path.find_last_of('.');
                            if (last_dot != std::string::npos) {
                                std::string current_ext = item_file_path.substr(last_dot + 1);
                                // If current extension is mp4/webm/etc (video format), try audio format
                                if (current_ext == "mp4" || current_ext == "webm" || current_ext == "mkv") {
                                    std::string base_path = item_file_path.substr(0, last_dot);
                                    std::string converted_path = base_path + "." + selected_format;
                                    if (fileExists(converted_path)) {
                                        file_exists = true;
                                        item_file_path = converted_path;  // Update to correct path
                                    }
                                }
                            }
                        }
                    }
                    
                    // Also check if item is marked as downloaded (even if file check failed, trust the flag)
                    if (!file_exists && j < static_cast<int>(task.playlist_items.size()) && task.playlist_items[j].downloaded) {
                        file_exists = true;
                        // If we don't have a path yet, try to get it from playlist_items
                        if (item_file_path.empty() && !task.playlist_items[j].file_path.empty()) {
                            item_file_path = task.playlist_items[j].file_path;
                        }
                    }
                    
                    // CRITICAL: Also check playlist_item_file_paths map - this is updated immediately after conversion
                    // This ensures Drag button works right after conversion completes
                    if (!file_exists && task.original_ptr && task.original_ptr->playlist_item_file_paths.find(j) != task.original_ptr->playlist_item_file_paths.end()) {
                        std::string path_from_map = task.original_ptr->playlist_item_file_paths[j];
                        if (!path_from_map.empty() && !ValidationUtils::isTemporaryFile(path_from_map)) {
                            // Verify file actually exists at this path
                            if (fileExists(path_from_map)) {
                                file_exists = true;
                                if (item_file_path.empty()) {
                                    item_file_path = path_from_map;
                                }
                            } else if (app) {
                                // Try with converted extension (mp4 -> mp3/m4a/etc)
                                std::string selected_format = app->getSettings()->selected_format;
                                size_t last_dot = path_from_map.find_last_of('.');
                                if (last_dot != std::string::npos) {
                                    std::string current_ext = path_from_map.substr(last_dot + 1);
                                    if (current_ext == "mp4" || current_ext == "webm" || current_ext == "mkv") {
                                        std::string base_path = path_from_map.substr(0, last_dot);
                                        std::string converted_path = base_path + "." + selected_format;
                                        if (fileExists(converted_path)) {
                                            file_exists = true;
                                            item_file_path = converted_path;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // Item is completed if:
                    // 1. Item is marked as downloaded in playlist_items (most reliable), OR
                    // 2. File exists on disk, OR
                    // 3. Task status is "completed" (all items are completed), OR
                    // 4. Item has a file path saved in playlist_item_file_paths
                    bool is_completed = false;
                    
                    // First, check if item is marked as downloaded in playlist_items (most reliable)
                    if (j < static_cast<int>(task.playlist_items.size())) {
                        is_completed = task.playlist_items[j].downloaded;
                    }
                    
                    // Second, check if file exists on disk
                    if (!is_completed) {
                        is_completed = file_exists;
                    }
                    
                    // Third, check if task status is "completed"
                    if (!is_completed) {
                        is_completed = (task.status == "completed");
                    }
                    
                    // Fourth, check if item has a file path saved in the map
                    if (!is_completed && task.original_ptr && task.original_ptr->playlist_item_file_paths.find(j) != task.original_ptr->playlist_item_file_paths.end()) {
                        // Item has a file path saved, consider it completed
                        is_completed = true;
                        // Also update file_exists and item_file_path if not already set
                        if (!file_exists) {
                            file_exists = true;
                        }
                        if (item_file_path.empty()) {
                        item_file_path = task.original_ptr->playlist_item_file_paths[j];
                        }
                    }
                    
                    // Mark as downloaded if file exists or is in the map
                    if (is_completed && j < static_cast<int>(task.playlist_items.size())) {
                        task.playlist_items[j].downloaded = true;
                    }
                    
                    // Determine if item has error (failed to download)
                    // Item has error if:
                    // 1. Task status is "error" and file doesn't exist
                    // 2. Task status is "error" and we're past the current item (item should have been downloaded but wasn't)
                    bool has_error = false;
                    if (task.status == "error" && !is_completed) {
                        // If task has error and file doesn't exist, mark as error
                        // Also mark as error if we're past the current downloading item (should have been downloaded)
                        if (task.current_playlist_item < 0 || j <= task.current_playlist_item) {
                            has_error = true;
                        }
                    }
                    
                    // Get display name (custom rename or default)
                    std::string display_name;
                    if (task.original_ptr && task.original_ptr->playlist_item_renames.find(j) != task.original_ptr->playlist_item_renames.end()) {
                        // Use custom rename if available
                        display_name = task.original_ptr->playlist_item_renames[j];
                    } else if (j < task.playlist_items.size() && !task.playlist_items[j].title.empty()) {
                        // Use saved title from playlist_items (most reliable source)
                        display_name = task.playlist_items[j].title;
                    } else if (is_current && !task.current_item_title.empty()) {
                        // Only use current_item_title for currently downloading item if we don't have saved title yet
                        display_name = task.current_item_title;
                    } else {
                        // Fallback to default name
                        display_name = "Item " + std::to_string(j + 1);
                    }
                    
                    // Show filename with extension only after conversion is complete
                    // During download, show title (without extension) to avoid showing wrong extension
                    if (j < task.playlist_items.size() && !task.playlist_items[j].file_path.empty()) {
                        // Only use filename if it's set (means conversion is complete or format is final)
                        if (!task.playlist_items[j].filename.empty()) {
                            // Use filename directly (already includes correct extension like .mp3 or .ogg)
                            display_name = task.playlist_items[j].filename;
                        } else {
                            // During download/conversion, filename is not set yet
                            // Get selected format from settings to determine if extension is final
                            std::string selected_format = app ? app->getSettings()->selected_format : "mp3";
                            std::string file_path = task.playlist_items[j].file_path;
                            size_t last_dot = file_path.find_last_of('.');
                            if (last_dot != std::string::npos) {
                                std::string ext = file_path.substr(last_dot);
                                // Check if extension matches selected format (final format)
                                bool is_final_format = (ext == "." + selected_format);
                                // Also allow common final formats even if not selected
                                if (!is_final_format) {
                                    is_final_format = (ext == ".mp3" || ext == ".flac" || ext == ".m4a" || ext == ".ogg");
                                }
                                // If extension is temporary (not final), don't show it yet
                                // Temporary formats: .opus, .webm (always temporary), or any supported format that doesn't match selected format
                                bool is_temporary = (!is_final_format && 
                                                     ((ext == ".opus" || ext == ".webm") ||
                                                      ((ext == ".mp3" || ext == ".m4a" || ext == ".flac" || ext == ".ogg") &&
                                                       ext != "." + selected_format)));
                                
                                if (!is_temporary) {
                                    // Extension is final format, extract filename from file_path
                                    size_t last_slash = file_path.find_last_of("/\\");
                                    if (last_slash != std::string::npos) {
                                        std::string extracted_filename = file_path.substr(last_slash + 1);
                                        if (!extracted_filename.empty()) {
                                            display_name = extracted_filename;
                                        }
                                    }
                                }
                                // If extension is temporary, keep showing title (display_name already set above)
                            }
                        }
                    }
                    
                    // Use two-column table layout like single file downloads
                    const float drag_button_width = 50.0f;
                    const float drag_button_margin = 4.0f; // Margin on both sides of Drag button
                    std::string table_id = "##playlist_item_" + std::to_string(i) + "_" + std::to_string(j);
                    
                    // Push style to remove cell padding for compact display, no horizontal padding
                    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 1.0f));
                    if (ImGui::BeginTable(table_id.c_str(), 2, ImGuiTableFlags_NoBordersInBody)) {
                        ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, drag_button_width + drag_button_margin * 2);
                        
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        
                        // Left column: Status indicator, name, and metadata
                        // Status indicator: yellow for current, green for completed, red for error, grey for pending
                        // Align bullet to bottom of text line (remove bottom padding)
                        float line_height = ImGui::GetTextLineHeight();
                        float current_y = ImGui::GetCursorPosY();
                        // Move cursor down to align bullet with bottom of text (remove bottom padding)
                        ImGui::SetCursorPosY(current_y + line_height - ImGui::GetFontSize());
                        
                        if (is_current) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f)); // Yellow
                            ImGui::Bullet();
                            ImGui::PopStyleColor();
                        } else if (is_completed && file_exists) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)); // Green
                            ImGui::Bullet();
                            ImGui::PopStyleColor();
                        } else if (has_error || (task.status == "error" && !is_completed)) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f)); // Red
                            ImGui::Bullet();
                            ImGui::PopStyleColor();
                        } else {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f)); // Grey
                            ImGui::Bullet();
                            ImGui::PopStyleColor();
                        }
                        
                        // Reset cursor Y position back to top for text alignment
                        ImGui::SetCursorPosY(current_y);
                        ImGui::SameLine(0, 4); // 4px spacing after bullet, keep on same line
                        
                        // Get metadata first (for completed items) to calculate available width
                        std::string metadata_str = "";
                        int duration = 0;
                        int bitrate = 0;
                        int64_t file_size = 0;
                        

                        // Always try to get metadata from PlaylistItem first (most reliable source, especially after reload)
                        // This works for both active downloads and tasks loaded from history
                        if (j < static_cast<int>(task.playlist_items.size())) {
                            // Use saved metadata from history if available
                            if (task.playlist_items[j].duration > 0) {
                                duration = task.playlist_items[j].duration;
                            }
                            if (task.playlist_items[j].bitrate > 0) {
                                bitrate = task.playlist_items[j].bitrate;
                            }
                            if (task.playlist_items[j].file_size > 0) {
                                file_size = task.playlist_items[j].file_size;
                            }
                        }

                        // If snapshot metadata is missing, try to pull from the live task pointer (after reload)
                        // Do this even if some fields are already present, to maximize coverage on reload
                        if (task.original_ptr) {
                            auto& orig_items = task.original_ptr->playlist_items;
                            if (j < static_cast<int>(orig_items.size())) {
                                if (duration == 0 && orig_items[j].duration > 0) {
                                    duration = orig_items[j].duration;
                                }
                                if (bitrate == 0 && orig_items[j].bitrate > 0) {
                                    bitrate = orig_items[j].bitrate;
                                }
                                if (file_size == 0 && orig_items[j].file_size > 0) {
                                    file_size = orig_items[j].file_size;
                                }
                            }
                        }
                        
                        // For completed items, also try to get file_size from file if not available from PlaylistItem
                        // Note: duration and bitrate cannot be obtained from file system, only from saved metadata
                        if (is_completed && file_size == 0) {
                            std::string item_file_path;
                            // Get file path - try multiple sources
                            // First, try from playlist_items[j].file_path
                            if (j < static_cast<int>(task.playlist_items.size()) && !task.playlist_items[j].file_path.empty()) {
                                item_file_path = task.playlist_items[j].file_path;
                            }
                            // Then try from playlist_item_file_paths map
                            if (item_file_path.empty() && task.original_ptr && task.original_ptr->playlist_item_file_paths.find(j) != task.original_ptr->playlist_item_file_paths.end()) {
                                item_file_path = task.original_ptr->playlist_item_file_paths[j];
                            }
                            // Finally, try to construct path from directory and title
                            if (item_file_path.empty()) {
                                std::string base_dir = app->getSettings()->downloads_dir;
                                if (app->getSettings()->save_playlists_to_separate_folder && !task.playlist_name.empty()) {
                                    std::string folder_name = app->sanitizeFilename(task.playlist_name);
                                    base_dir += "/" + folder_name;
                                }
                                std::string renamed_path = base_dir + "/" + display_name + ".mp3";
                                if (fileExists(renamed_path)) {
                                    item_file_path = renamed_path;
                                } else if (j < task.playlist_items.size() && !task.playlist_items[j].title.empty()) {
                                    std::string original_path = base_dir + "/" + task.playlist_items[j].title + ".mp3";
                                    if (fileExists(original_path)) {
                                        item_file_path = original_path;
                                    }
                                }
                            }
                            
                            if (!item_file_path.empty()) {
                                struct stat file_stat;
                                if (stat(item_file_path.c_str(), &file_stat) == 0) {
                                    file_size = file_stat.st_size;
                                }
                            }
                        }
                        
                        // Build metadata string: duration, bitrate, size
                        if (duration > 0 || bitrate > 0 || file_size > 0) {
                            std::vector<std::string> parts;
                            if (duration > 0) {
                                parts.push_back(AudioUtils::formatDuration(duration));
                            }
                            if (bitrate > 0) {
                                parts.push_back(std::to_string(bitrate) + " kbps");
                            }
                            if (file_size > 0) {
                                parts.push_back(AudioUtils::formatFileSize(file_size));
                            }
                            if (!parts.empty()) {
                                metadata_str = parts[0];
                                for (size_t k = 1; k < parts.size(); k++) {
                                    metadata_str += " • " + parts[k];
                                }
                            }
                        }
                        
                        // Calculate available width for content
                        float available_width = ImGui::GetContentRegionAvail().x;
                        
                        // Input field for renaming (for all items that are completed or current)
                        if (is_completed || is_current) {
                            char name_buf[512];
                            strncpy(name_buf, display_name.c_str(), sizeof(name_buf) - 1);
                            name_buf[sizeof(name_buf) - 1] = '\0';
                            
                            // Calculate width: leave space for metadata if present
                            float metadata_width = 0;
                            if (is_completed && !metadata_str.empty()) {
                                metadata_width = ImGui::CalcTextSize(metadata_str.c_str()).x + 8.0f; // 8px spacing between name and metadata
                            }
                            float input_width = available_width - metadata_width;
                            
                            ImGui::SetNextItemWidth(input_width);
                            // Use unique ID with task index and item index
                            std::string input_id = "##rename_" + std::to_string(i) + "_" + std::to_string(j);
                            // Disable text input - make read-only
                            ImGui::InputText(input_id.c_str(), name_buf, sizeof(name_buf), ImGuiInputTextFlags_ReadOnly);
                        } else {
                            // Just show text for pending items
                            ImGui::Text("%s", display_name.c_str());
                        }
                        
                        // Show metadata immediately after name (duration, bitrate, size) for completed items
                        // Align to right edge of column to eliminate empty space
                        if (is_completed && !metadata_str.empty()) {
                            ImGui::SameLine(0, 8);
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                            // Calculate position to align metadata to right edge
                            float metadata_text_width = ImGui::CalcTextSize(metadata_str.c_str()).x;
                            float current_x = ImGui::GetCursorPosX();
                            float column_end_x = current_x + ImGui::GetContentRegionAvail().x;
                            float target_x = column_end_x - metadata_text_width;
                            if (target_x > current_x) {
                                ImGui::SetCursorPosX(target_x);
                            }
                            ImGui::Text("%s", metadata_str.c_str());
                            ImGui::PopStyleColor();
                        }
                        
                        // Right column: Drag button (only if file actually exists)
                        ImGui::TableNextColumn();
                        
                        // No background color for right column (transparent)
                        
                        // CRITICAL: Show Drag button based on logic:
                        // Show button for each item as soon as it's downloaded and converted to final format,
                        // regardless of whether the entire playlist is completed
                        bool is_final_file = false;
                        
                        // Check if entire playlist is completed (saved to history)
                        // Include both "completed" and "already_exists" statuses as completed playlists
                        bool playlist_completed = (task.status == "completed" || task.status == "already_exists");
                        
                        // Check if this item is marked as downloaded
                        bool item_downloaded = false;
                        if (j < static_cast<int>(task.playlist_items.size())) {
                            item_downloaded = task.playlist_items[j].downloaded;
                        }
                        
                        if (file_exists) {
                            // Get the actual file path to check (prefer from map, then from playlist_items)
                            std::string path_to_check = item_file_path;
                            if (path_to_check.empty() && task.original_ptr && 
                                task.original_ptr->playlist_item_file_paths.find(j) != task.original_ptr->playlist_item_file_paths.end()) {
                                path_to_check = task.original_ptr->playlist_item_file_paths[j];
                            }
                            if (path_to_check.empty() && j < static_cast<int>(task.playlist_items.size()) && 
                                !task.playlist_items[j].file_path.empty()) {
                                path_to_check = task.playlist_items[j].file_path;
                            }
                            
                            if (!path_to_check.empty() && !ValidationUtils::isTemporaryFile(path_to_check)) {
                                // LOGIC:
                                // 1. If playlist is completed (saved to history) - show button for all items with file path (regardless of format)
                                // 2. If playlist is downloading - show button for items that are downloaded AND converted to final format
                                if (playlist_completed) {
                                    // Playlist is completed and saved to history - show button if file exists (regardless of format)
                                    is_final_file = true;
                                } else {
                                    // Playlist is still downloading - check if this item is downloaded and has final format
                                    // Get selected format from settings
                                    std::string selected_format = app ? app->getSettings()->selected_format : "mp3";
                                    
                                    // Check if file extension matches final format
                                    size_t last_dot = path_to_check.find_last_of('.');
                                    if (last_dot != std::string::npos) {
                                        std::string ext = path_to_check.substr(last_dot);
                                        
                                        // Check if extension matches selected format (final format)
                                        bool is_final_format = (ext == "." + selected_format);
                                        
                                        // Check if extension is temporary (not final)
                                        // Temporary formats: .opus, .webm (always temporary), or any supported format that doesn't match selected format
                                        bool is_temporary = ((ext == ".opus" || ext == ".webm") ||
                                                             ((ext == ".mp3" || ext == ".m4a" || ext == ".flac" || ext == ".ogg") &&
                                                              ext != "." + selected_format));
                                        
                                        // If path points to intermediate file, try to find final converted file
                                        if (is_temporary) {
                                            // Try to find final file by replacing extension
                                            std::string base_path = path_to_check.substr(0, last_dot);
                                            std::string target_ext = "." + selected_format;
                                            std::string test_final_path = base_path + target_ext;
                                            
                                            // Check if final file exists
                                            if (fileExists(test_final_path)) {
                                                // Final file found - use it
                                                is_temporary = false;
                                                is_final_format = true;
                                            }
                                        }
                                        
                                        // Show button if file has final format (not temporary)
                                        // Don't require item_downloaded flag - file_exists is sufficient
                                        // This ensures button appears immediately after conversion completes
                                        is_final_file = (!is_temporary && is_final_format);
                                    } else {
                                        // No extension - don't show button (file may be incomplete)
                                        is_final_file = false;
                                    }
                                }
                            }
                        }
                        
                        // Show Drag button if file exists and is final file
                        if (file_exists && is_final_file) {
                            // Add horizontal margins (padding) on both sides of Drag button
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + drag_button_margin); // Left margin
                            
                            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
                            // Use unique ID for button to avoid conflicts
                            // float drag_button_height = ImGui::GetFrameHeight() + 5.0f; // Increased height
                            std::string drag_button_id = "Drag##drag_" + std::to_string(i) + "_" + std::to_string(j);
                            ImGui::Button(drag_button_id.c_str(), ImVec2(drag_button_width, 0));
                            
                            // Right margin is handled by table column width (drag_button_width + drag_button_margin * 2)
                            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0, false)) {
                                std::string drag_file_path;
                                
                                // Check if playlist is completed and saved to history
                                bool playlist_completed = (task.status == "completed" || task.status == "already_exists");
                                
                                if (playlist_completed) {
                                    // Playlist is completed and saved to history - use path from history (playlist_items)
                                    if (j < static_cast<int>(task.playlist_items.size()) && !task.playlist_items[j].file_path.empty()) {
                                        drag_file_path = task.playlist_items[j].file_path;
                                    }
                                    
                                    // If not found in history, try from map as fallback
                                    if (drag_file_path.empty() && task.original_ptr && 
                                        task.original_ptr->playlist_item_file_paths.find(j) != task.original_ptr->playlist_item_file_paths.end()) {
                                        drag_file_path = task.original_ptr->playlist_item_file_paths[j];
                                    }
                                } else {
                                    // Playlist is still downloading - use path from beginning of loop (item_file_path)
                                    // This path is updated immediately after conversion and before moving to next track
                                    drag_file_path = item_file_path;
                                    
                                    // If item_file_path is empty, try to get it from map (updated after conversion)
                                    if (drag_file_path.empty() && task.original_ptr && 
                                        task.original_ptr->playlist_item_file_paths.find(j) != task.original_ptr->playlist_item_file_paths.end()) {
                                        drag_file_path = task.original_ptr->playlist_item_file_paths[j];
                                    }
                                    
                                    // If we have a path, check if it's the final format or try to find final file
                                    if (!drag_file_path.empty()) {
                                        std::string selected_format = app ? app->getSettings()->selected_format : "mp3";
                                        size_t last_dot = drag_file_path.find_last_of('.');
                                        
                                        if (last_dot != std::string::npos) {
                                            std::string ext = drag_file_path.substr(last_dot);
                                            std::string target_ext = "." + selected_format;
                                            
                                            // Check if this is a temporary file (.opus, .webm) or intermediate format
                                            bool is_temporary = ((ext == ".opus" || ext == ".webm") ||
                                                                 ((ext == ".mp3" || ext == ".m4a" || ext == ".flac" || ext == ".ogg") &&
                                                                  ext != target_ext));
                                            
                                            // If path points to temporary/intermediate file, try to find final file
                                            if (is_temporary) {
                                                std::string base_path = drag_file_path.substr(0, last_dot);
                                                std::string final_path = base_path + target_ext;
                                                
                                                // Check if final file exists
                                                if (fileExists(final_path)) {
                                                    drag_file_path = final_path;
                                                } else {
                                                    // Final file doesn't exist, can't drag
                                                    drag_file_path.clear();
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                // Verify file exists before dragging
                                if (!drag_file_path.empty() && fileExists(drag_file_path)) {
                                    app->startFileDrag(drag_file_path);
                                }
                                
                                // Last resort fallback: try to find file using display name (only if still empty)
                                if (drag_file_path.empty()) {
                                    std::string base_dir = app->getSettings()->downloads_dir;
                                    if (app->getSettings()->save_playlists_to_separate_folder && !task.playlist_name.empty()) {
                                        // Use playlist name if available
                                        std::string folder_name = app->sanitizeFilename(task.playlist_name);
                                        base_dir += "/" + folder_name;
                                    }
                                    
                                    // Get selected format to use correct extension
                                    std::string selected_format = app ? app->getSettings()->selected_format : "mp3";
                                    
                                    // Try with display name (renamed) first, using correct extension
                                    std::string renamed_path = base_dir + "/" + app->sanitizeFilename(display_name) + "." + selected_format;
#ifdef _WIN32
                                    // Use UTF-16 API on Windows for proper Unicode support
                                    int path_size = MultiByteToWideChar(CP_UTF8, 0, renamed_path.c_str(), -1, NULL, 0);
                                    bool file_found = false;
                                    if (path_size > 0) {
                                        std::wstring wide_path(path_size, 0);
                                        MultiByteToWideChar(CP_UTF8, 0, renamed_path.c_str(), -1, &wide_path[0], path_size);
                                        wide_path.resize(path_size - 1);
                                        
                                        if (fileExists(renamed_path)) {
                                            drag_file_path = renamed_path;
                                            file_found = true;
                                        }
                                    }
                                    if (!file_found) {
                                        // If renamed file doesn't exist, try original name from playlist_items
                                        std::string original_name;
                                        if (j < task.playlist_items.size() && !task.playlist_items[j].title.empty()) {
                                            original_name = task.playlist_items[j].title;
                                        } else {
                                            original_name = "Item " + std::to_string(j + 1);
                                        }
                                        std::string original_path = base_dir + "/" + app->sanitizeFilename(original_name) + "." + selected_format;
                                        int orig_path_size = MultiByteToWideChar(CP_UTF8, 0, original_path.c_str(), -1, NULL, 0);
                                        if (orig_path_size > 0) {
                                            std::wstring wide_orig_path(orig_path_size, 0);
                                            MultiByteToWideChar(CP_UTF8, 0, original_path.c_str(), -1, &wide_orig_path[0], orig_path_size);
                                            wide_orig_path.resize(orig_path_size - 1);
                                            if (fileExists(original_path)) {
                                                drag_file_path = original_path;
                                                file_found = true;
                                            }
                                        }
                                        if (!file_found) {
                                            // Try path from playlist_items
                                            if (j < task.playlist_items.size() && !task.playlist_items[j].file_path.empty()) {
                                                std::string playlist_item_path = task.playlist_items[j].file_path;
                                                int item_path_size = MultiByteToWideChar(CP_UTF8, 0, playlist_item_path.c_str(), -1, NULL, 0);
                                                if (item_path_size > 0) {
                                                    std::wstring wide_item_path(item_path_size, 0);
                                                    MultiByteToWideChar(CP_UTF8, 0, playlist_item_path.c_str(), -1, &wide_item_path[0], item_path_size);
                                                    wide_item_path.resize(item_path_size - 1);
                                                    if (fileExists(playlist_item_path)) {
                                                        drag_file_path = playlist_item_path;
                                                    }
                                                }
                                            }
                                        }
                                    }
#else
                                    if (fileExists(renamed_path)) {
                                        drag_file_path = renamed_path;
                                    } else {
                                        // If renamed file doesn't exist, try original name from playlist_items
                                        std::string original_name;
                                        if (j < task.playlist_items.size() && !task.playlist_items[j].title.empty()) {
                                            original_name = task.playlist_items[j].title;
                                        } else {
                                            original_name = "Item " + std::to_string(j + 1);
                                        }
                                        std::string original_path = base_dir + "/" + app->sanitizeFilename(original_name) + "." + selected_format;
                                        if (fileExists(original_path)) {
                                            drag_file_path = original_path;
                                        } else {
                                            // Try path from playlist_items
                                            if (j < task.playlist_items.size() && !task.playlist_items[j].file_path.empty()) {
                                                std::string playlist_item_path = task.playlist_items[j].file_path;
                                                if (fileExists(playlist_item_path)) {
                                                    drag_file_path = playlist_item_path;
                                                }
                                            }
                                        }
                                    }
#endif
                                    // Verify file exists and start drag
                                    if (!drag_file_path.empty() && fileExists(drag_file_path)) {
                                        app->startFileDrag(drag_file_path);
                                    }
                                }
                            }
                            ImGui::PopStyleVar();
                        }
                        
                        ImGui::EndTable();
                    }
                    ImGui::PopStyleVar(); // Restore cell padding style
                    
                    // CRITICAL: PopID for each item
                    ImGui::PopID();
                }
                // Restore padding after items
                ImGui::PopStyleVar(2);
            }
            // Restore padding after collapsing header (including separator style)
            ImGui::PopStyleVar(3);
            } // Close if (display_count > 0)
        } // Close if (task.is_playlist)
        
        // Add spacing between cards (after playlist dropdown if it exists, or after card if not)
        if (i < render_list.size() - 1) { // Don't add spacing after last card
            ImGui::Dummy(ImVec2(0, 5.0f));
        }
        ImGui::PopID();
    }
    
    // Remove active tasks after rendering (with lock)
    if (!tasks_to_remove.empty()) {
        std::sort(tasks_to_remove.begin(), tasks_to_remove.end(), std::greater<size_t>());
    }
    
    bool removed_any = false;
    std::vector<std::string> urls_to_delete_from_history;  // Collect URLs to delete outside lock
    
    if (!tasks_to_remove.empty()) {
        std::lock_guard<std::mutex> lock(app->tasks_mutex_);
        for (size_t ptr_val : tasks_to_remove) {
            DownloadTask* task_ptr = reinterpret_cast<DownloadTask*>(ptr_val);
            // Find task by pointer
            auto it = std::find_if(app->tasks_.begin(), app->tasks_.end(), 
                [task_ptr](const std::unique_ptr<DownloadTask>& t) { return t.get() == task_ptr; });
            if (it != app->tasks_.end()) {
                auto& task = *it;
                // Track deleted URL if it's a completed/error/cancelled task
                // Collect URL to delete AFTER releasing the lock to avoid deadlock
                if (task->status == "completed" || task->status == "error" || task->status == "cancelled" || task->status == "already_exists") {
                    urls_to_delete_from_history.push_back(task->url);
                }
                if (task->status == "downloading") {
                    app->cancelDownload(task.get());
                    // Don't wait - cancellation will be handled by cleanup
                    // Removed sleep to avoid blocking during shutdown
                }
                if (task->status == "downloading" && app->active_downloads_ > 0) {
                    app->active_downloads_--;
                }
                app->tasks_.erase(it);
                removed_any = true;
            }
        }
        app->active_downloads_ = 0;
        for (const auto& t : app->tasks_) {
            if (t->status == "downloading") {
                app->active_downloads_++;
            }
        }
    }
    
    // Delete URLs from history OUTSIDE the tasks_mutex_ lock to avoid deadlock
    for (const auto& url : urls_to_delete_from_history) {
        app->deleteUrlFromHistory(url);
    }
    
    // Remove history items by ID (more reliable than index)
    if (!history_to_remove_by_id.empty()) {
        history_to_remove_by_id.erase(std::unique(history_to_remove_by_id.begin(), history_to_remove_by_id.end()), history_to_remove_by_id.end());
        for (const std::string& id : history_to_remove_by_id) {
            if (!id.empty() && app->history_manager_) {
                // Get URL before deletion for task cleanup
                std::string deleted_url;
                {
                    const auto& history_items = app->getHistoryItems();
                    auto it = std::find_if(history_items.begin(), history_items.end(),
                        [&id](const HistoryItem& item) { return item.id == id; });
                    if (it != history_items.end()) {
                        deleted_url = it->url;
                    }
                }
                
                // Delete via HistoryManager using ID
                app->history_manager_->deleteItemById(id);
                
                // Also remove any corresponding active task with the same URL
                if (!deleted_url.empty()) {
                    std::lock_guard<std::mutex> lock(app->tasks_mutex_);
                    auto it = std::find_if(app->tasks_.begin(), app->tasks_.end(),
                        [&deleted_url](const std::unique_ptr<DownloadTask>& t) {
                            return t && t->url == deleted_url;
                        });
                    if (it != app->tasks_.end()) {
                        auto& task = *it;
                        if (task->status == "downloading") {
                            app->cancelDownload(task.get());
                        }
                        if (task->status == "downloading" && app->active_downloads_ > 0) {
                            app->active_downloads_--;
                        }
                        app->tasks_.erase(it);
                        removed_any = true;
                    }
                }
            }
        }
        app->rebuildHistoryViewTasks();
        app->persistHistoryItems();
    }
    
    // Remove history items by index (backward compatibility)
    if (!history_to_remove.empty()) {
        std::sort(history_to_remove.begin(), history_to_remove.end(), std::greater<int>());
        history_to_remove.erase(std::unique(history_to_remove.begin(), history_to_remove.end()), history_to_remove.end());
        for (int idx : history_to_remove) {
            if (idx >= 0 && app->history_manager_) {
                size_t index = static_cast<size_t>(idx);
                if (index < app->history_manager_->getHistoryItemsCount()) {
                    const auto& history_items = app->getHistoryItems();
                    std::string deleted_url = history_items[index].url;
                    // Delete via HistoryManager
                    app->history_manager_->deleteItemByIndex(index);
                    
                    // Also remove any corresponding active task with the same URL
                    {
                        std::lock_guard<std::mutex> lock(app->tasks_mutex_);
                        auto it = std::find_if(app->tasks_.begin(), app->tasks_.end(),
                            [&deleted_url](const std::unique_ptr<DownloadTask>& t) {
                                return t && t->url == deleted_url;
                            });
                        if (it != app->tasks_.end()) {
                            auto& task = *it;
                            if (task->status == "downloading") {
                                app->cancelDownload(task.get());
                            }
                            if (task->status == "downloading" && app->active_downloads_ > 0) {
                                app->active_downloads_--;
                            }
                            app->tasks_.erase(it);
                            removed_any = true;
                        }
                    }
                }
            }
        }
        app->rebuildHistoryViewTasks();
        app->persistHistoryItems();
    }
    
    if (removed_any) {
        // Call rewriteHistoryFromTasks asynchronously to avoid blocking UI
        app->runBackground([app]() {
            app->rewriteHistoryFromTasks();
        });
    }
}

void UIRenderer::renderSettings() {
    if (!app_context_) return;
    
    App* app = app_context_;
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Format selection
    const char* formats[] = { "mp3", "m4a", "flac", "ogg" };
    int format_idx = 0;
    for (int i = 0; i < 4; i++) {
        if (app->getSettings()->selected_format == formats[i]) {
            format_idx = i;
            break;
        }
    }
    
    if (ImGui::Combo("##format", &format_idx, formats, 4)) {
        app->getSettings()->selected_format = formats[format_idx];
    }
    ImGui::SameLine();
    ImGui::Text("Audio Format");
    
    // Quality selection
    const char* qualities[] = { "best", "320k", "256k", "192k", "128k" };
    int quality_idx = 0;
    for (int i = 0; i < 5; i++) {
        if (app->getSettings()->selected_quality == qualities[i]) {
            quality_idx = i;
            break;
        }
    }
  
    if (ImGui::Combo("##quality", &quality_idx, qualities, 5)) {
        app->getSettings()->selected_quality = qualities[quality_idx];
    }
    ImGui::SameLine();
    ImGui::Text("Quality");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Downloads directory (move up after quality)
    ImGui::Text("Downloads Directory:");
    ImGui::TextWrapped("%s", app->getSettings()->downloads_dir.c_str());
    ImGui::Spacing();
    float button_width = (ImGui::GetContentRegionAvail().x - 10.0f) / 2.0f; // Two buttons with spacing
    if (ImGui::Button("Select Folder", ImVec2(button_width, 0))) {
        app->selectDownloadsFolder();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Folder", ImVec2(button_width, 0))) {
        app->openDownloadsFolder();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Playlist settings (after folder selection)
    // Playlists are always enabled - type is determined automatically by number of elements
        ImGui::Spacing();
        if (ImGui::Checkbox("Save Playlists to Separate Folder", &app->getSettings()->save_playlists_to_separate_folder)) {
            // Settings will be saved when panel closes
        }
        ImGui::TextDisabled("When enabled, playlists will be saved \n to a subfolder named after the playlist");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Proxy settings
    if (ImGui::Checkbox("Use Proxy", &app->getSettings()->use_proxy)) {
        // Settings will be saved when panel closes
    }
    if (app->getSettings()->use_proxy) {
        ImGui::Spacing();
        static char proxy_buf[256] = "";
        // Initialize from settings_->proxy_input if empty
        if (proxy_buf[0] == '\0' && !app->getSettings()->proxy_input.empty()) {
            strncpy(proxy_buf, app->getSettings()->proxy_input.c_str(), sizeof(proxy_buf) - 1);
            proxy_buf[sizeof(proxy_buf) - 1] = '\0';
        }
        ImGui::Text("Proxy URL:");
        ImGui::SetNextItemWidth(-1.0f); // Full width
        if (ImGui::InputText("##proxy", proxy_buf, sizeof(proxy_buf))) {
            app->getSettings()->proxy_input = proxy_buf;
        }
        ImGui::TextDisabled("Format: protocol://ip:port (e.g. socks5://127.0.0.1:1080)");
        ImGui::TextDisabled("Or just ip:port (will use http:// by default)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    // yt-dlp Advanced Settings
    ImGui::Text("yt-dlp Advanced Settings:");
    ImGui::Spacing();
    
    // Cookies for playlists - two mutually exclusive options
    if (ImGui::Checkbox("Use Browser Cookies (Playlists)", &app->getSettings()->ytdlp_use_cookies_for_playlists)) {
        // If browser cookies enabled, disable cookies file
        if (app->getSettings()->ytdlp_use_cookies_for_playlists) {
            app->getSettings()->ytdlp_use_cookies_file = false;
        }
    }
    if (app->getSettings()->ytdlp_use_cookies_for_playlists) {
        ImGui::Indent(20.0f);
        // Dropdown first, then label on the right
        ImGui::SetNextItemWidth(150.0f);
        
        // Browser list for dropdown - use BrowserUtils
        int browser_count = BrowserUtils::getBrowserCount();
        const char* browsers[7]; // Maximum 7 browsers (macOS)
        for (int i = 0; i < browser_count; i++) {
            browsers[i] = BrowserUtils::getBrowserName(i);
        }
        
        // Ensure index is within bounds
        if (app->getSettings()->ytdlp_selected_browser_index >= browser_count) {
            app->getSettings()->ytdlp_selected_browser_index = 0;
        }
        
        if (ImGui::Combo("##browser_priority", &app->getSettings()->ytdlp_selected_browser_index, browsers, browser_count)) {
            // Settings will be saved when panel closes
        }
        ImGui::SameLine(0, 8.0f);
        ImGui::Text("Browser Priority:");
        ImGui::Unindent(20.0f);
    }
    ImGui::TextDisabled("Use browser cookies for playlist downloads");
    
    ImGui::Spacing();
    
    // Cookies file option (mutually exclusive with browser cookies)
    if (ImGui::Checkbox("Use Cookies File (Playlists)", &app->getSettings()->ytdlp_use_cookies_file)) {
        // If cookies file enabled, disable browser cookies
        if (app->getSettings()->ytdlp_use_cookies_file) {
            app->getSettings()->ytdlp_use_cookies_for_playlists = false;
        }
    }
    if (app->getSettings()->ytdlp_use_cookies_file) {
        ImGui::Indent(20.0f);
        
        // Display current path or placeholder
        static char cookies_file_buf[512] = "";
        if (cookies_file_buf[0] == '\0' && !app->getSettings()->ytdlp_cookies_file_path.empty()) {
            strncpy(cookies_file_buf, app->getSettings()->ytdlp_cookies_file_path.c_str(), sizeof(cookies_file_buf) - 1);
            cookies_file_buf[sizeof(cookies_file_buf) - 1] = '\0';
        }
        
        ImGui::Text("Cookies File:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##cookies_file", cookies_file_buf, sizeof(cookies_file_buf))) {
            app->getSettings()->ytdlp_cookies_file_path = cookies_file_buf;
        }
        
        ImGui::Spacing();
        if (ImGui::Button("Select File", ImVec2(-1.0f, 0))) {
            std::string file_path;
            if (PlatformUtils::selectFileDialogWithWindow(app->getWindowManager() ? app->getWindowManager()->getWindow() : nullptr, file_path, "txt")) {
                if (!file_path.empty()) {
                    app->getSettings()->ytdlp_cookies_file_path = file_path;
                    size_t len = (std::min)(file_path.length(), sizeof(cookies_file_buf) - 1);  // Use parentheses to avoid Windows min/max macro conflict
                    file_path.copy(cookies_file_buf, len);
                    cookies_file_buf[len] = '\0';
                }
            }
        }
        
        ImGui::Unindent(20.0f);
    }
    ImGui::TextDisabled("Use cookies file for playlist downloads");
    
    ImGui::Spacing();
    
    // Sleep intervals for playlists
    if (ImGui::Checkbox("Use Sleep Intervals (Playlists)", &app->getSettings()->ytdlp_use_sleep_intervals_playlist)) {
        // Settings will be saved when panel closes
    }
    if (app->getSettings()->ytdlp_use_sleep_intervals_playlist) {
        ImGui::Indent(20.0f);
        // Input first, then label immediately after
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("##sleep_interval_playlist", &app->getSettings()->ytdlp_playlist_sleep_interval, 0);
        app->getSettings()->ytdlp_playlist_sleep_interval = (std::max)(0, app->getSettings()->ytdlp_playlist_sleep_interval);  // Use parentheses to avoid Windows min/max macro conflict
        ImGui::SameLine(0, 8.0f);
        ImGui::Text("Sleep Interval:");
        
        // Input first, then label immediately after
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("##max_sleep_interval_playlist", &app->getSettings()->ytdlp_playlist_max_sleep_interval, 0);
        app->getSettings()->ytdlp_playlist_max_sleep_interval = (std::max)(0, app->getSettings()->ytdlp_playlist_max_sleep_interval);  // Use parentheses to avoid Windows min/max macro conflict
        ImGui::SameLine(0, 8.0f);
        ImGui::Text("Max Sleep Interval:");
        ImGui::Unindent(20.0f);
    }
    ImGui::TextDisabled("Add delays between requests for playlists");
    
    ImGui::Spacing();
    
    // Sleep requests
    if (ImGui::Checkbox("Use Sleep Requests (Playlists)", &app->getSettings()->ytdlp_use_sleep_requests)) {
        // Settings will be saved when panel closes
    }
    if (app->getSettings()->ytdlp_use_sleep_requests) {
        ImGui::Indent(20.0f);
        // Input first, then label immediately after
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("##sleep_requests", &app->getSettings()->ytdlp_playlist_sleep_requests, 0);
        app->getSettings()->ytdlp_playlist_sleep_requests = (std::max)(0, app->getSettings()->ytdlp_playlist_sleep_requests);  // Use parentheses to avoid Windows min/max macro conflict
        ImGui::SameLine(0, 8.0f);
        ImGui::Text("Sleep Requests:");
        ImGui::Unindent(20.0f);
    }
    ImGui::TextDisabled("Sleep after N requests for playlists");
    
    ImGui::Spacing();
    
    // Socket timeout
    if (ImGui::Checkbox("Use Socket Timeout", &app->getSettings()->ytdlp_use_socket_timeout)) {
        // Settings will be saved when panel closes
    }
    if (app->getSettings()->ytdlp_use_socket_timeout) {
        ImGui::Indent(20.0f);
        // Input first, then label immediately after
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("##socket_timeout", &app->getSettings()->ytdlp_socket_timeout, 0);
        app->getSettings()->ytdlp_socket_timeout = (std::max)(10, (std::min)(600, app->getSettings()->ytdlp_socket_timeout));  // Clamp between 10 and 600 seconds, use parentheses to avoid Windows min/max macro conflict
        ImGui::SameLine(0, 8.0f);
        ImGui::Text("Socket Timeout (seconds):");
        ImGui::Unindent(20.0f);
    }
    ImGui::TextDisabled("Timeout for download connections \n (default: 120, recommended: 60-180)");
    
    ImGui::Spacing();
    
    // Fragment retries
    if (ImGui::Checkbox("Use Fragment Retries", &app->getSettings()->ytdlp_use_fragment_retries)) {
        // Settings will be saved when panel closes
    }
    if (app->getSettings()->ytdlp_use_fragment_retries) {
        ImGui::Indent(20.0f);
        // Input first, then label immediately after
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("##fragment_retries", &app->getSettings()->ytdlp_fragment_retries, 0);
        app->getSettings()->ytdlp_fragment_retries = (std::max)(1, (std::min)(50, app->getSettings()->ytdlp_fragment_retries));  // Clamp between 1 and 50, use parentheses to avoid Windows min/max macro conflict
        ImGui::SameLine(0, 8.0f);
        ImGui::Text("Fragment Retries:");
        ImGui::Unindent(20.0f);
    }
    ImGui::TextDisabled("Number of retries for HLS fragments \n (default: 10, important for SoundCloud)");
    
    ImGui::Spacing();
    
    // Concurrent fragments
    if (ImGui::Checkbox("Use Concurrent Fragments", &app->getSettings()->ytdlp_use_concurrent_fragments)) {
        // Settings will be saved when panel closes
    }
    if (app->getSettings()->ytdlp_use_concurrent_fragments) {
        ImGui::Indent(20.0f);
        // Input first, then label immediately after
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputInt("##concurrent_fragments", &app->getSettings()->ytdlp_concurrent_fragments, 0);
        app->getSettings()->ytdlp_concurrent_fragments = (std::max)(1, (std::min)(4, app->getSettings()->ytdlp_concurrent_fragments));  // Clamp between 1 and 4, use parentheses to avoid Windows min/max macro conflict
        ImGui::SameLine(0, 8.0f);
        ImGui::Text("Concurrent Fragments:");
        ImGui::Unindent(20.0f);
    }
    ImGui::TextDisabled("Number of parallel fragments for HLS downloads \n (default: 2, max: 4, recommended: 2-4 for faster downloads)");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // Service availability check
    ImGui::Text("Service Status:");
    {
        ServiceChecker::ServiceStatus status = app->getServiceChecker()->getStatus();
        const char* status_text = "";
        if (status == ServiceChecker::SERVICE_UNCHECKED) {
            status_text = "Not checked";
        } else if (status == ServiceChecker::SERVICE_CHECKING) {
            status_text = "Checking...";
        } else if (status == ServiceChecker::SERVICE_AVAILABLE) {
            status_text = "Available";
        } else {
            status_text = "Unavailable";
        }
        ImGui::TextDisabled("%s", status_text);
    }
    ImGui::Spacing();
    if (ImGui::Button("Check Service Availability", ImVec2(-1, 0))) {
        app->checkServiceAvailability(true, false); // Force check (ignore cache) when user clicks button
    }
    ImGui::TextDisabled("Test if download services are working");
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    // yt-dlp management (moved to bottom)
    ImGui::Text("yt-dlp:");
    ImGui::SameLine();
    const char* version_label = app->getSettings()->ytdlp_version.empty() ? "Unknown" : app->getSettings()->ytdlp_version.c_str();
    ImGui::TextDisabled("Version: %s", version_label);
    ImGui::Spacing();
    
    ImGui::BeginGroup();
    if (app->ytdlp_update_in_progress_) {
        ImGui::BeginDisabled();
        ImGui::Button("Updating...", ImVec2(-1, 0));
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Update yt-dlp", ImVec2(-1, 0))) {
            app->updateYtDlp();
        }
    }
    ImGui::TextDisabled("Updates bundled yt-dlp using -U");
    ImGui::EndGroup();
    
    if (!app->ytdlp_update_status_.empty()) {
        ImGui::Spacing();
        // Color status text based on success/failure
        if (app->ytdlp_update_status_.find("successfully") != std::string::npos) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255)); // Green for success
            ImGui::TextWrapped("%s", app->ytdlp_update_status_.c_str());
            ImGui::PopStyleColor();
        } else if (app->ytdlp_update_status_.find("failed") != std::string::npos) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255)); // Red for failure
            ImGui::TextWrapped("%s", app->ytdlp_update_status_.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextWrapped("%s", app->ytdlp_update_status_.c_str());
        }
    }
    
    // ffmpeg status (use PathFinder to check bundled first, then system)
    {
        std::string ffmpeg_status = "Not found";
        std::string ffmpeg_path = PathFinder::findFfmpegPath();
        
        if (!ffmpeg_path.empty() && ffmpeg_path != "ffmpeg") {
            // Check if it's in the executable directory (bundled/Release)
#ifdef _WIN32
            char exe_path[MAX_PATH];
            DWORD length = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
            if (length > 0 && length < MAX_PATH) {
                std::string exe_dir = exe_path;
                size_t last_slash = exe_dir.find_last_of("\\/");
                if (last_slash != std::string::npos) {
                    exe_dir = exe_dir.substr(0, last_slash);
                    // Check if ffmpeg is in the same directory as executable
                    if (ffmpeg_path.find(exe_dir) == 0) {
                        ffmpeg_status = "in Release";
                    } else {
                        ffmpeg_status = "in system";
                    }
                } else {
                    ffmpeg_status = "in system";
                }
            } else {
                ffmpeg_status = "in system";
            }
#elif defined(__APPLE__)
            // Check if it's in app bundle
            if (ffmpeg_path.find("/Contents/Resources/") != std::string::npos) {
                ffmpeg_status = "in bundle";
            } else {
                ffmpeg_status = "in system";
            }
#else
            // Linux: check if it's in a local directory (not /usr/bin, /bin, etc.)
            if (ffmpeg_path.find("/usr/") == 0 || ffmpeg_path.find("/bin/") == 0) {
                ffmpeg_status = "in system";
            } else {
                ffmpeg_status = "local";
            }
#endif
        } else if (ffmpeg_path == "ffmpeg") {
            // Fallback: will use system PATH
            ffmpeg_status = "in system";
        }
        
        ImGui::Spacing();
        ImGui::Text("ffmpeg:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ffmpeg_status.c_str());
    }
    
    // Extra padding at very bottom of settings panel
    ImGui::Spacing();
    ImGui::Spacing();
}

// Base64 decoding helper
static bool is_base64_char(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

static std::vector<unsigned char> base64_decode(const std::string& encoded_string) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> ret;
    int in_len = encoded_string.size();
    int i = 0;
    int in = 0;
    unsigned char char_array_4[4], char_array_3[3];
    
    while (in_len-- && (encoded_string[in] != '=') && is_base64_char(encoded_string[in])) {
        char_array_4[i++] = encoded_string[in]; in++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = chars.find(char_array_4[i]);
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; (i < 3); i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }
    
    if (i) {
        for (int j = i; j < 4; j++)
            char_array_4[j] = 0;
        
        for (int j = 0; j < 4; j++)
            char_array_4[j] = chars.find(char_array_4[j]);
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
        
        for (int j = 0; (j < i - 1); j++) ret.push_back(char_array_3[j]);
    }
    
    return ret;
}

UIRenderer::ThumbnailData* UIRenderer::loadThumbnailFromBase64(const std::string& base64_data) {
    if (base64_data.empty()) {
        return nullptr;
    }
    
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(thumbnail_cache_mutex_);
        auto it = thumbnail_cache_.find(base64_data);
        if (it != thumbnail_cache_.end()) {
            return &it->second;
        }
    }
    
    // Decode base64
    std::vector<unsigned char> image_data = base64_decode(base64_data);
    if (image_data.empty()) {
        return nullptr;
    }
    
    // Create temporary file
#ifdef _WIN32
    char temp_path[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_path) == 0) {
        return nullptr;
    }
    std::string temp_file = std::string(temp_path) + "ytdaudio_thumb_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".jpg";
#else
    std::string temp_file = "/tmp/ytdaudio_thumb_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".jpg";
#endif
    std::ofstream out(temp_file, std::ios::binary);
    if (!out) {
        return nullptr;
    }
    out.write(reinterpret_cast<const char*>(image_data.data()), image_data.size());
    out.close();
    
    // Load image using SDL_image or system command
    SDL_Surface* surface = nullptr;
    
#ifdef SDL_IMAGE_FOUND
    // Use SDL_image to load JPEG/PNG directly (preferred method)
    surface = IMG_Load(temp_file.c_str());
    if (!surface) {
        std::cout << "[DEBUG] UIRenderer::loadThumbnailFromBase64: IMG_Load failed: " << IMG_GetError() << std::endl;
    }
    // Clean up temp file
#ifdef _WIN32
    _unlink(temp_file.c_str());
#else
    unlink(temp_file.c_str());
#endif
#elif _WIN32
    // On Windows without SDL_image, try to use Windows API or skip
    // For now, skip thumbnail loading to avoid errors
    _unlink(temp_file.c_str());
    return nullptr;  // Skip thumbnail loading on Windows without SDL_image
#elif __APPLE__
    // Use sips on macOS
    std::string bmp_file = temp_file + ".bmp";
    std::string command = "sips -s format bmp \"" + temp_file + "\" --out \"" + bmp_file + "\" >/dev/null 2>&1";
    int result = system(command.c_str());
    if (result == 0 && access(bmp_file.c_str(), F_OK) == 0) {
        surface = SDL_LoadBMP(bmp_file.c_str());
        unlink(bmp_file.c_str());
    }
    unlink(temp_file.c_str());
#else
    // Use ImageMagick convert on Linux
    std::string bmp_file = temp_file + ".bmp";
    std::string command = "convert \"" + temp_file + "\" \"" + bmp_file + "\" >/dev/null 2>&1";
    int result = system(command.c_str());
    if (result == 0 && access(bmp_file.c_str(), F_OK) == 0) {
        surface = SDL_LoadBMP(bmp_file.c_str());
        unlink(bmp_file.c_str());
    }
    unlink(temp_file.c_str());
#endif
    
    if (!surface) {
        return nullptr;
    }
    
    // Get renderer from App
    SDL_Renderer* renderer = app_context_->getWindowManager() ? app_context_->getWindowManager()->getRenderer() : nullptr;
    if (!renderer) {
        SDL_FreeSurface(surface);
        return nullptr;
    }
    
    // Get image dimensions from surface before creating texture
    int img_width = surface->w;
    int img_height = surface->h;
    
    // Create texture from surface
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    
    if (!texture) {
        return nullptr;
    }
    
    // Cache the texture with dimensions
    ThumbnailData data;
    data.texture = texture;
    data.width = img_width;
    data.height = img_height;
    
    {
        std::lock_guard<std::mutex> lock(thumbnail_cache_mutex_);
        thumbnail_cache_[base64_data] = data;
        return &thumbnail_cache_[base64_data];
    }
}

void UIRenderer::drawThumbnail(const std::string& thumbnail_base64, float max_width, float column_height, const std::string& platform) {
    // If thumbnail is empty, use placeholder
    std::string actual_thumbnail = thumbnail_base64.empty() ? PLACEHOLDER_THUMBNAIL_BASE64 : thumbnail_base64;
    
    ThumbnailData* data = loadThumbnailFromBase64(actual_thumbnail);
    if (!data || !data->texture) {
        // If loading failed, try placeholder as fallback (if we weren't already using it)
        if (!thumbnail_base64.empty()) {
            data = loadThumbnailFromBase64(PLACEHOLDER_THUMBNAIL_BASE64);
            if (!data || !data->texture) {
                return;
            }
        } else {
            return;
        }
    }
    
    // Calculate display size preserving aspect ratio
    // Width is limited to max_width (70px), height is calculated automatically
    float aspect_ratio = static_cast<float>(data->width) / static_cast<float>(data->height);
    float display_width = max_width;
    float display_height = display_width / aspect_ratio;
    
    // Limit height to column height if image is too tall (prevent overflow)
    if (display_height > column_height) {
        display_height = column_height;
        display_width = display_height * aspect_ratio;
        // If width exceeds max_width after height adjustment, scale down proportionally
        if (display_width > max_width) {
            float scale = max_width / display_width;
            display_width = max_width;
            display_height *= scale;
        }
    }
    
    // Center vertically in column - perfect centering for all platforms
    // Get the starting position of the column (should be at the top)
    float column_start_y = ImGui::GetCursorPosY();
    
    // Calculate vertical offset to center the thumbnail in the column
    // This is the space above the image
    float vertical_offset = (column_height - display_height) * 0.5f;
    
    // Always add vertical offset to center the thumbnail (even if 0 or negative, we handle it)
    // If offset is negative, it means image is taller than column, so start at 0
    if (vertical_offset > 0) {
        ImGui::Dummy(ImVec2(0, vertical_offset));
    }
    
    // Center horizontally if image is narrower than column
    float current_x = ImGui::GetCursorPosX();
    if (display_width < max_width) {
        float offset_x = (max_width - display_width) * 0.5f;
        ImGui::SetCursorPosX(current_x + offset_x);
    }
    
    // Draw thumbnail image
    SDL_Texture* texture = static_cast<SDL_Texture*>(data->texture);
    ImGui::Image(reinterpret_cast<ImTextureID>(texture), ImVec2(display_width, display_height));
    
    // Calculate remaining space at the bottom to ensure we fill exactly column_height
    float current_y = ImGui::GetCursorPosY();
    float used_height = current_y - column_start_y;
    float remaining_height = column_height - used_height;
    
    // Add remaining space at the bottom to fill the column height exactly
    if (remaining_height > 0) {
        ImGui::Dummy(ImVec2(0, remaining_height));
    }
}

