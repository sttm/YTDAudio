#ifndef PLATFORM_UTILS_H
#define PLATFORM_UTILS_H

#include <string>

struct SDL_Window;

namespace PlatformUtils {


// File/folder dialogs
bool selectFolderDialog(std::string& path);
bool selectFolderDialogWithWindow(SDL_Window* window, std::string& path);
bool selectFileDialog(std::string& path, const std::string& file_types);
bool selectFileDialogWithWindow(SDL_Window* window, std::string& path, const std::string& file_types);

// File operations
bool startFileDrag(SDL_Window* window, const std::string& file_path);
void openFileLocation(const std::string& file_path);
void openFolder(const std::string& folder_path);
bool createDirectory(const std::string& path);
bool fileExists(const std::string& file_path);  // Check if file exists (Unicode-safe on Windows)

// Path functions
std::string getConfigPath();
std::string getHistoryPath();
std::string getDownloadsPath();
std::string getExecutablePath();
std::string getExecutableDirectory();

// Network/URL functions
std::string downloadThumbnailAsBase64(const std::string& thumbnail_url, bool use_proxy = false);
void openURL(const std::string& url);

} // namespace PlatformUtils

#endif // PLATFORM_UTILS_H
