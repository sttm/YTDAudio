#pragma once

#include <string>
#include <functional>
#include <SDL.h>

// Forward declaration
class App;

class FileManager {
public:
    FileManager(App* app_context);
    ~FileManager();
    
    // File operations
    void openDownloadsFolder(const std::string& downloads_dir);
    void openFileLocation(const std::string& file_path);
    void startFileDrag(SDL_Window* window, const std::string& file_path);
    void selectDownloadsFolder(SDL_Window* window, std::function<void(const std::string&)> on_folder_selected);
    
private:
    App* app_context_;  // Pointer to App for accessing data
};

