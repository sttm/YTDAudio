#include "event_handler.h"
#include "imgui_impl_sdl2.h"
#include <cstring>

EventHandler::EventHandler()
    : paste_callback_(nullptr)
{
}

EventHandler::~EventHandler() {
}

EventHandler::EventResult EventHandler::processEvents() {
    EventResult result;
    result.should_quit = false;
    result.window_resized = false;
    result.new_width = 0;
    result.new_height = 0;
    result.paste_requested = false;
    result.file_dropped = false;
    result.dropped_file_path.clear();
    
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Process event for ImGui first
        ImGui_ImplSDL2_ProcessEvent(&event);
        
        switch (event.type) {
            case SDL_QUIT:
                result.should_quit = true;
                break;
                
            case SDL_WINDOWEVENT:
                handleWindowEvent(event, result);
                break;
                
            case SDL_KEYDOWN:
                handleKeyEvent(event, result);
                break;
                
            case SDL_DROPFILE:
                handleDropEvent(event, result);
                break;
                
            default:
                break;
        }
    }
    
    return result;
}

void EventHandler::setPasteCallback(std::function<void(const std::string&)> callback) {
    paste_callback_ = callback;
}

void EventHandler::handleWindowEvent(const SDL_Event& event, EventResult& result) {
    if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
        result.window_resized = true;
        result.new_width = event.window.data1;
        result.new_height = event.window.data2;
    }
}

void EventHandler::handleKeyEvent(const SDL_Event& event, EventResult& result) {
    // Handle paste shortcut using scan codes (physical key positions)
    // This works regardless of keyboard layout because scan codes are layout-independent
    if (isModifierPressed() && event.key.keysym.scancode == SDL_SCANCODE_V) {
        // Get clipboard content
        const char* clipboard_text = ImGui::GetClipboardText();
        if (clipboard_text != nullptr && strlen(clipboard_text) > 0) {
            result.paste_requested = true;
            if (paste_callback_) {
                paste_callback_(std::string(clipboard_text));
            }
        }
    }
}

void EventHandler::handleDropEvent(const SDL_Event& event, EventResult& result) {
    if (event.drop.file) {
        result.file_dropped = true;
        result.dropped_file_path = event.drop.file;
        SDL_free(event.drop.file);
    }
}

bool EventHandler::isModifierPressed() const {
    const Uint8* keystate = SDL_GetKeyboardState(NULL);
    
#ifdef __APPLE__
    // On macOS, check for Cmd (Super/GUI) or Ctrl
    return (keystate[SDL_SCANCODE_LGUI] || keystate[SDL_SCANCODE_RGUI] || 
            keystate[SDL_SCANCODE_LCTRL] || keystate[SDL_SCANCODE_RCTRL]);
#else
    // On other platforms, check for Ctrl
    return (keystate[SDL_SCANCODE_LCTRL] || keystate[SDL_SCANCODE_RCTRL]);
#endif
}

