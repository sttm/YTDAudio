#include "file_manager.h"
#include "../app.h"
#include "../platform/platform_utils.h"
#include "../common/validation_utils.h"
#include <functional>

FileManager::FileManager(App* app_context) 
    : app_context_(app_context) {
}

FileManager::~FileManager() {
}

void FileManager::openDownloadsFolder(const std::string& downloads_dir) {
    PlatformUtils::openFolder(downloads_dir);
}

void FileManager::openFileLocation(const std::string& file_path) {
    if (!ValidationUtils::isValidPath(file_path)) {
        return;
    }
    PlatformUtils::openFileLocation(file_path);
}

void FileManager::startFileDrag(SDL_Window* window, const std::string& file_path) {
    if (!ValidationUtils::isValidPath(file_path)) {
        return;
    }
    PlatformUtils::startFileDrag(window, file_path);
}

void FileManager::selectDownloadsFolder(SDL_Window* window, std::function<void(const std::string&)> on_folder_selected) {
    // Run folder selection in a separate thread to avoid blocking UI
    app_context_->runBackground([this, window, on_folder_selected]() {
        try {
            std::string new_dir;
            bool success = false;
            if (window != nullptr) {
                success = PlatformUtils::selectFolderDialogWithWindow(window, new_dir);
            } else {
                success = PlatformUtils::selectFolderDialog(new_dir);
            }
            
            if (success && !new_dir.empty() && on_folder_selected) {
                on_folder_selected(new_dir);
            }
        } catch (...) {
            // Swallow exceptions to avoid std::terminate from background thread
        }
    });
}


