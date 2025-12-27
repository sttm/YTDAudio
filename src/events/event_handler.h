#pragma once

#include <SDL.h>
#include <imgui.h>
#include <functional>
#include <string>

class EventHandler {
public:
    struct EventResult {
        bool should_quit;
        bool window_resized;
        int new_width;
        int new_height;
        bool paste_requested;
        bool file_dropped;
        std::string dropped_file_path;
    };
    
    EventHandler();
    ~EventHandler();
    
    // Process all pending events
    EventResult processEvents();
    
    // Set callback for paste events (optional)
    void setPasteCallback(std::function<void(const std::string&)> callback);
    
private:
    std::function<void(const std::string&)> paste_callback_;
    
    void handleWindowEvent(const SDL_Event& event, EventResult& result);
    void handleKeyEvent(const SDL_Event& event, EventResult& result);
    void handleDropEvent(const SDL_Event& event, EventResult& result);
    bool isModifierPressed() const;
};

