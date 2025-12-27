#include "path_finder.h"
#include "platform_detector.h"
#include <sys/stat.h>
#include <cstring>
#include <vector>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include "../common/platform_macros.h"
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#else
#include <unistd.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

// Static cache to avoid multiple calls to findYtDlpPath
static std::string cached_ytdlp_path;
static bool ytdlp_path_cached = false;

std::string PathFinder::findYtDlpPath() {
    // CRITICAL: Cache the result to avoid multiple calls and potential duplicate processes
    if (ytdlp_path_cached) {
        std::cout << "[DEBUG] PathFinder::findYtDlpPath: Using cached path: " << cached_ytdlp_path << std::endl;
        return cached_ytdlp_path;
    }
    
    std::cout << "[DEBUG] PathFinder::findYtDlpPath: Searching for yt-dlp (first call)..." << std::endl;
    std::string ytdlp_path;
    
    // Try app bundle first (macOS)
    if (PlatformDetector::isMacOS()) {
        std::cout << "[DEBUG] PathFinder::findYtDlpPath: Checking app bundle (macOS)..." << std::endl;
        std::string bundle_path = findInAppBundle("yt-dlp");
        if (!bundle_path.empty()) {
            std::cout << "[DEBUG] PathFinder::findYtDlpPath: Found yt-dlp in app bundle: " << bundle_path << std::endl;
            cached_ytdlp_path = bundle_path;
            ytdlp_path_cached = true;
            return bundle_path;
        }
        std::cout << "[DEBUG] PathFinder::findYtDlpPath: yt-dlp not found in app bundle" << std::endl;
    }
    
#ifdef _WIN32
    // On Windows, try to find yt-dlp in res directory first (Release builds)
    char exe_path[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        std::string exe_dir = exe_path;
        size_t last_slash = exe_dir.find_last_of("\\/");
        if (last_slash != std::string::npos) {
            exe_dir = exe_dir.substr(0, last_slash);
            // Try res/yt-dlp.exe first (Release builds)
            std::string res_ytdlp = exe_dir + "\\res\\yt-dlp.exe";
            if (isExecutable(res_ytdlp)) {
                std::cout << "[DEBUG] PathFinder::findYtDlpPath: Found yt-dlp in res directory: " << res_ytdlp << std::endl;
                cached_ytdlp_path = res_ytdlp;
                ytdlp_path_cached = true;
                return res_ytdlp;
            }
            // Fallback to same directory as executable (Debug builds)
            std::string local_ytdlp = exe_dir + "\\yt-dlp.exe";
            if (isExecutable(local_ytdlp)) {
                std::cout << "[DEBUG] PathFinder::findYtDlpPath: Found yt-dlp in executable directory: " << local_ytdlp << std::endl;
                cached_ytdlp_path = local_ytdlp;
                ytdlp_path_cached = true;
                return local_ytdlp;
            }
        }
    }
#endif
    
    // Try system PATH
    std::cout << "[DEBUG] PathFinder::findYtDlpPath: Checking system PATH..." << std::endl;
    ytdlp_path = findInPath("yt-dlp");
    if (!ytdlp_path.empty()) {
        std::cout << "[DEBUG] PathFinder::findYtDlpPath: Found yt-dlp in PATH: " << ytdlp_path << std::endl;
        cached_ytdlp_path = ytdlp_path;
        ytdlp_path_cached = true;
        return ytdlp_path;
    }
    std::cout << "[DEBUG] PathFinder::findYtDlpPath: yt-dlp not found in PATH" << std::endl;
    
    // Try python module as fallback
    std::cout << "[DEBUG] PathFinder::findYtDlpPath: Trying python module..." << std::endl;
#ifdef _WIN32
    FILE* pipe = popen("python -m yt_dlp --version 2>nul", "r");
#else
    FILE* pipe = popen("python3 -m yt_dlp --version 2>/dev/null", "r");
#endif
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            pclose(pipe);
            std::cout << "[DEBUG] PathFinder::findYtDlpPath: Found yt-dlp as python module: python3 -m yt_dlp" << std::endl;
            cached_ytdlp_path = "python3 -m yt_dlp";
            ytdlp_path_cached = true;
            return "python3 -m yt_dlp";
        }
        pclose(pipe);
    }
    std::cout << "[DEBUG] PathFinder::findYtDlpPath: Python module not available" << std::endl;
    
    // Final fallback
    std::cout << "[DEBUG] PathFinder::findYtDlpPath: Using fallback: yt-dlp (will rely on system PATH)" << std::endl;
    cached_ytdlp_path = "yt-dlp";
    ytdlp_path_cached = true;
    return "yt-dlp";
}

std::string PathFinder::findFfmpegPath() {
    std::string ffmpeg_path;
    
    // Try app bundle first (macOS)
    if (PlatformDetector::isMacOS()) {
        std::string bundle_path = findInAppBundle("ffmpeg");
        if (!bundle_path.empty()) {
            return bundle_path;
        }
    }
    
#ifdef _WIN32
    // On Windows, try to find ffmpeg in res directory first (Release builds), then same directory as executable (Debug builds)
    char exe_path[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        std::string exe_dir = exe_path;
        size_t last_slash = exe_dir.find_last_of("\\/");
        if (last_slash != std::string::npos) {
            exe_dir = exe_dir.substr(0, last_slash);
            // Try res/ffmpeg.exe first (Release builds)
            std::string res_ffmpeg = exe_dir + "\\res\\ffmpeg.exe";
            if (isExecutable(res_ffmpeg)) {
                return res_ffmpeg;
            }
            // Fallback to same directory as executable (Debug builds)
            std::string local_ffmpeg = exe_dir + "\\ffmpeg.exe";
            if (isExecutable(local_ffmpeg)) {
                return local_ffmpeg;
            }
        }
    }
#endif
    
    // Try system PATH
    ffmpeg_path = findInPath("ffmpeg");
    if (!ffmpeg_path.empty()) {
        return ffmpeg_path;
    }
    
    // Final fallback
    return "ffmpeg";
}

std::string PathFinder::findInAppBundle(const std::string& filename) {
#ifdef __APPLE__
    char exe_path[PATH_MAX];
    uint32_t size = sizeof(exe_path);
    
    if (_NSGetExecutablePath(exe_path, &size) == 0) {
        std::string exe_dir = exe_path;
        size_t last_slash = exe_dir.find_last_of("/");
        if (last_slash != std::string::npos) {
            // Go up from MacOS to Contents/Resources
            std::string resources_dir = exe_dir.substr(0, last_slash) + "/../Resources";
            char resolved_path[PATH_MAX];
            if (realpath(resources_dir.c_str(), resolved_path) != nullptr) {
                std::string bundle_file = std::string(resolved_path) + "/" + filename;
                if (isExecutable(bundle_file)) {
                    return bundle_file;
                }
            }
        }
    }
#endif
    return "";
}

std::string PathFinder::findInPath(const std::string& filename) {
    std::cout << "[DEBUG] PathFinder::findInPath: Searching for '" << filename << "' in PATH..." << std::endl;
    std::string command;
    
#ifdef _WIN32
    command = "where " + filename + " 2>nul";
#else
    command = "which " + filename + " 2>/dev/null";
#endif
    
    std::cout << "[DEBUG] PathFinder::findInPath: Executing: " << command << std::endl;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cout << "[DEBUG] PathFinder::findInPath: popen() failed" << std::endl;
        return "";
    }
    
    char buffer[PATH_MAX];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
        // Remove newline
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }
    }
    
    pclose(pipe);
    std::cout << "[DEBUG] PathFinder::findInPath: Result: " << (result.empty() ? "NOT_FOUND" : result) << std::endl;
    
    // Verify it's executable
    if (!result.empty() && isExecutable(result)) {
        return result;
    }
    
    return "";
}

std::string PathFinder::findInSystemPaths(const std::string& filename) {
    // Try common system paths
    std::vector<std::string> paths;
    
    if (PlatformDetector::isMacOS()) {
        paths = {
            "/opt/homebrew/bin",
            "/usr/local/bin",
            "/usr/bin"
        };
    } else if (PlatformDetector::isWindows()) {
        // Windows system paths are usually in PATH, so findInPath should work
        return findInPath(filename);
    } else {
        paths = {
            "/usr/local/bin",
            "/usr/bin",
            "/bin"
        };
    }
    
    for (const auto& path : paths) {
        std::string full_path = path + "/" + filename;
        if (isExecutable(full_path)) {
            return full_path;
        }
    }
    
    return "";
}

bool PathFinder::isExecutable(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    
    // Check if it's a regular file
    if (!S_ISREG(st.st_mode)) {
        return false;
    }
    
    // Check if executable
#ifdef _WIN32
    // On Windows, check if it's .exe or has executable extension
    if (path.find(".exe") != std::string::npos) {
        return true;
    }
    // Also check if file exists and is readable
    return (st.st_mode & _S_IREAD) != 0;
#else
    return (st.st_mode & S_IXUSR) != 0;
#endif
}

