#include "platform_utils.h"
#include "../common/thumbnail_downloader.h"
#include "../common/windows_utils.h"  // For ::fileExists (cross-platform implementation)
#include <iostream>
#include <cstring>
#include <fstream>
#include <SDL_syswm.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include <errno.h>
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

namespace PlatformUtils {

// macOS implementations


bool selectFolderDialog(std::string& path) {
    std::string command = "osascript -e 'set theFolder to choose folder with prompt \"Select Downloads Folder\"' -e 'return POSIX path of theFolder'";
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return false;
    }
    
    std::string result;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    int exitCode = pclose(pipe);
    
    // Remove trailing whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ' || result.back() == '\t')) {
        result.pop_back();
    }
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t' || result.front() == '\n' || result.front() == '\r')) {
        result.erase(0, 1);
    }
    
    if (!result.empty() && exitCode == 0) {
        path = result;
        return true;
    }
    return false;
}

bool selectFolderDialogWithWindow(SDL_Window* window, std::string& path) {
    (void)window; // osascript doesn't need window handle
    return selectFolderDialog(path);
}

bool selectFileDialog(std::string& path, const std::string& file_types) {
    std::string command = "osascript -e 'set theFile to choose file with prompt \"Select Cookies File\"";
    if (!file_types.empty()) {
        command += " of type {\"public.text\", \"public.data\"}";
    }
    command += "' -e 'return POSIX path of theFile'";
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return false;
    }
    
    std::string result;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    int exitCode = pclose(pipe);
    
    // Remove trailing whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ' || result.back() == '\t')) {
        result.pop_back();
    }
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t' || result.front() == '\n' || result.front() == '\r')) {
        result.erase(0, 1);
    }
    
    if (!result.empty() && exitCode == 0) {
        path = result;
        return true;
    }
    return false;
}

bool selectFileDialogWithWindow(SDL_Window* window, std::string& path, const std::string& file_types) {
    (void)window; // osascript doesn't need window handle
    return selectFileDialog(path, file_types);
}

} // namespace PlatformUtils

// macOS drag & drop implementation (must be outside namespace for Objective-C)
@interface FileDragSource : NSObject <NSDraggingSource>
@end

@implementation FileDragSource
- (NSDragOperation)draggingSession:(NSDraggingSession *)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    return NSDragOperationCopy;
}
@end

namespace PlatformUtils {

bool startFileDrag(SDL_Window* window, const std::string& file_path) {
    @autoreleasepool {
        NSString* filePath = [NSString stringWithUTF8String:file_path.c_str()];
        
        if (![[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
            return false;
        }
        
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        
        if (!SDL_GetWindowWMInfo(window, &wmInfo)) {
            return false;
        }
        
        if (wmInfo.subsystem != SDL_SYSWM_COCOA) {
            return false;
        }
        
        NSWindow* nsWindow = wmInfo.info.cocoa.window;
        NSView* view = [nsWindow contentView];
        
        FileDragSource* dragSource = [[FileDragSource alloc] init];
        NSURL* fileURL = [NSURL fileURLWithPath:filePath];
        
        NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
        [item setData:[fileURL dataRepresentation] forType:NSPasteboardTypeFileURL];
        
        NSDraggingItem* draggingItem = [[NSDraggingItem alloc] initWithPasteboardWriter:item];
        
        NSPoint mouseLoc = [NSEvent mouseLocation];
        NSPoint windowLoc = [nsWindow convertPointFromScreen:mouseLoc];
        NSPoint viewLoc = [view convertPoint:windowLoc fromView:nil];
        
        NSRect dragFrame = NSMakeRect(viewLoc.x - 16, viewLoc.y - 16, 32, 32);
        
        NSImage* dragImage = [[NSWorkspace sharedWorkspace] iconForFile:filePath];
        dragImage.size = NSMakeSize(32, 32);
        [draggingItem setDraggingFrame:dragFrame contents:dragImage];
        
        NSEvent* currentEvent = [NSApp currentEvent];
        if (!currentEvent) {
            NSPoint location = [view convertPoint:[nsWindow mouseLocationOutsideOfEventStream] fromView:nil];
            NSEvent* syntheticEvent = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                                          location:location
                                                     modifierFlags:0
                                                         timestamp:0
                                                      windowNumber:[nsWindow windowNumber]
                                                           context:nil
                                                       eventNumber:0
                                                        clickCount:1
                                                          pressure:1.0];
            currentEvent = syntheticEvent;
        }
        
        NSDraggingSession* session = [view beginDraggingSessionWithItems:@[draggingItem] 
                                                                   event:currentEvent 
                                                                  source:dragSource];
        [session setDraggingFormation:NSDraggingFormationNone];
        
        return true;
    }
}

void openFileLocation(const std::string& file_path) {
    NSString* path = [NSString stringWithUTF8String:file_path.c_str()];
    [[NSWorkspace sharedWorkspace] selectFile:path inFileViewerRootedAtPath:@""];
}

void openFolder(const std::string& folder_path) {
    NSString* path = [NSString stringWithUTF8String:folder_path.c_str()];
    NSURL* url = [NSURL fileURLWithPath:path];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

std::string getConfigPath() {
    const char* home = getenv("HOME");
    if (home) {
        std::string config_dir = std::string(home) + "/Library/Application Support/YTDAudio";
        mkdir(config_dir.c_str(), 0755);
        return config_dir + "/config.txt";
    }
    return "config.txt";
}

std::string getHistoryPath() {
    const char* home = getenv("HOME");
    if (home) {
        std::string config_dir = std::string(home) + "/Library/Application Support/YTDAudio";
        mkdir(config_dir.c_str(), 0755);
        return config_dir + "/history.json";
    }
    return "history.json";
}

std::string getDownloadsPath() {
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/Music/YTDAudio";
    }
    return ".";
}

std::string getExecutablePath() {
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char resolved_path[PATH_MAX];
        if (realpath(path, resolved_path) != nullptr) {
            return std::string(resolved_path);
        }
        return std::string(path);
    }
    return "";
}

std::string getExecutableDirectory() {
    std::string exe_path = getExecutablePath();
    size_t last_slash = exe_path.find_last_of("/");
    if (last_slash != std::string::npos) {
        return exe_path.substr(0, last_slash);
    }
    return "";
}

bool fileExists(const std::string& file_path) {
    // Delegate to cross-platform implementation in windows_utils.h
    return ::fileExists(file_path);
}

bool createDirectory(const std::string& path) {
    if (path.empty()) return false;
    
    // Check if directory already exists
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return true; // Directory already exists
    }
    
    // Try to create directory
    if (mkdir(path.c_str(), 0755) == 0) {
        return true; // Successfully created
    }
    
    // If mkdir failed, check if it's because directory already exists
    if (errno == EEXIST) {
        // Double-check it's actually a directory
        if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return true;
        }
    }
    
    // Log error for debugging
    std::cout << "[DEBUG] createDirectory: Failed to create " << path << ", errno: " << errno << std::endl;
    return false;
}

std::string downloadThumbnailAsBase64(const std::string& thumbnail_url, bool use_proxy) {
    // Delegate to ThumbnailDownloader namespace which has native NSURLSession implementation
    // This is more efficient than the curl approach and ensures consistent behavior
    return ThumbnailDownloader::downloadThumbnailAsBase64(thumbnail_url, use_proxy);
}

void openURL(const std::string& url) {
    if (url.empty()) return;
    
    NSString* urlString = [NSString stringWithUTF8String:url.c_str()];
    NSURL* nsURL = [NSURL URLWithString:urlString];
    if (nsURL) {
        [[NSWorkspace sharedWorkspace] openURL:nsURL];
    }
}

} // namespace PlatformUtils

#endif // __APPLE__
