#pragma once

#include <SDL.h>
#include <imgui.h>
#include <string>
#include <vector>

class WindowManager {
public:
    WindowManager();
    ~WindowManager();
    
    // Initialize SDL and create window/renderer
    bool initialize(int width = 900, int height = 650, const std::string& title = "YTDAudio");
    
    // Cleanup resources
    void cleanup();
    
    // Get window and renderer
    SDL_Window* getWindow() const { return window_; }
    SDL_Renderer* getRenderer() const { return renderer_; }
    
    // Get window dimensions
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    
    // Update window size from SDL
    void updateSize();
    
    // Setup ImGui for this window/renderer
    bool setupImGui();
    
    // Begin ImGui frame
    void beginImGuiFrame();
    
    // End ImGui frame and render
    void endImGuiFrame();
    
    // Load custom font
    bool loadFont(const std::string& font_path, float size = 16.0f);
    
    // Setup ImGui style
    void setupImGuiStyle();
    
private:
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    int width_;
    int height_;
    bool sdl_initialized_;
    bool imgui_initialized_;
    
    void setupMidDarkStyle();
    std::vector<std::string> findFontCandidates();
};

