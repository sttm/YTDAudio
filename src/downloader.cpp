#include "downloader.h"
#include "common/validation_utils.h"
#include "common/audio_utils.h"
#include "common/json_utils.h"
#include "common/windows_utils.h"
#include "common/process_launcher.h"
#include "platform/path_finder.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <vector>
#include <memory>
#include <future>
#include <chrono>

// yt-dlp configuration parameters - easily adjustable
namespace YtDlpConfig {
    // ===== Feature flags - enable/disable specific options =====
    const bool USE_SLEEP_INTERVALS_PLAYLIST = false; // Use sleep intervals for playlists
    const bool USE_COOKIES_FOR_PLAYLISTS = false;   // Use browser cookies for playlists
    const bool USE_SLEEP_REQUESTS = false;          // Use --sleep-requests for playlists
    
    // ===== Parameter values =====
    // Sleep intervals for playlist downloads (to avoid rate limiting)
    const int PLAYLIST_SLEEP_INTERVAL = 1;
    const int PLAYLIST_MAX_SLEEP_INTERVAL = 1;
    const int PLAYLIST_SLEEP_REQUESTS = 1;
    
    // Browser priority list for cookies (checked in order)
    // Firefox and Safari first to avoid keychain password prompts on macOS
    // Chrome and Chromium last on macOS because they require keychain access
    // If no browser is available, app will work without cookies
    const std::vector<std::string> BROWSER_PRIORITY_MACOS = {"firefox", "safari", "edge", "opera", "brave", "chrome", "chromium"};
    const std::vector<std::string> BROWSER_PRIORITY_OTHER = {"firefox", "edge", "opera", "brave", "chrome", "chromium"};
    
    // Format selection
    const std::string FORMAT_SELECTION = "bestaudio/best";
    
    // Output template
    const std::string OUTPUT_TEMPLATE = "%(title)s.%(ext)s";
    
    // Audio quality settings
    const std::string AUDIO_QUALITY_BEST = "0";
    const std::string AUDIO_QUALITY_320K = "320K";
    const std::string AUDIO_QUALITY_256K = "256K";
    const std::string AUDIO_QUALITY_192K = "192K";
    const std::string AUDIO_QUALITY_128K = "128K";
    
    // Socket timeout for getVideoInfo (in seconds)
    const int VIDEO_INFO_TIMEOUT = 10;
    
    // Socket timeout for downloads (in seconds) - increased for SoundCloud HLS streams
    const int DOWNLOAD_SOCKET_TIMEOUT = 120;  // 2 minutes for large files/slow connections
    
    // Fragment retries for HLS streams (like SoundCloud)
    const int FRAGMENT_RETRIES = 10;  // Number of retries for HLS fragments
}

#ifdef _WIN32
#define NOMINMAX  // Prevent Windows min/max macros from conflicting with std::min/max
#include <windows.h>
#include <io.h>
#include <shlobj.h>
#include "common/process_launcher.h"
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#endif
#endif

// RAII wrapper for FILE* pipe to ensure it's always closed
namespace {
    struct PipeGuard {
        FILE* pipe_;
#ifdef _WIN32
        HANDLE process_handle_;
        PipeGuard(FILE* p, HANDLE h = INVALID_HANDLE_VALUE) : pipe_(p), process_handle_(h) {}
        ~PipeGuard() {
            if (pipe_) {
                fclose(pipe_);
                pipe_ = nullptr;
            }
            if (process_handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(process_handle_);
                process_handle_ = INVALID_HANDLE_VALUE;
            }
        }
#else
        PipeGuard(FILE* p) : pipe_(p) {}
        ~PipeGuard() {
            if (pipe_) {
                pclose(pipe_);
                pipe_ = nullptr;
            }
        }
#endif
        FILE* get() { return pipe_; }
        void release() { pipe_ = nullptr; }  // For manual release if needed
    };
}

// Escape URL for Windows cmd.exe - escape special characters that cmd.exe interprets
// Helper function to find final converted file (e.g., mp3 instead of opus/webm)
static std::string findFinalConvertedFile(const std::string& intermediate_path, const std::string& target_format) {
    if (intermediate_path.empty() || target_format.empty()) {
        return intermediate_path;
    }
    
    size_t last_dot = intermediate_path.find_last_of(".");
    std::string current_ext = (last_dot != std::string::npos) ? intermediate_path.substr(last_dot) : "";
    std::string target_ext = "." + target_format;
    
    // If it's already in target format, return as-is
    if (current_ext == target_ext) {
        return intermediate_path;
    }
    
    // Common intermediate formats that get converted (exclude target format)
    // Note: .ogg is only intermediate if target_format is not "ogg"
    std::vector<std::string> intermediate_extensions = {".opus", ".webm"};
    // .m4a, .ogg, .flac, .wav are intermediate only if they're not the target format
    if (target_format != "m4a") {
        intermediate_extensions.push_back(".m4a");
    }
    if (target_format != "ogg") {
        intermediate_extensions.push_back(".ogg");
    }
    if (target_format != "flac") {
        intermediate_extensions.push_back(".flac");
    }
    if (target_format != "wav") {
        intermediate_extensions.push_back(".wav");
    }
    
    // Check if path has an intermediate extension
    bool has_intermediate_ext = false;
    if (last_dot != std::string::npos && last_dot < intermediate_path.length() - 1) {
        for (const auto& intermediate_ext : intermediate_extensions) {
            if (current_ext == intermediate_ext) {
                has_intermediate_ext = true;
                break;
            }
        }
    }
    
    // If it's not an intermediate format and not target format, return as-is
    if (!has_intermediate_ext) {
        return intermediate_path;
    }
    
    // Try to find file with target format extension
    if (last_dot != std::string::npos) {
        std::string base_path = intermediate_path.substr(0, last_dot);
        std::string target_path = base_path + "." + target_format;
        
#ifdef _WIN32
        // Use UTF-16 API on Windows for proper Unicode support
        int path_size = MultiByteToWideChar(CP_UTF8, 0, target_path.c_str(), -1, NULL, 0);
        if (path_size > 0) {
            std::wstring wide_path(path_size, 0);
            MultiByteToWideChar(CP_UTF8, 0, target_path.c_str(), -1, &wide_path[0], path_size);
            wide_path.resize(path_size - 1);
            
            struct _stat64i32 st;
            if (_wstat(wide_path.c_str(), &st) == 0) {
                std::string debug_msg = "[DEBUG] findFinalConvertedFile: Found " + target_format + " file: " + target_path;
                writeConsoleUtf8(debug_msg + "\n");
                return target_path;
            }
        }
#else
        struct stat st;
        if (stat(target_path.c_str(), &st) == 0) {
#ifdef _WIN32
            std::string debug_msg = "[DEBUG] findFinalConvertedFile: Found " + target_format + " file: " + target_path;
            writeConsoleUtf8(debug_msg + "\n");
#else
            std::cout << "[DEBUG] findFinalConvertedFile: Found " << target_format << " file: " << target_path << std::endl;
#endif
            return target_path;
        }
#endif
    }
    
    // If target format not found, return original path
    return intermediate_path;
}

// Convert format string to yt-dlp compatible format
// yt-dlp uses "vorbis" for OGG container format, not "ogg"
static std::string convertFormatForYtDlp(const std::string& format) {
    if (format == "ogg") {
        return "vorbis";
    }
    return format;
}

// This is needed when URL is passed through cmd /c "..." wrapper
static std::string escapeUrlForWindows(const std::string& url) {
#ifdef _WIN32
    std::string escaped;
    escaped.reserve(url.length() + 10);  // Pre-allocate with some extra space
    for (char c : url) {
        // Escape special cmd.exe characters: &, |, >, <, ^
        // Note: We escape & as ^& because when cmd /c "..." is used, & needs to be escaped
        if (c == '&') {
            escaped += "^&";
        } else if (c == '|') {
            escaped += "^|";
        } else if (c == '>') {
            escaped += "^>";
        } else if (c == '<') {
            escaped += "^<";
        } else if (c == '^') {
            escaped += "^^";
        } else {
            escaped += c;
        }
    }
    return escaped;
#else
    // On non-Windows, return URL as-is
    return url;
#endif
}

// Escape path for Windows cmd.exe - escape quotes and special characters in paths
// This is needed when path contains spaces and is passed through cmd /c "..." wrapper
// Note: We don't escape quotes here because paths shouldn't contain quotes
// The path will be wrapped in quotes in the command, and cmd.exe will handle spaces correctly
static std::string escapePathForWindows(const std::string& path) {
    // For now, just return the path as-is
    // The path will be wrapped in quotes in the command string
    // cmd.exe should handle spaces correctly when the path is in quotes
    return path;
}

Downloader::Downloader() : cancel_flag_(std::make_shared<std::atomic<bool>>(false)) {
}

Downloader::~Downloader() {
    // Set cancel flag to stop download
    cancel();
    
    // Wait for thread to finish (with timeout to avoid hanging)
    if (download_thread_.joinable()) {
        // Try to join with timeout
        auto future = std::async(std::launch::async, [this]() {
            if (download_thread_.joinable()) {
                download_thread_.join();
            }
        });
        
        // Wait up to 2 seconds for thread to finish
        if (future.wait_for(std::chrono::seconds(2)) == std::future_status::timeout) {
            std::cout << "[DEBUG] Downloader: Thread not finished in 2 seconds, detaching..." << std::endl;
            download_thread_.detach();
        }
    }
}

std::string Downloader::findYtDlpPath() {
    // Use PathFinder for centralized path finding logic
    return PathFinder::findYtDlpPath();
}

// Cached browser result - browser doesn't change during app runtime
static std::string cached_available_browser;
static bool browser_cache_initialized = false;

// Helper to find available browser for cookies (prioritize Chrome)
// Result is cached to avoid repeated yt-dlp calls (browser doesn't change during app runtime)
static std::string findAvailableBrowser() {
    // Return cached result if already found
    if (browser_cache_initialized) {
        return cached_available_browser;
    }
    
    std::string ytdlp_path = Downloader::findYtDlpPath();
    
    // List of browsers to try in priority order
    std::vector<std::string> browsers;
    
#ifdef __APPLE__
    // macOS browsers
    browsers = YtDlpConfig::BROWSER_PRIORITY_MACOS;
#else
    // Linux/Windows browsers
    browsers = YtDlpConfig::BROWSER_PRIORITY_OTHER;
#endif
    
    // Try each browser by testing if yt-dlp can access it
    // Skip browsers that require keychain on macOS if user might deny access
    for (const std::string& browser : browsers) {
#ifdef __APPLE__
        // On macOS, skip Chrome/Chromium if they're not first priority (to avoid keychain prompts)
        // Only check them if no other browser is available
        if ((browser == "chrome" || browser == "chromium") && browsers.size() > 1) {
            // Check if we already found a browser earlier in the list
            // This is a simple check - if we're iterating Chrome, we haven't found others yet
            // So we'll skip it and only use it as last resort
            continue;
        }
#endif
        // Test if browser is available by trying to use it with yt-dlp
        // Use a simple test command that doesn't actually download anything
#ifdef _WIN32
        // Use ProcessLauncher to avoid console window
        std::vector<std::string> args;
        args.push_back("--cookies-from-browser");
        args.push_back(browser);
        args.push_back("--version");
        ProcessInfo process_info = ProcessLauncher::launchProcess(ytdlp_path, args, true);  // redirect_stderr = true
        FILE* pipe = process_info.pipe;
        if (pipe) {
            char buffer[256];
            bool found_error = false;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string line = buffer;
                // Check for common error messages indicating browser is not available
                if (line.find("ERROR") != std::string::npos ||
                    line.find("No such browser") != std::string::npos ||
                    line.find("Unable to find") != std::string::npos ||
                    line.find("not found") != std::string::npos) {
                    found_error = true;
                    break;
                }
            }
            ProcessLauncher::closeProcess(process_info);
            
            // If no error found, browser is available
            if (!found_error) {
                std::cout << "[DEBUG] Found available browser: " << browser << std::endl;
                cached_available_browser = browser;
                browser_cache_initialized = true;
                return browser;
            }
        }
#else
        std::string test_cmd = "\"" + ytdlp_path + "\" --cookies-from-browser " + browser + " --version 2>&1";
        FILE* pipe = popen(test_cmd.c_str(), "r");
        if (pipe) {
            char buffer[256];
            bool found_error = false;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string line = buffer;
                // Check for common error messages indicating browser is not available
                if (line.find("ERROR") != std::string::npos ||
                    line.find("No such browser") != std::string::npos ||
                    line.find("Unable to find") != std::string::npos ||
                    line.find("not found") != std::string::npos) {
                    found_error = true;
                    break;
                }
            }
            pclose(pipe);
            
            // If no error found, browser is available
            if (!found_error) {
                std::cout << "[DEBUG] Found available browser: " << browser << std::endl;
                cached_available_browser = browser;
                browser_cache_initialized = true;
                return browser;
            }
        }
#endif
    }
    
    // If no browser found in first pass, try Chrome/Chromium on macOS as last resort
#ifdef __APPLE__
    for (const std::string& browser : {"chrome", "chromium"}) {
        std::string test_cmd = "\"" + ytdlp_path + "\" --cookies-from-browser " + browser + " --version 2>&1";
        FILE* pipe = popen(test_cmd.c_str(), "r");
        if (pipe) {
            char buffer[256];
            bool found_error = false;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                std::string line = buffer;
                if (line.find("ERROR") != std::string::npos ||
                    line.find("No such browser") != std::string::npos ||
                    line.find("Unable to find") != std::string::npos ||
                    line.find("not found") != std::string::npos) {
                    found_error = true;
                    break;
                }
            }
            pclose(pipe);
            
            if (!found_error) {
                std::cout << "[DEBUG] Found available browser (last resort): " << browser << std::endl;
                cached_available_browser = browser;
                browser_cache_initialized = true;
                return browser;
            }
        }
    }
#endif
    
    // Fallback: return empty string if no browser found (yt-dlp will work without cookies)
    // On macOS, don't fallback to chrome to avoid keychain prompts
    std::cout << "[DEBUG] No browser detected, will proceed without cookies" << std::endl;
    cached_available_browser = "";
    browser_cache_initialized = true;
    return "";
}

// Helper to find ffmpeg: use PathFinder for centralized path finding
static std::string findFfmpegPath() {
    // Use PathFinder for centralized path finding logic
    return PathFinder::findFfmpegPath();
}

std::string Downloader::getExpectedFilename(
    const std::string& url,
    const std::string& output_dir,
    const std::string& format
) {
    std::string ytdlp_path = findYtDlpPath();
    std::ostringstream cmd;
    
    // Escape yt-dlp path if needed
    if (ytdlp_path.find(' ') != std::string::npos || ytdlp_path.find('"') != std::string::npos) {
        std::string escaped_path;
        for (char c : ytdlp_path) {
            if (c == '"' || c == '\\') {
                escaped_path += '\\';
            }
            escaped_path += c;
        }
        cmd << "\"" << escaped_path << "\"";
    } else {
        cmd << ytdlp_path;
    }

    // Prefer ffmpeg from app bundle, then system ffmpeg
    std::string ffmpeg_path = findFfmpegPath();
    if (!ffmpeg_path.empty()) {
        cmd << " --ffmpeg-location \"" << ffmpeg_path << "\"";
    }
    
    // Audio format - convert "ogg" to "vorbis" for yt-dlp compatibility
    // Declare once before #ifdef to avoid multiple initialization on Windows
    std::string ytdlp_format = convertFormatForYtDlp(format);
    
    // Get filename with format (same as in buildYtDlpCommand)
    cmd << " --get-filename";
    cmd << " -f \"bestaudio/best\"";
    cmd << " -x";  // Extract audio
    cmd << " --audio-format " << ytdlp_format;
    // Normalize output path separators for platform
    std::string normalized_output_dir = output_dir;
#ifdef _WIN32
    // On Windows, replace forward slashes with backslashes
    std::replace(normalized_output_dir.begin(), normalized_output_dir.end(), '/', '\\');
    // Remove duplicate backslashes
    std::string result;
    for (size_t i = 0; i < normalized_output_dir.length(); i++) {
        if (normalized_output_dir[i] != '\\' || result.empty() || result.back() != '\\') {
            result += normalized_output_dir[i];
        }
    }
    normalized_output_dir = result;
    std::string output_path = normalized_output_dir + "\\%(title)s.%(ext)s";
#else
    std::string output_path = normalized_output_dir + "/%(title)s.%(ext)s";
#endif
    cmd << " -o \"" << output_path << "\"";
    std::string escaped_url = escapeUrlForWindows(url);
    cmd << " \"" << escaped_url << "\"";
    
#ifdef _WIN32
    // Use ProcessLauncher to avoid console window
    std::vector<std::string> args;
    args.push_back("--get-filename");
    args.push_back("-f");
    args.push_back("bestaudio/best");
    args.push_back("-x");  // Extract audio
    // Use ytdlp_format already declared above
    args.push_back("--audio-format");
    args.push_back(ytdlp_format);
    args.push_back("-o");
    args.push_back(output_path);
    args.push_back(url);
    
    // Add ffmpeg location if available (ffmpeg_path already declared above)
    if (!ffmpeg_path.empty()) {
        args.insert(args.begin() + 1, "--ffmpeg-location");
        args.insert(args.begin() + 2, ffmpeg_path);
    }
    
    ProcessInfo process_info = ProcessLauncher::launchProcess(ytdlp_path, args, true);  // redirect_stderr = true
    FILE* pipe = process_info.pipe;
    if (!pipe) {
        return "";
    }
    
    char buffer[1024];
    std::string filename;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        filename = buffer;
        // Remove newline and carriage return
        while (!filename.empty() && (filename.back() == '\n' || filename.back() == '\r')) {
            filename.pop_back();
        }
        // Trim leading whitespace
        while (!filename.empty() && (filename.front() == ' ' || filename.front() == '\t')) {
            filename.erase(0, 1);
        }
    }
    
    ProcessLauncher::closeProcess(process_info);
    
    // If filename is empty, yt-dlp failed
    if (filename.empty()) {
        return "";
    }
    
    return filename;
#else
    cmd << " 2>/dev/null";
    
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        return "";
    }
    
    char buffer[1024];
    std::string filename;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        filename = buffer;
        // Remove newline and carriage return
        while (!filename.empty() && (filename.back() == '\n' || filename.back() == '\r')) {
            filename.pop_back();
        }
        // Trim leading whitespace
        while (!filename.empty() && (filename.front() == ' ' || filename.front() == '\t')) {
            filename.erase(0, 1);
        }
    }
    
    int status = pclose(pipe);
    
    // If yt-dlp failed, return empty string
    if (status != 0 || filename.empty()) {
        return "";
    }
    
    return filename;
#endif
}

bool Downloader::checkYtDlpAvailable() {
    std::string ytdlp_path = findYtDlpPath();
#ifdef _WIN32
    // Use ProcessLauncher to avoid console window
    std::vector<std::string> args;
    args.push_back("--version");
    ProcessInfo process_info = ProcessLauncher::launchProcess(ytdlp_path, args, true);  // redirect_stderr = true
    FILE* pipe = process_info.pipe;
    if (!pipe) {
        return false;
    }
    
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    ProcessLauncher::closeProcess(process_info);
#else
    std::string command = ytdlp_path + " --version 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return false;
    }
    
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
#endif
    
    return !result.empty();
}

std::string Downloader::getYtDlpVersion() {
    std::cout << "[DEBUG] Downloader::getYtDlpVersion: Checking yt-dlp version..." << std::endl;
    std::string ytdlp_path = findYtDlpPath();
#ifdef _WIN32
    // Use ProcessLauncher to avoid console window
    std::vector<std::string> args;
    args.push_back("--version");
    std::cout << "[DEBUG] Downloader::getYtDlpVersion: Executing with ProcessLauncher (no console window)" << std::endl;
    ProcessInfo process_info = ProcessLauncher::launchProcess(ytdlp_path, args, true);  // redirect_stderr = true
    FILE* pipe = process_info.pipe;
    if (!pipe) {
        std::cout << "[DEBUG] Downloader::getYtDlpVersion: Failed to execute command" << std::endl;
        return "Unknown";
    }
    
    char buffer[128];
    std::string result;
    // CRITICAL: Read output with timeout check (version check should be fast)
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
        // Limit result size to prevent issues
        if (result.length() > 100) break;
    }
    ProcessLauncher::closeProcess(process_info);
#else
    std::string final_cmd = ytdlp_path + " --version 2>/dev/null";
    std::cout << "[DEBUG] Downloader::getYtDlpVersion: Executing: " << final_cmd << std::endl;
    FILE* pipe = popen(final_cmd.c_str(), "r");
    if (!pipe) {
        std::cout << "[DEBUG] Downloader::getYtDlpVersion: Failed to execute command" << std::endl;
        return "Unknown";
    }
    
    char buffer[128];
    std::string result;
    // CRITICAL: Read output with timeout check (version check should be fast)
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
        // Limit result size to prevent issues
        if (result.length() > 100) break;
    }
    pclose(pipe);
#endif
    
    // Remove newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    
    std::string version = result.empty() ? "Unknown" : result;
    std::cout << "[DEBUG] Downloader::getYtDlpVersion: Version detected: " << version << std::endl;
    return version;
}

bool Downloader::updateYtDlp(std::string& log_output) {
    log_output.clear();
    
    std::string ytdlp_path = findYtDlpPath();
    if (ytdlp_path.empty()) {
        log_output = "yt-dlp not found";
        return false;
    }
    
    // Build command: yt-dlp -U
    std::ostringstream cmd;
    
    // Escape path if needed (same logic as buildYtDlpCommand)
    if (ytdlp_path.find(' ') != std::string::npos || ytdlp_path.find('"') != std::string::npos) {
        std::string escaped_path;
        for (char c : ytdlp_path) {
            if (c == '"' || c == '\\') {
                escaped_path += '\\';
            }
            escaped_path += c;
        }
        cmd << "\"" << escaped_path << "\"";
    } else {
        cmd << ytdlp_path;
    }
    
    cmd << " -U 2>&1"; // Self-update, capture stderr too
    
    std::string command = cmd.str();
    std::cout << "[DEBUG] Updating yt-dlp with command: " << command << std::endl;
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        log_output = "Failed to start yt-dlp update process";
        return false;
    }
    
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        log_output += buffer;
    }
    int status = pclose(pipe);
    
    std::cout << "[DEBUG] yt-dlp update finished with status: " << status << std::endl;
    if (!log_output.empty() && log_output.length() < 1000) {
        std::cout << "[DEBUG] yt-dlp update log: " << log_output << std::endl;
    }
    
    return status == 0;
}

void Downloader::downloadAsync(
    const std::string& url,
    const std::string& output_dir,
    const std::string& format,
    const std::string& quality,
    const std::string& proxy,
    const std::string& spotify_api_key,
    const std::string& youtube_api_key,
    const std::string& soundcloud_api_key,
    bool download_playlist,
    ProgressCallback progress_cb,
    CompleteCallback complete_cb,
    const YtDlpSettings& settings,
    const std::string& playlist_items
) {
    // Reset cancel flag for new download
    if (cancel_flag_) {
        cancel_flag_->store(false);
    } else {
        cancel_flag_ = std::make_shared<std::atomic<bool>>(false);
    }
    
    // Wait for previous thread to finish (with timeout to avoid blocking GUI)
    // This ensures we don't have multiple threads accessing the same Downloader object
    if (download_thread_.joinable()) {
        // Try to join with a short timeout - if it takes too long, detach it
        // This prevents blocking the GUI while ensuring thread safety
        // CRITICAL: Store thread handle before async, as detach() may be called
        std::thread previous_thread = std::move(download_thread_);
        
        auto future = std::async(std::launch::async, [&previous_thread]() {
            if (previous_thread.joinable()) {
                previous_thread.join();
            }
        });
        
        // Wait up to 100ms for thread to finish
        if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout) {
            // Thread is taking too long, detach it (it will clean up on its own)
            std::cout << "[DEBUG] Previous download thread still running, detaching..." << std::endl;
            if (previous_thread.joinable()) {
                previous_thread.detach();
            }
        }
    }
    
    // Create new thread with shared cancel flag for safe access
    std::shared_ptr<std::atomic<bool>> cancel_flag_copy = cancel_flag_;
    download_thread_ = std::thread([this, url, output_dir, format, quality, proxy, 
                                    spotify_api_key, youtube_api_key, soundcloud_api_key,
                                    download_playlist, progress_cb, complete_cb, settings, playlist_items, cancel_flag_copy]() {
        downloadThread(url, output_dir, format, quality, proxy, 
                      spotify_api_key, youtube_api_key, soundcloud_api_key,
                      download_playlist, progress_cb, complete_cb, settings, playlist_items, cancel_flag_copy);
    });
}

void Downloader::cancel() {
    if (cancel_flag_) {
        cancel_flag_->store(true);
    }
}

void Downloader::downloadThread(
    const std::string& url,
    const std::string& output_dir,
    const std::string& format,
    const std::string& quality,
    const std::string& proxy,
    const std::string& spotify_api_key,
    const std::string& youtube_api_key,
    const std::string& soundcloud_api_key,
    bool download_playlist,
    ProgressCallback progress_cb,
    CompleteCallback complete_cb,
    const YtDlpSettings& settings,
    const std::string& playlist_items,
    std::shared_ptr<std::atomic<bool>> cancel_flag
) {
    std::cout << "[DEBUG] downloadThread started: URL=" << url 
              << ", Output=" << output_dir 
              << ", Format=" << format 
              << ", Quality=" << quality
              << ", Playlist=" << (download_playlist ? "YES" : "NO")
              << ", PlaylistItems=" << (playlist_items.empty() ? "ALL" : playlist_items) << std::endl;
    
    // Set environment variable to force unbuffered output from Python
    // This ensures yt-dlp output is available immediately and doesn't block
#ifndef _WIN32
    setenv("PYTHONUNBUFFERED", "1", 1);
#endif
    
    FILE* pipe = nullptr;
    
#ifdef _WIN32
    ProcessInfo process_info;
    // On Windows, use CreateProcessW via ProcessLauncher - no escaping needed!
    std::string ytdlp_path = findYtDlpPath();
    std::vector<std::string> args = buildYtDlpArguments(url, output_dir, format, quality, proxy, 
                                                         spotify_api_key, youtube_api_key, soundcloud_api_key,
                                                         download_playlist, settings, playlist_items);
    
    // Debug: print arguments
    std::cout << "[DEBUG] Downloader: Using CreateProcessW (no cmd.exe escaping needed!)" << std::endl;
    std::cout << "[DEBUG] Downloader: Executable: " << ytdlp_path << std::endl;
    std::cout << "[DEBUG] Downloader: Arguments: ";
    for (size_t i = 0; i < args.size(); i++) {
        std::cout << args[i];
        if (i < args.size() - 1) std::cout << " ";
    }
    std::cout << std::endl;
    
    process_info = ProcessLauncher::launchProcess(ytdlp_path, args, false);
    if (!process_info.isValid()) {
        std::cout << "[DEBUG] ERROR: Failed to start yt-dlp process via CreateProcessW" << std::endl;
        complete_cb("", "Failed to start yt-dlp process");
        return;
    }
    pipe = process_info.pipe;
#else
    // On Unix, use the old method with exec
    std::string command = buildYtDlpCommand(url, output_dir, format, quality, proxy, spotify_api_key, youtube_api_key, soundcloud_api_key, download_playlist, settings, playlist_items);
    std::cout << "[DEBUG] yt-dlp command: " << command << std::endl;
    
    // CRITICAL: Use 'exec' to replace shell process with yt-dlp, reducing to 1 process total
    std::string exec_cmd = "exec " + command;
    std::string escaped_cmd = exec_cmd;
    size_t pos = 0;
    while ((pos = escaped_cmd.find("'", pos)) != std::string::npos) {
        escaped_cmd.replace(pos, 1, "'\\''");
        pos += 4;
    }
    std::string shell_cmd = "sh -c '" + escaped_cmd + "'";
    std::cout << "[DEBUG] Downloader: Using 'exec' to replace shell with yt-dlp (reduces to 1 process)" << std::endl;
    pipe = popen(shell_cmd.c_str(), "r");
    if (!pipe) {
        std::cout << "[DEBUG] ERROR: Failed to start yt-dlp process" << std::endl;
        complete_cb("", "Failed to start yt-dlp process");
        return;
    }
#endif
    
    // Use common PipeGuard RAII wrapper
#ifdef _WIN32
    PipeGuard pipe_guard(pipe, process_info.process_handle);
#else
    PipeGuard pipe_guard(pipe);
#endif
    
    std::cout << "[DEBUG] yt-dlp process started, reading output..." << std::endl;
    
    char buffer[4096];  // Increased buffer size for long JSON lines
    std::string line;
    std::string current_json_line;  // Accumulate multi-buffer JSON lines
    std::string last_file_path;
    std::string error_message;
    ProgressInfo last_progress;
    last_progress.progress = 0.0f;
    last_progress.status = "downloading";  // Set status to "downloading" immediately
    last_progress.is_playlist = false;
    last_progress.current_item_index = -1;
    last_progress.total_items = 0;
    
    // Call progress callback immediately to update UI status
    if (progress_cb) {
        progress_cb(last_progress);
    }
    
    bool playlist_detected = false;
    int playlist_item_count = 0;
    int playlist_total_items = 0;
    std::string current_item_title;
    int last_known_item_index = -1;  // Track last known item index for SoundCloud playlists
    std::string last_seen_title = "";  // Track last seen title to detect item changes
    std::string filename_from_json = "";  // Store filename from JSON for fallback path construction
    
    // Use pipe from guard
    FILE* pipe_ptr = pipe_guard.get();
    
    // CRITICAL: Check cancel flag before entering loop
    bool was_cancelled = false;
    while (fgets(buffer, sizeof(buffer), pipe_ptr) != nullptr) {
        // Check cancel flag on each iteration
        if (cancel_flag && cancel_flag->load()) {
            was_cancelled = true;
            std::cout << "[DEBUG] Download cancelled - breaking from read loop" << std::endl;
            break;
        }
        current_json_line += buffer;
        
        // Check if line is complete (ends with newline) or if it's a complete JSON object
        size_t len = strlen(buffer);
        bool line_complete = (len > 0 && buffer[len - 1] == '\n');
        
        // Also check if we have a complete JSON object (starts with { and ends with })
        bool json_complete = false;
        if (!current_json_line.empty() && current_json_line.front() == '{') {
            // Count braces to see if JSON is complete
            int brace_count = 0;
            for (char c : current_json_line) {
                if (c == '{') brace_count++;
                else if (c == '}') brace_count--;
            }
            json_complete = (brace_count == 0);
        }
        
        if (line_complete || json_complete) {
            line = current_json_line;
            current_json_line.clear();
            
            // Remove trailing newline
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            
            // Debug: log all lines that might be JSON (contains { or "title")
            // if (!line.empty() && (line.find('{') != std::string::npos || line.find("\"title\"") != std::string::npos)) {
            //     std::cout << "[DEBUG] Potential JSON line received: " << line.substr(0, 300) << std::endl;
            // }
            
            // First, try to parse as JSON (more reliable)
            // JSON lines from yt-dlp start with { (but may have leading whitespace)
            std::string trimmed_line = line;
            while (!trimmed_line.empty() && (trimmed_line.front() == ' ' || trimmed_line.front() == '\t')) {
                trimmed_line = trimmed_line.substr(1);
            }
            
            if (!trimmed_line.empty() && trimmed_line.front() == '{') {
                // std::cout << "[DEBUG] JSON received: " << trimmed_line << std::endl;
                ProgressInfo json_info = parseJsonProgress(trimmed_line);
#ifdef _WIN32
                std::string debug_msg = "[DEBUG] Parsed JSON: is_playlist=" + std::to_string(json_info.is_playlist ? 1 : 0)
                                      + ", current_item_index=" + std::to_string(json_info.current_item_index)
                                      + ", current_item_title=\"" + json_info.current_item_title + "\"";
                writeConsoleUtf8(debug_msg + "\n");
#else
                std::cout << "[DEBUG] Parsed JSON: is_playlist=" << json_info.is_playlist 
                      << ", current_item_index=" << json_info.current_item_index 
                      << ", current_item_title=\"" << json_info.current_item_title << "\"" << std::endl;
#endif
            
            // Update progress from JSON
            if (json_info.progress > 0 || json_info.downloaded > 0 || !json_info.status.empty()) {
                if (json_info.progress > 0) {
                    last_progress.progress = json_info.progress;
                }
                if (json_info.downloaded > 0) {
                    last_progress.downloaded = json_info.downloaded;
                }
                if (json_info.total > 0) {
                    last_progress.total = json_info.total;
                }
                if (json_info.speed > 0) {
                    last_progress.speed = json_info.speed;
                }
                if (!json_info.status.empty()) {
                    last_progress.status = json_info.status;
                }
            }
            
            // Update duration from JSON (important for metadata)
            if (json_info.duration > 0) {
                last_progress.duration = json_info.duration;
            }
            
            // Update thumbnail_url from JSON (important for playlists)
            if (!json_info.thumbnail_url.empty()) {
                last_progress.thumbnail_url = json_info.thumbnail_url;
                std::cout << "[DEBUG] Updated last_progress.thumbnail_url from JSON: " << last_progress.thumbnail_url << std::endl;
                // Notify UI immediately about thumbnail URL (especially for first playlist item)
                if (progress_cb) {
                    progress_cb(last_progress);
                }
            }
            
            // Update playlist info from JSON
            // For SoundCloud playlists, we might not have playlist_index in JSON, but we can detect playlist from context
            // If we have a title and we're downloading a playlist URL, this is likely a playlist item
            // IMPORTANT: Also check if we have a title - for SoundCloud, first track's title may arrive before is_playlist is set
            bool is_likely_playlist_item = (playlist_detected || json_info.is_playlist || 
                                           (!json_info.current_item_title.empty() && playlist_total_items > 0));
            
            // CRITICAL FIX: Save title and set index even if is_playlist is not yet set
            // This handles the case when first track's title arrives before is_playlist is detected
            // We need to save the title and set index so they can be used later when is_playlist is set
            if (!json_info.current_item_title.empty()) {
                bool title_saved = false;
                bool index_set = false;
                
                // Save title immediately if we don't have one yet, even if is_playlist is not set
                // This ensures first track's title is preserved
                if (last_progress.current_item_title.empty()) {
                    last_progress.current_item_title = json_info.current_item_title;
                    current_item_title = json_info.current_item_title;
                    title_saved = true;
#ifdef _WIN32
                    std::string debug_msg = "[DEBUG] Saved title early (before is_playlist set): \"" + json_info.current_item_title + "\"";
                    writeConsoleUtf8(debug_msg + "\n");
#else
                    std::cout << "[DEBUG] Saved title early (before is_playlist set): \"" << json_info.current_item_title << "\"" << std::endl;
#endif
                }
                
                // Also set index to 0 if this is the first item (index < 0 and we have a title)
                // This ensures first track gets index 0 even before is_playlist is detected
                if (last_progress.current_item_index < 0 && last_seen_title.empty()) {
                    last_progress.current_item_index = 0;
                    last_known_item_index = 0;
                    last_seen_title = json_info.current_item_title;
                    index_set = true;
#ifdef _WIN32
                    std::string debug_msg = "[DEBUG] Set index to 0 early (before is_playlist set) for title: \"" + json_info.current_item_title + "\"";
                    writeConsoleUtf8(debug_msg + "\n");
#else
                    std::cout << "[DEBUG] Set index to 0 early (before is_playlist set) for title: \"" << json_info.current_item_title << "\"" << std::endl;
#endif
                }
                
                // If we saved title or set index, notify UI immediately so it can update the first item
                if ((title_saved || index_set) && progress_cb) {
                    progress_cb(last_progress);
                }
            }
            
            if (json_info.is_playlist || is_likely_playlist_item) {
                playlist_detected = true;
                last_progress.is_playlist = true;
                
                if (json_info.current_item_index >= 0) {
                    int old_index = last_progress.current_item_index;
                    last_progress.current_item_index = json_info.current_item_index;
                    last_known_item_index = json_info.current_item_index;
                    if (old_index != json_info.current_item_index) {
                        std::cout << "[DEBUG] *** Playlist item index updated from JSON: " << json_info.current_item_index 
                                  << " (1-based: " << (json_info.current_item_index + 1) << ") ***" << std::endl;
                    }
                } else if (is_likely_playlist_item && !json_info.current_item_title.empty()) {
                    // For SoundCloud, if we don't have playlist_index but have a title and we're in a playlist,
                    // determine index by detecting title changes
                    // IMPORTANT: Only update index if title actually changed from the last seen title
                    // This prevents incrementing index for multiple JSON lines of the same track
                    if (last_seen_title != json_info.current_item_title) {
                        // New item detected by title change
                        // CRITICAL FIX: Check if this is truly the first item
                        // If last_progress.current_item_index is still < 0 OR if it's 0 but last_seen_title is empty,
                        // then this is the first item and should get index 0
                        if (last_progress.current_item_index < 0 || (last_progress.current_item_index == 0 && last_seen_title.empty())) {
                            // First item - start at 0
                            last_progress.current_item_index = 0;
                            last_known_item_index = 0;
                            last_seen_title = json_info.current_item_title;
                            std::cout << "[DEBUG] *** Setting playlist item index to 0 (first item detected: \"" << json_info.current_item_title << "\") ***" << std::endl;
                        } else {
                            // Title changed from previous item - increment index
                            // Only increment if we're moving to a different track
                            // IMPORTANT: Use last_known_item_index + 1, not last_progress.current_item_index + 1
                            // This ensures correct incrementing even if current_item_index was set to 0 early
                            last_progress.current_item_index = last_known_item_index + 1;
                            last_known_item_index = last_progress.current_item_index;
                            last_seen_title = json_info.current_item_title;
                            std::cout << "[DEBUG] *** Incremented playlist item index to: " << last_progress.current_item_index 
                                      << " (1-based: " << (last_progress.current_item_index + 1) 
                                      << ", title: \"" << json_info.current_item_title << "\") ***" << std::endl;
                        }
                    } else {
                        // Same title as before - keep current index (don't increment)
                        std::cout << "[DEBUG] Same title as before (\"" << json_info.current_item_title << "\"), keeping index " << last_progress.current_item_index << std::endl;
                    }
                }
                
                if (json_info.total_items > 0) {
                    if (json_info.total_items != playlist_total_items) {
                        playlist_total_items = json_info.total_items;
                        last_progress.total_items = json_info.total_items;
                        std::cout << "[DEBUG] Playlist total items from JSON: " << playlist_total_items << std::endl;
                    }
                }
                
                if (!json_info.current_item_title.empty()) {
                    // Update current_item_title - this is done for all JSON lines
                    // But index update is handled above, so we don't duplicate it here
                    // Only update title if it's different (to avoid unnecessary updates)
                    if (last_progress.current_item_title != json_info.current_item_title) {
                        last_progress.current_item_title = json_info.current_item_title;
                        current_item_title = json_info.current_item_title;
#ifdef _WIN32
                        std::string debug_msg = "[DEBUG] Item title from JSON (index=" + std::to_string(last_progress.current_item_index) 
                                              + "): \"" + json_info.current_item_title + "\"";
                        writeConsoleUtf8(debug_msg + "\n");
#else
                        std::cout << "[DEBUG] Item title from JSON (index=" << last_progress.current_item_index 
                                  << "): \"" << json_info.current_item_title << "\"" << std::endl;
#endif
                    }
                } else {
                    std::cout << "[DEBUG] JSON parsed but current_item_title is EMPTY (index=" << json_info.current_item_index << ")" << std::endl;
                    std::cout << "[DEBUG] JSON line preview: " << line.substr(0, 300) << std::endl;
                }
                
                if (!json_info.playlist_name.empty()) {
                    last_progress.playlist_name = json_info.playlist_name;
                    std::cout << "[DEBUG] Playlist name from JSON: \"" << json_info.playlist_name << "\"" << std::endl;
                }
                
                // Extract filename from JSON if available (full path, not just title)
                // Try "_filename" field first (full path, more reliable), then "filename"
                size_t filename_pos = line.find("\"_filename\":");
                if (filename_pos == std::string::npos) {
                    filename_pos = line.find("\"filename\":");
                }
                if (filename_pos != std::string::npos) {
                    size_t start = line.find('"', filename_pos + 11) + 1;
                    size_t end = start;
                    // Find end quote, handling escaped quotes
                    while (end < line.length()) {
                        if (line[end] == '"' && (end == start || line[end - 1] != '\\')) {
                            break;
                        }
                        end++;
                    }
                    if (end > start && end < line.length()) {
                        std::string file_path = line.substr(start, end - start);
                        // Use JsonUtils::unescapeJsonString to properly decode Unicode escape sequences
                        std::string unescaped_path = JsonUtils::unescapeJsonString(file_path);
#ifdef _WIN32
                        std::string raw_debug = "[DEBUG] Raw file_path from JSON: " + file_path.substr(0, 100) + (file_path.length() > 100 ? "..." : "");
                        writeConsoleUtf8(raw_debug + "\n");
                        std::string unescaped_debug = "[DEBUG] Unescaped file_path: " + unescaped_path.substr(0, 100) + (unescaped_path.length() > 100 ? "..." : "");
                        writeConsoleUtf8(unescaped_debug + "\n");
#else
                        std::cout << "[DEBUG] Raw file_path from JSON: " << file_path.substr(0, 100) << (file_path.length() > 100 ? "..." : "") << std::endl;
                        std::cout << "[DEBUG] Unescaped file_path: " << unescaped_path.substr(0, 100) << (unescaped_path.length() > 100 ? "..." : "") << std::endl;
#endif
                        
                        // Check if this is a full path or just filename
                        if (unescaped_path.find('/') != std::string::npos || unescaped_path.find('\\') != std::string::npos) {
                            // Full path - try to find final converted file (mp3 instead of opus/webm/etc)
                            std::string final_path = findFinalConvertedFile(unescaped_path, format);
                            last_file_path = final_path;
                            // Also set in progress info for playlist items
                            if (playlist_detected && json_info.current_item_index >= 0) {
                                last_progress.current_file_path = final_path;
                            }
#ifdef _WIN32
                            std::string debug_msg = "[DEBUG] File path from JSON filename (full path): " + last_file_path;
                            writeConsoleUtf8(debug_msg + "\n");
#else
                            std::cout << "[DEBUG] File path from JSON filename (full path): " << last_file_path << std::endl;
#endif
                        } else {
                            // Just filename - save for later use
                            filename_from_json = unescaped_path;
                            std::cout << "[DEBUG] Filename from JSON (name only): " << filename_from_json << std::endl;
                        }
                    }
                }
                // Also try "filepath" field (alternative)
                if (last_file_path.empty()) {
                    size_t filepath_pos = line.find("\"filepath\":");
                    if (filepath_pos != std::string::npos) {
                        size_t start = line.find('"', filepath_pos + 11) + 1;
                        size_t end = line.find('"', start);
                        if (end != std::string::npos && end > start) {
                            std::string file_path = line.substr(start, end - start);
                            // Use JsonUtils::unescapeJsonString to properly decode Unicode escape sequences
                            std::string unescaped_path = JsonUtils::unescapeJsonString(file_path);
                            // Try to find final converted file (mp3 instead of opus/webm/etc)
                            std::string final_path = findFinalConvertedFile(unescaped_path, format);
                            last_file_path = final_path;
                            // Also set in progress info for playlist items
                            if (playlist_detected && json_info.current_item_index >= 0) {
                                last_progress.current_file_path = final_path;
                                    }
#ifdef _WIN32
                            std::string debug_msg = "[DEBUG] File path from JSON filepath: " + last_file_path;
                            writeConsoleUtf8(debug_msg + "\n");
#else
                            std::cout << "[DEBUG] File path from JSON filepath: " << last_file_path << std::endl;
#endif
                        }
                    }
                }
            }
            
            // Call progress callback with JSON data
            // IMPORTANT: Also call if we have a title (even if is_playlist is not yet set)
            // This ensures first track's title is sent to UI immediately
            if (progress_cb && (json_info.progress > 0 || !json_info.status.empty() || json_info.is_playlist || !json_info.current_item_title.empty())) {
                progress_cb(last_progress);
            }
            
                // Continue to next line (JSON parsed, skip text parsing)
                continue;
            }
        }
        
        // Fallback to text parsing for non-JSON lines (errors, warnings, etc.)
        // Detect playlist information and extract playlist name
        // Look for "[download] Downloading playlist: NAME"
        if (line.find("Downloading playlist:") != std::string::npos) {
            size_t colon_pos = line.find("Downloading playlist:");
            if (colon_pos != std::string::npos) {
                // Find the colon after "Downloading playlist"
                size_t actual_colon = line.find(':', colon_pos);
                if (actual_colon != std::string::npos) {
                    size_t name_start = actual_colon + 1; // After the colon
                    // Skip whitespace after colon
                    while (name_start < line.length() && (line[name_start] == ' ' || line[name_start] == '\t')) {
                        name_start++;
                    }
                    // Extract playlist name until newline
                    size_t name_end = name_start;
                    while (name_end < line.length() && line[name_end] != '\n' && line[name_end] != '\r') {
                        name_end++;
                    }
                    if (name_end > name_start) {
                        std::string playlist_name = line.substr(name_start, name_end - name_start);
                        // Remove trailing whitespace
                        while (!playlist_name.empty() && (playlist_name.back() == ' ' || playlist_name.back() == '\t' || playlist_name.back() == '\r' || playlist_name.back() == '\n')) {
                            playlist_name.pop_back();
                        }
                        if (!playlist_name.empty()) {
                            last_progress.playlist_name = playlist_name;
                            std::cout << "[DEBUG] Extracted playlist name: \"" << playlist_name << "\"" << std::endl;
                        }
                    }
                }
            }
        }
        
        if (line.find("playlist") != std::string::npos || line.find("Playlist") != std::string::npos) {
            if (!playlist_detected) {
                playlist_detected = true;
                last_progress.is_playlist = true;
                std::cout << "[DEBUG] *** PLAYLIST DETECTED in yt-dlp output! Setting is_playlist=true ***" << std::endl;
                // Immediately notify UI about playlist detection
                if (progress_cb) {
                    progress_cb(last_progress);
                }
            }
        }
        
        // Detect playlist item count (e.g., "[download] Downloading item 1 of 10")
        // Use more specific pattern to avoid false matches
        if (line.find("Downloading item") != std::string::npos || 
            line.find("Downloading video") != std::string::npos) {
            
            // Try to extract current item index and total count (e.g., "item 1 of 10")
            // Use regex-like parsing for more reliability
            size_t item_pos = line.find("item");
            size_t of_pos = line.find(" of ");
            if (item_pos != std::string::npos && of_pos != std::string::npos && of_pos > item_pos) {
                // Extract current item number - look for number after "item" and before " of "
                size_t num_start = item_pos + 4;  // After "item"
                while (num_start < of_pos && (line[num_start] == ' ' || line[num_start] == '\t')) {
                    num_start++;
                }
                if (num_start < of_pos) {
                    try {
                        int current_item = std::stoi(line.substr(num_start, of_pos - num_start));
                        int new_index = current_item - 1;  // Convert to 0-based
                        
                        // Only update if index actually changed (to avoid duplicate updates)
                        if (new_index != last_progress.current_item_index) {
                            last_progress.current_item_index = new_index;
                            // Clear title when moving to next item to avoid showing wrong title
                            current_item_title.clear();
                            last_progress.current_item_title.clear();
                            std::cout << "[DEBUG] *** Playlist item index updated to: " << new_index << " (1-based: " << current_item << ") ***" << std::endl;
                        }
                    } catch (...) {
                        // Ignore parse errors
                    }
                }
                
                // Extract total count - look for number after " of "
                size_t count_start = of_pos + 4;  // After " of "
                while (count_start < line.length() && (line[count_start] == ' ' || line[count_start] == '\t')) {
                    count_start++;
                }
                if (count_start < line.length()) {
                    // Find end of number (space, newline, or non-digit)
                    size_t count_end = count_start;
                    while (count_end < line.length() && std::isdigit(line[count_end])) {
                        count_end++;
                    }
                    if (count_end > count_start) {
                        try {
                            int total = std::stoi(line.substr(count_start, count_end - count_start));
                            if (total != playlist_total_items && total > 0) {
                                playlist_total_items = total;
                                last_progress.total_items = total;
                                std::cout << "[DEBUG] Playlist total items: " << playlist_total_items << std::endl;
                            }
                        } catch (...) {
                            // Ignore parse errors
                        }
                    }
                }
            }
            
            last_progress.is_playlist = true;
            // Don't increment playlist_item_count here - it's not reliable
            // Instead, use current_item_index which is parsed from the line
        }
        
        // Try to extract current item title from yt-dlp output
        // Look for patterns like "[download] Destination: filename" or "[ExtractAudio] Destination: filename"
        // This is more reliable than parsing "Downloading item" because it happens when the file actually starts downloading
        if (playlist_detected && (line.find("Destination:") != std::string::npos)) {
            // Try to extract filename/title from the line
            size_t dest_pos = line.find("Destination:");
            if (dest_pos != std::string::npos) {
                size_t start = dest_pos + 11;  // After "Destination:"
                while (start < line.length() && (line[start] == ' ' || line[start] == '\t')) {
                    start++;
                }
                // Extract filename (until newline or quote)
                size_t end = start;
                while (end < line.length() && line[end] != '\n' && line[end] != '\r' && line[end] != '"') {
                    end++;
                }
                if (end > start) {
                    std::string filename = line.substr(start, end - start);
                    // Remove path and extension to get title
                    size_t last_slash = filename.find_last_of("/\\");
                    if (last_slash != std::string::npos) {
                        filename = filename.substr(last_slash + 1);
                    }
                    size_t last_dot = filename.find_last_of(".");
                    if (last_dot != std::string::npos) {
                        filename = filename.substr(0, last_dot);
                    }
                    // Remove playlist index prefix if present (format: "XX - " or "X - " where X is playlist index)
                    // This handles cases where filename contains prefix from template: %(playlist_index)02d - %(title)s
                    if (filename.length() >= 4 && filename[2] == ' ' && filename[3] == '-' && filename[4] == ' ') {
                        // Check if first 2 characters are digits (format: "XX - ")
                        if (filename.length() >= 5 && std::isdigit(filename[0]) && std::isdigit(filename[1])) {
                            filename = filename.substr(5); // Remove "XX - "
                        } else if (filename.length() >= 4 && std::isdigit(filename[0])) {
                            // Format: "X - " (single digit)
                            filename = filename.substr(4); // Remove "X - "
                        }
                    }
                    if (!filename.empty()) {
                        // Update title when we see Destination: - this is when the item actually starts downloading
                        // This is more reliable than using "Downloading item" which may appear multiple times
                        current_item_title = filename;
                        last_progress.current_item_title = filename;
                        
                        // If we have a valid current_item_index, update progress immediately
                        // This ensures the title is applied to the correct element
                        if (last_progress.current_item_index >= 0) {
                            if (progress_cb) {
                                progress_cb(last_progress);
                            }
                        }
                        std::cout << "[DEBUG] Extracted item title from Destination: " << filename 
                                  << " for item index " << last_progress.current_item_index << std::endl;
                    }
                }
            }
        }
        
        // Log important yt-dlp output
        if (line.find("[download]") != std::string::npos || 
            line.find("[ExtractAudio]") != std::string::npos ||
            line.find("[Merger]") != std::string::npos ||
            line.find("[info]") != std::string::npos) {
            // Remove newline for cleaner output
            std::string log_line = line;
            while (!log_line.empty() && (log_line.back() == '\n' || log_line.back() == '\r')) {
                log_line.pop_back();
            }
            std::cout << "[DEBUG] yt-dlp: " << log_line << std::endl;
        }
        
        // Check for errors first
        if (line.find("ERROR:") != std::string::npos) {
            // Extract error message
            size_t error_pos = line.find("ERROR:");
            if (error_pos != std::string::npos) {
                std::string error = line.substr(error_pos + 6); // Skip "ERROR:"
                // Trim whitespace
                while (!error.empty() && (error.front() == ' ' || error.front() == '\t')) {
                    error.erase(0, 1);
                }
                while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) {
                    error.pop_back();
                }
                if (!error.empty()) {
                    error_message = error;
                    std::cout << "[DEBUG] ERROR detected: " << error << std::endl;
                }
            }
        } else if (line.find("WARNING:") != std::string::npos) {
            // Extract warning message (might be important)
            size_t warn_pos = line.find("WARNING:");
            if (warn_pos != std::string::npos) {
                std::string warning = line.substr(warn_pos + 8); // Skip "WARNING:"
                // Trim whitespace
                while (!warning.empty() && (warning.front() == ' ' || warning.front() == '\t')) {
                    warning.erase(0, 1);
                }
                while (!warning.empty() && (warning.back() == '\n' || warning.back() == '\r')) {
                    warning.pop_back();
                }
                // Some warnings are actually errors
                if (warning.find("Unable to download") != std::string::npos ||
                    warning.find("Video unavailable") != std::string::npos ||
                    warning.find("Private video") != std::string::npos ||
                    warning.find("Sign in to confirm") != std::string::npos) {
                    error_message = warning;
                }
            }
        }
        
        // Parse progress from line
        ProgressInfo info = parseProgress(line);
        if (info.progress > 0 || !info.status.empty()) {
            if (info.progress > 0) {
                last_progress.progress = info.progress;
                last_progress.downloaded = info.downloaded;
                last_progress.total = info.total;
                last_progress.speed = info.speed;
            } else if (!info.status.empty()) {
                last_progress.status = info.status;
            }
            if (progress_cb) {
                progress_cb(last_progress);
            }
        }
        
        // Try to extract filename from output
        if (line.find("[download] Destination:") != std::string::npos ||
            line.find("[ExtractAudio] Destination:") != std::string::npos ||
            line.find("[Merger] Merging formats into") != std::string::npos) {
            // Extract path after "Destination:" or "into"
            size_t pos = line.find(":");
            if (pos == std::string::npos) {
                pos = line.find("into");
                if (pos != std::string::npos) {
                    pos += 4; // Skip "into"
                }
            } else {
                pos += 1; // Skip ":"
            }
            
            if (pos != std::string::npos) {
                last_file_path = line.substr(pos);
                // Trim whitespace
                while (!last_file_path.empty() && (last_file_path.front() == ' ' || last_file_path.front() == '\t')) {
                    last_file_path.erase(0, 1);
                }
                // Remove newline
                if (!last_file_path.empty() && last_file_path.back() == '\n') {
                    last_file_path.pop_back();
                }
                // Remove quotes if present
                if (!last_file_path.empty() && last_file_path.front() == '"') {
                    last_file_path.erase(0, 1);
                }
                if (!last_file_path.empty() && last_file_path.back() == '"') {
                    last_file_path.pop_back();
                }
            }
        }
        
        // Also check for final file path in post-processing
        if (line.find("Deleting original file") != std::string::npos ||
            line.find("has already been downloaded") != std::string::npos ||
            line.find("[download]") != std::string::npos && line.find("has already been downloaded") != std::string::npos) {
            // File path might be in the line
            size_t quote_start = line.find('"');
            if (quote_start != std::string::npos) {
                size_t quote_end = line.find('"', quote_start + 1);
                if (quote_end != std::string::npos) {
                    last_file_path = line.substr(quote_start + 1, quote_end - quote_start - 1);
                }
            } else {
                // Try to extract path without quotes (yt-dlp format: [download] filename has already been downloaded)
                size_t download_pos = line.find("[download]");
                if (download_pos != std::string::npos) {
                    size_t start = download_pos + 10; // Skip "[download]"
                    // Skip whitespace
                    while (start < line.length() && (line[start] == ' ' || line[start] == '\t')) {
                        start++;
                    }
                    // Find "has already been downloaded"
                    size_t end = line.find(" has already been downloaded", start);
                    if (end != std::string::npos) {
                        std::string potential_path = line.substr(start, end - start);
                        // Check if it's a valid path (contains / or starts with output_dir)
                        if (potential_path.find('/') != std::string::npos || potential_path.find('\\') != std::string::npos) {
                            last_file_path = potential_path;
                        } else {
                            // It's just a filename, prepend output_dir
                            last_file_path = output_dir + "/" + potential_path;
                        }
                    }
                }
            }
        }
        
    }
    
    // Process any remaining incomplete JSON line after loop ends
    if (!current_json_line.empty()) {
        line = current_json_line;
        // Remove trailing newline
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        
        std::string trimmed_line = line;
        while (!trimmed_line.empty() && (trimmed_line.front() == ' ' || trimmed_line.front() == '\t')) {
            trimmed_line = trimmed_line.substr(1);
        }
        
        if (!trimmed_line.empty() && trimmed_line.front() == '{') {
            // std::cout << "[DEBUG] JSON received (final incomplete line): " << trimmed_line << std::endl;
            ProgressInfo json_info = parseJsonProgress(trimmed_line);
            
            if (json_info.progress > 0 || json_info.downloaded > 0 || !json_info.status.empty()) {
                if (json_info.progress > 0) {
                    last_progress.progress = json_info.progress;
                }
                if (json_info.downloaded > 0) {
                    last_progress.downloaded = json_info.downloaded;
                }
                if (json_info.total > 0) {
                    last_progress.total = json_info.total;
                }
                if (json_info.speed > 0) {
                    last_progress.speed = json_info.speed;
                }
                if (!json_info.status.empty()) {
                    last_progress.status = json_info.status;
                }
            }
            
            if (!json_info.thumbnail_url.empty()) {
                last_progress.thumbnail_url = json_info.thumbnail_url;
                std::cout << "[DEBUG] Updated last_progress.thumbnail_url from JSON (final): " << last_progress.thumbnail_url << std::endl;
                if (progress_cb) {
                    progress_cb(last_progress);
                }
            }
            
            if (progress_cb && (json_info.progress > 0 || !json_info.status.empty() || json_info.is_playlist || !json_info.current_item_title.empty())) {
                progress_cb(last_progress);
            }
        }
    }
    
    // CRITICAL: If cancelled, terminate process immediately on Windows
    // On Unix, closing the pipe will send SIGPIPE to the process
    if (was_cancelled || (cancel_flag && cancel_flag->load())) {
        std::cout << "[DEBUG] Download cancelled - terminating process" << std::endl;
#ifdef _WIN32
        // On Windows, force terminate the process immediately
        if (process_info.isValid()) {
            ProcessLauncher::terminateProcess(process_info);
            pipe_guard.release();  // Prevent double close (terminateProcess already closed everything)
        }
#else
        // On Unix, closing the pipe will cause the process to receive SIGPIPE
        // But we should also try to kill it explicitly if possible
        if (pipe_guard.get()) {
            pclose(pipe_guard.get());
            pipe_guard.release();
        }
#endif
        complete_cb("", "Download cancelled");
        return;
    }
    
    // Close pipe and get status (pipe_guard will ensure it's closed even on exception)
    int status = -1;
#ifdef _WIN32
    // On Windows with ProcessLauncher, get status from process handle
    if (process_info.isValid()) {
        status = ProcessLauncher::closeProcess(process_info);
        pipe_guard.release();  // Prevent double close (closeProcess already closed the pipe)
    }
#else
    if (pipe_guard.get()) {
        status = pclose(pipe_guard.get());
        pipe_guard.release();  // Prevent double close
    }
#endif
    
    if (playlist_detected) {
        std::cout << "[DEBUG] *** PLAYLIST DOWNLOAD COMPLETED ***" << std::endl;
        // Calculate processed items count from last_known_item_index (0-based, so +1 for count)
        int processed_count = (last_known_item_index >= 0) ? (last_known_item_index + 1) : 0;
        std::cout << "[DEBUG] Items processed: " << processed_count;
        if (playlist_total_items > 0) {
            std::cout << " of " << playlist_total_items;
        }
        std::cout << std::endl;
    }
    
    std::cout << "[DEBUG] yt-dlp process finished with status: " << status << std::endl;
    
    // Check if final file exists (for error recovery)
    bool file_exists = false;
    std::string final_file_path = last_file_path;
    
    // Try to find final converted file in the requested format
    if (!final_file_path.empty()) {
        std::string converted_path = findFinalConvertedFile(final_file_path, format);
        if (!converted_path.empty() && converted_path != final_file_path) {
            struct stat st;
            if (stat(converted_path.c_str(), &st) == 0) {
                final_file_path = converted_path;
                file_exists = true;
                std::cout << "[DEBUG] Found final converted file: " << final_file_path << std::endl;
            }
        }
    }
    
    // Check if the file we have exists
    if (!file_exists && !final_file_path.empty()) {
        struct stat st;
        if (stat(final_file_path.c_str(), &st) == 0) {
            file_exists = true;
        }
    }
    
    // If file exists, ignore "Unable to rename" or file deletion errors (non-critical)
    // Also ignore "Did not get any data blocks" if file exists (file was downloaded successfully)
    // Only check this if there's actually an error
    bool ignore_error = false;
    if (file_exists && (status != 0 || !error_message.empty())) {
        if (error_message.find("Unable to rename") != std::string::npos ||
            error_message.find("No such file or directory") != std::string::npos ||
            error_message.find("Did not get any data blocks") != std::string::npos ||
            (status == 256 && error_message.empty())) {  // Status 256 often means non-critical file operation errors
            std::cout << "[DEBUG] Ignoring non-critical file operation error - final file exists: " << final_file_path << std::endl;
            ignore_error = true;
            // Update last_file_path to the final file
            if (!final_file_path.empty()) {
                last_file_path = final_file_path;
            }
        }
    }
    
    if ((status != 0 || !error_message.empty()) && !ignore_error) {
        // Build detailed error message
        std::string detailed_error;
        if (!error_message.empty()) {
            detailed_error = error_message;
        } else {
            detailed_error = "yt-dlp exited with error code: " + std::to_string(status);
        }
        
        // Add common error explanations
        if (error_message.find("Private video") != std::string::npos) {
            detailed_error += " (Video is private or unavailable)";
        } else if (error_message.find("Sign in to confirm") != std::string::npos) {
            detailed_error += " (Включите cookies в настройках / Enable cookies in settings)";
        } else if (error_message.find("Video unavailable") != std::string::npos) {
            detailed_error += " (Video has been removed or is unavailable)";
        } else if (error_message.find("Read timed out") != std::string::npos ||
                   error_message.find("Read timeout") != std::string::npos ||
                   error_message.find("Connection timed out") != std::string::npos) {
            detailed_error += " (Connection timeout - YouTube may be blocked by DPI filters. Try using VPN/proxy)";
        } else if (error_message.find("Unable to download API page") != std::string::npos ||
                   (error_message.find("Unable to download") != std::string::npos && 
                    error_message.find("youtube.com") != std::string::npos)) {
            detailed_error += " (Cannot connect to YouTube - may be blocked. Try using VPN/proxy in settings)";
        } else if (error_message.find("Unable to download") != std::string::npos) {
            detailed_error += " (Check your internet connection and URL)";
        } else if (error_message.find("403") != std::string::npos) {
            detailed_error += " (Access forbidden - may need VPN/proxy)";
        } else if (error_message.find("429") != std::string::npos) {
            detailed_error += " (Too many requests - wait a few minutes)";
        } else if (status == 1) {
            detailed_error += " (General error - check URL and platform support)";
        }
        
        std::cout << "[DEBUG] Download failed with error: " << detailed_error << std::endl;
        complete_cb("", detailed_error);
        return;
    }
    
    // If we didn't get the file path from output, try to find it in the output directory
    // This is a fallback - first try to get expected filename, then search for the most recently modified file
    if (last_file_path.empty() || !std::ifstream(last_file_path).good()) {
        std::cout << "[DEBUG] File path not found in output, last_file_path=" << last_file_path << std::endl;
        
        // First, try to get expected filename using getExpectedFilename
        // This is more reliable than searching for the most recently modified file
        // especially when file already exists and yt-dlp doesn't output the path
        // Use it for single files or playlists with one item (which are effectively single files)
        bool is_single_file = !playlist_detected || (playlist_detected && playlist_item_count <= 1 && playlist_total_items <= 1);
        if (is_single_file) {
            std::string expected_path = getExpectedFilename(url, output_dir, format);
            if (!expected_path.empty()) {
                // Check if expected file exists
                struct stat expected_stat;
                if (stat(expected_path.c_str(), &expected_stat) == 0) {
                    last_file_path = expected_path;
                    std::cout << "[DEBUG] Found expected file: " << last_file_path << std::endl;
                } else {
                    // If getExpectedFilename returned intermediate format, try final format extension
                    // (yt-dlp --get-filename sometimes returns original extension before conversion)
                    std::string expected_ext = "." + format;
                    size_t last_dot = expected_path.find_last_of(".");
                    if (last_dot != std::string::npos) {
                        std::string current_ext = expected_path.substr(last_dot);
                        // If current extension is not the target format, try target format
                        if (current_ext != expected_ext) {
                            std::string final_path = expected_path.substr(0, last_dot) + expected_ext;
                            struct stat final_stat;
                            if (stat(final_path.c_str(), &final_stat) == 0) {
                                last_file_path = final_path;
                                std::cout << "[DEBUG] Found expected file (converted to " << format << "): " << last_file_path << std::endl;
                            } else {
                                std::cout << "[DEBUG] Expected file not found (" << current_ext << " or " << expected_ext << "): " << expected_path << ", will try filename from JSON" << std::endl;
                            }
                        } else {
                            std::cout << "[DEBUG] Expected file not found: " << expected_path << ", will try filename from JSON" << std::endl;
                        }
                    } else {
                        std::cout << "[DEBUG] Expected file not found: " << expected_path << ", will try filename from JSON" << std::endl;
                    }
                }
            } else {
                std::cout << "[DEBUG] getExpectedFilename returned empty path, will try filename from JSON" << std::endl;
            }
            
            // If still no path found and we have filename from JSON, try to construct path from it
            if (last_file_path.empty() && !filename_from_json.empty()) {
                // filename_from_json might be a full path or just filename
                std::string constructed_path;
                if (filename_from_json.find('/') != std::string::npos || filename_from_json.find('\\') != std::string::npos) {
                    // It's a full path
                    constructed_path = filename_from_json;
                } else {
                    // It's just a filename, prepend output_dir
                    constructed_path = output_dir + "/" + filename_from_json;
                }
                
                // Try to find file with various extensions (for format conversion)
                std::vector<std::string> extensions_to_try;
                std::string expected_ext = "." + format;
                size_t last_dot = constructed_path.find_last_of(".");
                if (last_dot != std::string::npos) {
                    std::string base_path = constructed_path.substr(0, last_dot);
                    std::string original_ext = constructed_path.substr(last_dot);
                    extensions_to_try.push_back(original_ext);  // Try original extension first
                    extensions_to_try.push_back(expected_ext);  // Then try target format
                    // Also try common intermediate audio extensions that might be converted
                    extensions_to_try.push_back(".webm");
                    extensions_to_try.push_back(".opus");
                    if (format != "m4a") {
                        extensions_to_try.push_back(".m4a");
                    }
                    if (format != "ogg") {
                        extensions_to_try.push_back(".ogg");
                    }
                    if (format != "flac") {
                        extensions_to_try.push_back(".flac");
                    }
                    if (format != "mp3") {
                        extensions_to_try.push_back(".mp3");
                    }
                } else {
                    extensions_to_try.push_back(expected_ext);
                    // Also try common intermediate formats
                    extensions_to_try.push_back(".webm");
                    extensions_to_try.push_back(".opus");
                    if (format != "m4a") extensions_to_try.push_back(".m4a");
                    if (format != "ogg") extensions_to_try.push_back(".ogg");
                    if (format != "flac") extensions_to_try.push_back(".flac");
                    if (format != "mp3") extensions_to_try.push_back(".mp3");
                }
                
                // Try each extension
                for (const std::string& ext : extensions_to_try) {
                    std::string test_path = constructed_path;
                    if (!ext.empty()) {
                        size_t last_dot = test_path.find_last_of(".");
                        if (last_dot != std::string::npos) {
                            test_path = test_path.substr(0, last_dot) + ext;
                        } else {
                            test_path = test_path + ext;
                        }
                    }
                    
                    struct stat stat_file;
                    if (stat(test_path.c_str(), &stat_file) == 0) {
                        last_file_path = test_path;
                        std::cout << "[DEBUG] Found file using filename from JSON: " << last_file_path << std::endl;
                        break;
                    }
                }
                
                if (last_file_path.empty()) {
                    std::cout << "[DEBUG] File not found using filename from JSON (tried multiple extensions): " << constructed_path << std::endl;
                }
            }
        } else {
            std::cout << "[DEBUG] Playlist with multiple items detected, skipping getExpectedFilename" << std::endl;
        }
        
        // If still no path found, search for the most recently modified file in the output directory
        // WARNING: This is unreliable for single files - only use as last resort for playlists
        // For single files, we should have found the file by now using filename from JSON or getExpectedFilename
        if (last_file_path.empty()) {
            // For single files, don't use "most recently modified" - it's too unreliable
            // Only use this for playlists where we might have multiple files
            if (playlist_detected && playlist_item_count > 1) {
            std::cout << "[DEBUG] Searching for files in output directory: " << output_dir << std::endl;
            std::string found_path;
            time_t latest_time = 0;
        
#ifdef _WIN32
        // Use Unicode version for proper support of non-ASCII filenames
        std::string search_pattern = output_dir + "\\*";
        int pattern_size = MultiByteToWideChar(CP_UTF8, 0, search_pattern.c_str(), -1, NULL, 0);
        if (pattern_size > 0) {
            std::wstring wide_pattern(pattern_size, 0);
            MultiByteToWideChar(CP_UTF8, 0, search_pattern.c_str(), -1, &wide_pattern[0], pattern_size);
            wide_pattern.resize(pattern_size - 1);
            
            WIN32_FIND_DATAW find_data;
            HANDLE find_handle = FindFirstFileW(wide_pattern.c_str(), &find_data);
            if (find_handle != INVALID_HANDLE_VALUE) {
                do {
                    if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        // Convert wide filename to UTF-8
                        // Get required buffer size (includes null terminator)
                        int filename_size = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, NULL, 0, NULL, NULL);
                        if (filename_size > 0) {
                            std::vector<char> file_name_buf(filename_size);
                            // Convert to UTF-8 (returns size including null terminator)
                            int converted_size = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, file_name_buf.data(), filename_size, NULL, NULL);
                            if (converted_size > 0 && converted_size <= filename_size) {
                                // Create string from buffer, excluding null terminator
                                std::string file_name(file_name_buf.data(), converted_size - 1);
                                
                                std::string file_path = output_dir + "\\" + file_name;
                            // Skip temporary files (.part, .f*.part, .temp, etc.)
                            if (ValidationUtils::isTemporaryFile(file_path)) {
                                std::cout << "[DEBUG] Skipping temporary file: " << file_path << std::endl;
                                continue;
                            }
                            // Use getFileMetadata for Unicode-aware stat
                            int64_t file_size = -1;
                            int64_t file_mtime = -1;
                            if (getFileMetadata(file_path, file_size, file_mtime)) {
                                if (file_mtime > latest_time) {
                                    latest_time = file_mtime;
                                    found_path = file_path;
                                }
                            }
                            }
                        }
                    }
                } while (FindNextFileW(find_handle, &find_data));
                FindClose(find_handle);
            }
        }
#else
        DIR* dir = opendir(output_dir.c_str());
        if (dir != nullptr) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_name[0] == '.') continue; // Skip hidden files
                
                std::string file_path = output_dir + "/" + entry->d_name;
                // Skip temporary files (.part, .f*.part, .temp, etc.)
                if (ValidationUtils::isTemporaryFile(file_path)) {
                    std::cout << "[DEBUG] Skipping temporary file: " << file_path << std::endl;
                    continue;
                }
                struct stat file_stat;
                if (stat(file_path.c_str(), &file_stat) == 0) {
                    if (S_ISREG(file_stat.st_mode)) { // Regular file
                        if (file_stat.st_mtime > latest_time) {
                            latest_time = file_stat.st_mtime;
                            found_path = file_path;
                        }
                    }
                }
            }
            closedir(dir);
        }
#endif
        
                if (!found_path.empty()) {
                    last_file_path = found_path;
                    std::cout << "[DEBUG] Found most recently modified file (playlist fallback): " << last_file_path << std::endl;
                } else {
                    std::cout << "[DEBUG] WARNING: No files found in output directory" << std::endl;
                }
            } else {
                // For single files, this is a problem - we should have found the file by now
                // But if download was successful (status == 0), try to find the most recently modified file
                // as a last resort (file was downloaded but path wasn't captured)
                if (status == 0) {
                    std::cout << "[DEBUG] Download successful but file path not found. Searching for most recently modified file in output directory..." << std::endl;
                    std::string found_path;
                    time_t latest_time = 0;
                    
                    // Determine expected extension based on format
                    std::string expected_ext = "." + format;
                    // Also check for intermediate formats that might not be converted yet
                    std::vector<std::string> possible_extensions = {expected_ext};
                    if (format == "mp3") {
                        possible_extensions.push_back(".webm");
                        possible_extensions.push_back(".opus");
                        possible_extensions.push_back(".m4a");
                    } else if (format == "ogg") {
                        possible_extensions.push_back(".opus");
                        possible_extensions.push_back(".webm");
                    } else if (format == "m4a") {
                        possible_extensions.push_back(".webm");
                        possible_extensions.push_back(".opus");
                    } else if (format == "flac") {
                        possible_extensions.push_back(".webm");
                        possible_extensions.push_back(".opus");
                        possible_extensions.push_back(".m4a");
                    } else if (format == "opus") {
                        possible_extensions.push_back(".webm");
                    }
                    
#ifdef _WIN32
                    // Use Unicode version for proper support of non-ASCII filenames
                    std::string search_pattern = output_dir + "\\*";
                    int pattern_size = MultiByteToWideChar(CP_UTF8, 0, search_pattern.c_str(), -1, NULL, 0);
                    if (pattern_size > 0) {
                        std::wstring wide_pattern(pattern_size, 0);
                        MultiByteToWideChar(CP_UTF8, 0, search_pattern.c_str(), -1, &wide_pattern[0], pattern_size);
                        wide_pattern.resize(pattern_size - 1);
                        
                        WIN32_FIND_DATAW find_data;
                        HANDLE find_handle = FindFirstFileW(wide_pattern.c_str(), &find_data);
                        if (find_handle != INVALID_HANDLE_VALUE) {
                            do {
                                if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                    // Convert wide filename to UTF-8
                                    // Get required buffer size (includes null terminator)
                                    int filename_size = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, NULL, 0, NULL, NULL);
                                    if (filename_size > 0) {
                                        std::vector<char> file_name_buf(filename_size);
                                        // Convert to UTF-8 (returns size including null terminator)
                                        int converted_size = WideCharToMultiByte(CP_UTF8, 0, find_data.cFileName, -1, file_name_buf.data(), filename_size, NULL, NULL);
                                        if (converted_size > 0 && converted_size <= filename_size) {
                                            // Create string from buffer, excluding null terminator
                                            std::string file_name(file_name_buf.data(), converted_size - 1);
                                            
                                            std::string full_path = output_dir + "\\" + file_name;
                                        
                                        // Skip temporary files
                                        if (ValidationUtils::isTemporaryFile(full_path)) {
                                            continue;
                                        }
                                        
                                        // Check if file matches any expected extension
                                        bool matches_format = false;
                                        for (const auto& ext : possible_extensions) {
                                            if (file_name.length() >= ext.length() && 
                                                file_name.substr(file_name.length() - ext.length()) == ext) {
                                                matches_format = true;
                                                break;
                                            }
                                        }
                                        
                                        if (matches_format) {
                                            FILETIME file_time = find_data.ftLastWriteTime;
                                            ULARGE_INTEGER ul;
                                            ul.LowPart = file_time.dwLowDateTime;
                                            ul.HighPart = file_time.dwHighDateTime;
                                            time_t file_time_t = (time_t)(ul.QuadPart / 10000000ULL - 11644473600ULL);
                                            
                                            if (file_time_t > latest_time) {
                                                latest_time = file_time_t;
                                                found_path = full_path;
                                            }
                                        }
                                        }
                                    }
                                }
                            } while (FindNextFileW(find_handle, &find_data));
                            FindClose(find_handle);
                        }
                    }
#else
                    DIR* dir = opendir(output_dir.c_str());
                    if (dir) {
                        struct dirent* entry;
                        while ((entry = readdir(dir)) != nullptr) {
                            if (entry->d_name[0] != '.') {
                                std::string file_name = entry->d_name;
                                std::string full_path = output_dir + "/" + file_name;
                                
                                // Skip temporary files
                                if (ValidationUtils::isTemporaryFile(full_path)) {
                                    continue;
                                }
                                
                                // Check if file matches any expected extension
                                bool matches_format = false;
                                for (const auto& ext : possible_extensions) {
                                    if (file_name.length() >= ext.length() && 
                                        file_name.substr(file_name.length() - ext.length()) == ext) {
                                        matches_format = true;
                                        break;
                                    }
                                }
                                
                                if (matches_format) {
                                    struct stat file_stat;
                                    if (stat(full_path.c_str(), &file_stat) == 0) {
                                        if (file_stat.st_mtime > latest_time) {
                                            latest_time = file_stat.st_mtime;
                                            found_path = full_path;
                                        }
                                    }
                                }
                            }
                        }
                        closedir(dir);
                    }
#endif
                    
                    if (!found_path.empty()) {
                        last_file_path = found_path;
#ifdef _WIN32
                        writeConsoleUtf8("[DEBUG] Found most recently modified file (fallback): " + last_file_path + "\n");
#else
                        std::cout << "[DEBUG] Found most recently modified file (fallback): " << last_file_path << std::endl;
#endif
                    } else {
                        std::cout << "[DEBUG] ERROR: Could not determine file path for single file download. "
                                  << "Filename from JSON: " << (filename_from_json.empty() ? "empty" : filename_from_json) << std::endl;
                    }
                } else {
                    std::cout << "[DEBUG] ERROR: Could not determine file path for single file download. "
                              << "Filename from JSON: " << (filename_from_json.empty() ? "empty" : filename_from_json) << std::endl;
                }
            }
        }
    }
    
    // Verify file exists and is not a temporary file
    if (!last_file_path.empty()) {
        // Double-check that we didn't get a temporary file
        if (ValidationUtils::isTemporaryFile(last_file_path)) {
            std::cout << "[DEBUG] WARNING: Found file is temporary, rejecting: " << last_file_path << std::endl;
            last_file_path.clear();
        } else {
            // Use getFileMetadata for Unicode-aware file check on Windows
            int64_t file_size = -1;
            int64_t file_mtime = -1;
            if (getFileMetadata(last_file_path, file_size, file_mtime)) {
#ifdef _WIN32
                writeConsoleUtf8("[DEBUG] File verified: " + last_file_path + " (size: " + std::to_string(file_size) + " bytes)\n");
#else
                std::cout << "[DEBUG] File verified: " << last_file_path << " (size: " << file_size << " bytes)" << std::endl;
#endif
            } else {
#ifdef _WIN32
                writeConsoleUtf8("[DEBUG] ERROR: File not found at path: " + last_file_path + "\n");
#else
                std::cout << "[DEBUG] ERROR: File not found at path: " << last_file_path << std::endl;
#endif
                // For playlists, empty path is acceptable (process finished, items were downloaded)
                if (!playlist_detected) {
                    complete_cb("", "Downloaded file not found: " + last_file_path);
                    return;
                } else {
                    std::cout << "[DEBUG] Playlist download: process finished, accepting empty path" << std::endl;
                }
            }
        }
    } else {
        // For playlists, empty path is acceptable if process finished successfully
        if (playlist_detected) {
            std::cout << "[DEBUG] Playlist download completed: process finished successfully, no single file path needed" << std::endl;
            // For playlists, we can pass empty path - items are already saved in playlist_item_file_paths
            complete_cb("", "");
            return;
        } else {
            std::cout << "[DEBUG] WARNING: No file path determined from yt-dlp output" << std::endl;
        }
    }
    
#ifdef _WIN32
    writeConsoleUtf8("[DEBUG] Download completed successfully: " + last_file_path + "\n");
#else
    std::cout << "[DEBUG] Download completed successfully: " << last_file_path << std::endl;
#endif
    complete_cb(last_file_path, "");
}

Downloader::ProgressInfo Downloader::parseProgress(const std::string& line) {
    ProgressInfo info;
    info.progress = 0.0f;
    info.downloaded = 0;
    info.total = 0;
    info.speed = 0;
    
    // Parse progress percentage: [download] 45.2% of 10.5MiB at 1.2MiB/s ETA 00:05
    // Also support formats without "at" speed: [download] 45.2% of 10.5MiB
    std::regex progress_regex(R"(\[download\]\s+(\d+\.?\d*)%\s+of\s+([\d.]+)([KMGT]?i?B)(?:\s+at\s+([\d.]+)([KMGT]?i?B)/s)?)");
    std::smatch match;
    
    if (std::regex_search(line, match, progress_regex)) {
        info.progress = std::stof(match[1].str()) / 100.0f;
        
        // Parse total size
        float total_size = std::stof(match[2].str());
        std::string total_unit = match[3].str();
        info.total = static_cast<int64_t>(total_size * parseSizeUnit(total_unit));
        
        // Parse speed (optional)
        if (match.size() > 5 && match[4].matched && match[5].matched) {
            float speed = std::stof(match[4].str());
            std::string speed_unit = match[5].str();
            info.speed = static_cast<int>(speed * parseSizeUnit(speed_unit));
        }
        
        info.downloaded = static_cast<int64_t>(info.total * info.progress);
        
        // Build status string
        std::ostringstream oss;
        oss << match[1].str() << "% - " << match[2].str() << match[3].str();
        if (info.speed > 0) {
            oss << " at " << match[4].str() << match[5].str() << "/s";
        }
        info.status = oss.str();
    } else {
        // Try to extract status message or just percentage
        if (line.find("[download]") != std::string::npos) {
            // Try to find just percentage: [download] 45.2%
            std::regex simple_progress_regex(R"(\[download\]\s+(\d+\.?\d*)%)");
            std::smatch simple_match;
            if (std::regex_search(line, simple_match, simple_progress_regex)) {
                info.progress = std::stof(simple_match[1].str()) / 100.0f;
                info.status = simple_match[1].str() + "%";
            } else {
                // Extract full status message
                size_t pos = line.find("[download]");
                if (pos != std::string::npos) {
                    info.status = line.substr(pos + 10);
                    // Remove leading/trailing whitespace
                    while (!info.status.empty() && (info.status.front() == ' ' || info.status.front() == '\t')) {
                        info.status.erase(0, 1);
                    }
                    // Remove newline
                    if (!info.status.empty() && info.status.back() == '\n') {
                        info.status.pop_back();
                    }
                }
            }
        }
    }
    
    return info;
}

Downloader::ProgressInfo Downloader::parseJsonProgress(const std::string& json_line) {
    ProgressInfo info;
    info.progress = 0.0f;
    info.downloaded = 0;
    info.total = 0;
    info.speed = 0;
    info.status = "";
    info.is_playlist = false;
    info.current_item_index = -1;
    info.total_items = 0;
    info.current_item_title = "";
    info.playlist_name = "";
    info.thumbnail_url = "";
    
    // Check if this looks like JSON (starts with {)
    if (json_line.empty() || json_line.front() != '{') {
        return info;  // Not JSON, return empty info
    }
    
    // Simple JSON parsing - extract key fields
    // Status field
    size_t status_pos = json_line.find("\"status\":");
    if (status_pos != std::string::npos) {
        size_t start = json_line.find('"', status_pos + 9) + 1;
        size_t end = json_line.find('"', start);
        if (end != std::string::npos && end > start) {
            info.status = json_line.substr(start, end - start);
        }
    }
    
    // Progress fields (downloaded_bytes, total_bytes)
    size_t downloaded_pos = json_line.find("\"downloaded_bytes\":");
    if (downloaded_pos != std::string::npos) {
        size_t start = downloaded_pos + 18;
        while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
            start++;
        }
        size_t end = start;
        while (end < json_line.length() && json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n') {
            end++;
        }
        if (end > start) {
            try {
                info.downloaded = std::stoll(json_line.substr(start, end - start));
            } catch (...) {
                // Ignore parse errors
            }
        }
    }
    
    size_t total_pos = json_line.find("\"total_bytes\":");
    if (total_pos != std::string::npos) {
        size_t start = total_pos + 14;
        while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
            start++;
        }
        size_t end = start;
        while (end < json_line.length() && json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n') {
            end++;
        }
        if (end > start) {
            try {
                info.total = std::stoll(json_line.substr(start, end - start));
            } catch (...) {
                // Ignore parse errors
            }
        }
    }
    
    // Calculate progress percentage
    if (info.total > 0) {
        info.progress = static_cast<float>(info.downloaded) / static_cast<float>(info.total);
    }
    
    // Speed field
    size_t speed_pos = json_line.find("\"speed\":");
    if (speed_pos != std::string::npos) {
        size_t start = speed_pos + 8;
        while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
            start++;
        }
        size_t end = start;
        while (end < json_line.length() && json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n') {
            end++;
        }
        if (end > start) {
            try {
                info.speed = static_cast<int>(std::stoll(json_line.substr(start, end - start)));
            } catch (...) {
                // Ignore parse errors
            }
        }
    }
    
    // Use JsonUtils::unescapeJsonString - no need for local lambda
    
    // Filename field (extract title from filename if title not available)
    size_t filename_pos = json_line.find("\"filename\":");
    if (filename_pos != std::string::npos) {
        size_t start = json_line.find('"', filename_pos + 11) + 1;
        size_t end = start;
        // Find end quote, handling escaped quotes
        while (end < json_line.length()) {
            if (json_line[end] == '"' && (end == start || json_line[end - 1] != '\\')) {
                break;
            }
            end++;
        }
        if (end > start && end < json_line.length()) {
            std::string raw_filename = json_line.substr(start, end - start);
            std::string filename = JsonUtils::unescapeJsonString(raw_filename);
            // Extract title from filename (remove path and extension)
            size_t last_slash = filename.find_last_of("/\\");
            if (last_slash != std::string::npos) {
                filename = filename.substr(last_slash + 1);
            }
            size_t last_dot = filename.find_last_of(".");
            if (last_dot != std::string::npos) {
                filename = filename.substr(0, last_dot);
            }
            // Remove playlist index prefix if present (format: "XX - " or "X - " where X is playlist index)
            // This handles cases where filename contains prefix from template: %(playlist_index)02d - %(title)s
            if (filename.length() >= 4 && filename[2] == ' ' && filename[3] == '-' && filename[4] == ' ') {
                // Check if first 2 characters are digits (format: "XX - ")
                if (filename.length() >= 5 && std::isdigit(filename[0]) && std::isdigit(filename[1])) {
                    filename = filename.substr(5); // Remove "XX - "
                } else if (filename.length() >= 4 && std::isdigit(filename[0])) {
                    // Format: "X - " (single digit)
                    filename = filename.substr(4); // Remove "X - "
                }
            }
            if (!filename.empty() && info.current_item_title.empty()) {
                info.current_item_title = filename;
                // std::cout << "[DEBUG] parseJsonProgress: Found title from 'filename' field: \"" << info.current_item_title << "\"" << std::endl;
            }
        }
    }
    
    // Title field (more reliable than filename)
    // Try multiple title fields for different platforms
    if (info.current_item_title.empty()) {
        size_t title_pos = json_line.find("\"title\":");
        if (title_pos != std::string::npos) {
            size_t start = json_line.find('"', title_pos + 8) + 1;
            size_t end = start;
            // Find end quote, handling escaped quotes
            while (end < json_line.length()) {
                if (json_line[end] == '"' && (end == start || json_line[end - 1] != '\\')) {
                    break;
                }
                end++;
            }
            if (end > start && end < json_line.length()) {
                std::string raw_title = json_line.substr(start, end - start);
                info.current_item_title = JsonUtils::unescapeJsonString(raw_title);
                // std::cout << "[DEBUG] parseJsonProgress: Found title from 'title' field: \"" << info.current_item_title << "\"" << std::endl;
            }
        }
    }
    
    // Try "fulltitle" field (used by some platforms like SoundCloud)
    if (info.current_item_title.empty()) {
        size_t fulltitle_pos = json_line.find("\"fulltitle\":");
        if (fulltitle_pos != std::string::npos) {
            size_t start = json_line.find('"', fulltitle_pos + 12) + 1;
            size_t end = start;
            // Find end quote, handling escaped quotes
            while (end < json_line.length()) {
                if (json_line[end] == '"' && (end == start || json_line[end - 1] != '\\')) {
                    break;
                }
                end++;
            }
            if (end > start && end < json_line.length()) {
                std::string raw_title = json_line.substr(start, end - start);
                info.current_item_title = JsonUtils::unescapeJsonString(raw_title);
                // std::cout << "[DEBUG] parseJsonProgress: Found title from 'fulltitle' field: \"" << info.current_item_title << "\"" << std::endl;
            }
        }
    }
    
    // Try "track" field (SoundCloud specific)
    if (info.current_item_title.empty()) {
        size_t track_pos = json_line.find("\"track\":");
        if (track_pos != std::string::npos) {
            // Track might be an object, try to find "title" inside it
            size_t track_title_pos = json_line.find("\"title\":", track_pos);
            if (track_title_pos != std::string::npos && track_title_pos < json_line.find("}", track_pos)) {
                size_t start = json_line.find('"', track_title_pos + 8) + 1;
                size_t end = start;
                // Find end quote, handling escaped quotes
                while (end < json_line.length()) {
                    if (json_line[end] == '"' && (end == start || json_line[end - 1] != '\\')) {
                        break;
                    }
                    end++;
                }
                if (end > start && end < json_line.length()) {
                    std::string raw_title = json_line.substr(start, end - start);
                    info.current_item_title = JsonUtils::unescapeJsonString(raw_title);
                    // std::cout << "[DEBUG] parseJsonProgress: Found title from 'track.title' field: \"" << info.current_item_title << "\"" << std::endl;
                }
            }
        }
    }
    
    // Playlist fields
    size_t playlist_index_pos = json_line.find("\"playlist_index\":");
    if (playlist_index_pos != std::string::npos) {
        size_t start = playlist_index_pos + 17;
        while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
            start++;
        }
        size_t end = start;
        while (end < json_line.length() && json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n') {
            end++;
        }
        if (end > start) {
            try {
                int index = std::stoi(json_line.substr(start, end - start));
                info.current_item_index = index - 1;  // Convert to 0-based
                info.is_playlist = true;
            } catch (...) {
                // Ignore parse errors
            }
        }
    }
    
    size_t playlist_count_pos = json_line.find("\"playlist_count\":");
    if (playlist_count_pos != std::string::npos) {
        size_t start = playlist_count_pos + 17;
        while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
            start++;
        }
        size_t end = start;
        while (end < json_line.length() && json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n') {
            end++;
        }
        if (end > start) {
            try {
                info.total_items = std::stoi(json_line.substr(start, end - start));
                info.is_playlist = true;
            } catch (...) {
                // Ignore parse errors
            }
        }
    }
    
    // Playlist title - try "playlist_title" first, then "playlist" as fallback
    // Playlist fields - use JsonUtils::extractJsonString
    // First check if playlist fields are null (indicates single file, not playlist)
    bool playlist_is_null = false;
    size_t playlist_pos_check = json_line.find("\"playlist\":");
    if (playlist_pos_check != std::string::npos) {
        size_t null_pos = json_line.find("null", playlist_pos_check);
        size_t next_comma = json_line.find(",", playlist_pos_check);
        size_t next_brace = json_line.find("}", playlist_pos_check);
        size_t next_field = (next_comma != std::string::npos && next_comma < next_brace) ? next_comma : next_brace;
        if (null_pos != std::string::npos && null_pos < next_field) {
            playlist_is_null = true;
        }
    }
    
    bool playlist_index_is_null = false;
    // playlist_index_pos already defined above, reuse it
    if (playlist_index_pos != std::string::npos) {
        size_t null_pos = json_line.find("null", playlist_index_pos);
        size_t next_comma = json_line.find(",", playlist_index_pos);
        size_t next_brace = json_line.find("}", playlist_index_pos);
        size_t next_field = (next_comma != std::string::npos && next_comma < next_brace) ? next_comma : next_brace;
        if (null_pos != std::string::npos && null_pos < next_field) {
            playlist_index_is_null = true;
        }
    }
    
    // Only extract playlist name if this is actually a playlist (not null)
    if (!playlist_is_null || !playlist_index_is_null) {
        // Try "playlist_title" first, then "playlist" as fallback
        info.playlist_name = JsonUtils::extractJsonString(json_line, "playlist_title");
        if (info.playlist_name.empty() && !playlist_is_null) {
            // Fallback to "playlist" field if "playlist_title" is not available
            // But only if "playlist" is not null
            info.playlist_name = JsonUtils::extractJsonString(json_line, "playlist");
        }
        // Check that we didn't accidentally extract "playlist_index" as playlist name
        if (info.playlist_name == "playlist_index") {
            info.playlist_name = "";
        }
        if (!info.playlist_name.empty()) {
            info.is_playlist = true;
        }
    }
    
    // Extract thumbnail URL from first playlist item (index 0 or playlist_index 1)
    // This is important for playlists to get thumbnail early during download
    // Also extract if we detect playlist by other means (playlist_name, playlist_count, etc.)
    // For SoundCloud, first item may arrive before is_playlist is set
    // Extract thumbnail if:
    // 1. It's explicitly a playlist item (is_playlist && index 0 or -1)
    // 2. We have playlist indicators (playlist_name, total_items) and it's the first item
    // 3. For SoundCloud, check if JSON contains playlist fields (playlist_title, playlist_count, album_type: "playlist")
    // 4. For any platform, if current_item_index is -1 or 0 and JSON has thumbnail/thumbnails, extract it
    bool is_first_item = (info.current_item_index == 0 || info.current_item_index == -1);
    bool has_playlist_indicators = !info.playlist_name.empty() || info.total_items > 0 || info.is_playlist;
    // Check if JSON contains playlist-related fields (even if not parsed yet)
    bool json_has_playlist_fields = (json_line.find("\"playlist_title\":") != std::string::npos ||
                                     json_line.find("\"playlist_count\":") != std::string::npos ||
                                     json_line.find("\"album_type\":\"playlist\"") != std::string::npos ||
                                     json_line.find("\"playlist\":") != std::string::npos);
    bool is_soundcloud = (json_line.find("\"extractor_key\":\"Soundcloud\"") != std::string::npos ||
                          json_line.find("soundcloud.com") != std::string::npos);
    // For SoundCloud or if JSON has playlist fields, extract thumbnail from first item
    // Also extract if we already know it's a playlist
    // IMPORTANT: For SoundCloud, always try to extract thumbnail from first item (index -1 or 0)
    // even if playlist detection hasn't happened yet, because SoundCloud playlists may have
    // playlist fields in the first item's JSON
    // IMPORTANT: For YouTube and other platforms, also try to extract thumbnail from first item
    // if JSON contains "thumbnail" field or "id" field (for YouTube), even if playlist detection hasn't happened yet
    bool should_extract_thumbnail = false;
    bool json_has_thumbnail = (json_line.find("\"thumbnail\":") != std::string::npos);
    bool json_has_id = (json_line.find("\"id\":") != std::string::npos);
    bool is_youtube_for_thumbnail = (json_line.find("youtube.com") != std::string::npos || 
                                      json_line.find("ytimg.com") != std::string::npos ||
                                      json_line.find("\"extractor_key\":\"Youtube\"") != std::string::npos);
    if (is_first_item) {
        if (is_soundcloud) {
            // For SoundCloud, always try if it's the first item
            should_extract_thumbnail = true;
            std::cout << "[DEBUG] parseJsonProgress: SoundCloud detected, will extract thumbnail from first item" << std::endl;
        } else if (info.is_playlist || has_playlist_indicators || json_has_playlist_fields || json_has_thumbnail || (is_youtube_for_thumbnail && json_has_id)) {
            // For other platforms, check playlist indicators OR if JSON has thumbnail field
            // OR if it's YouTube and has "id" field (can build thumbnail URL from video ID)
            // This allows extracting thumbnail from first item even before playlist is detected
            should_extract_thumbnail = true;
            if ((json_has_thumbnail || (is_youtube_for_thumbnail && json_has_id)) && !info.is_playlist && !has_playlist_indicators && !json_has_playlist_fields) {
                std::cout << "[DEBUG] parseJsonProgress: First item has thumbnail/id field, will extract (playlist may not be detected yet)" << std::endl;
            }
        }
    }
    
    std::cout << "[DEBUG] parseJsonProgress thumbnail check: is_first_item=" << is_first_item 
              << ", is_playlist=" << info.is_playlist 
              << ", has_playlist_indicators=" << has_playlist_indicators
              << ", json_has_playlist_fields=" << json_has_playlist_fields
              << ", json_has_thumbnail=" << json_has_thumbnail
              << ", json_has_id=" << json_has_id
              << ", is_youtube=" << is_youtube_for_thumbnail
              << ", is_soundcloud=" << is_soundcloud
              << ", should_extract=" << should_extract_thumbnail << std::endl;
    
    if (should_extract_thumbnail) {
        std::cout << "[DEBUG] parseJsonProgress: Attempting to extract thumbnail for first playlist item" << std::endl;
        // Use JsonUtils::extractThumbnailUrl for all platforms
        info.thumbnail_url = JsonUtils::extractThumbnailUrl(json_line);
        if (!info.thumbnail_url.empty()) {
            std::cout << "[DEBUG] parseJsonProgress: Extracted playlist thumbnail URL: " << info.thumbnail_url << std::endl;
        }
    }
    
    // Extract duration for current item from JSON
    // Duration in seconds (float) -> int
    // NOTE: bitrate is NOT extracted from JSON - it will be calculated from file_size and duration after conversion
    double duration_double = JsonUtils::extractJsonDouble(json_line, "duration");
    if (duration_double > 0) {
        info.duration = static_cast<int>(duration_double + 0.5); // round to nearest second
    }
    
    // REMOVED: bitrate extraction from JSON - bitrate will be calculated from file_size and duration after conversion
    // This ensures we get the actual bitrate of the converted file, not the source stream bitrate
    
    return info;
}

int64_t Downloader::parseSizeUnit(const std::string& unit) {
    if (unit == "B" || unit.empty()) return 1;
    if (unit == "KiB" || unit == "KB") return 1024;
    if (unit == "MiB" || unit == "MB") return 1024 * 1024;
    if (unit == "GiB" || unit == "GB") return 1024LL * 1024 * 1024;
    if (unit == "TiB" || unit == "TB") return 1024LL * 1024 * 1024 * 1024;
    return 1;
}

std::string Downloader::buildYtDlpCommand(
    const std::string& url,
    const std::string& output_dir,
    const std::string& format,
    const std::string& quality,
    const std::string& proxy,
    const std::string& spotify_api_key,
    const std::string& youtube_api_key,
    const std::string& soundcloud_api_key,
    bool download_playlist,
    const YtDlpSettings& settings,
    const std::string& playlist_items
) {
    std::ostringstream cmd;
    
    // Use found yt-dlp path
    std::string ytdlp_path = findYtDlpPath();
    
    // Escape yt-dlp path if it contains spaces (for bundle paths)
    if (ytdlp_path.find(' ') != std::string::npos || ytdlp_path.find('"') != std::string::npos) {
        // Escape quotes and wrap in quotes
        std::string escaped_path;
        for (char c : ytdlp_path) {
            if (c == '"' || c == '\\') {
                escaped_path += '\\';
            }
            escaped_path += c;
        }
        cmd << "\"" << escaped_path << "\"";
    } else {
        cmd << ytdlp_path;
    }

    // Prefer ffmpeg from app bundle, then system ffmpeg
    std::string ffmpeg_path = findFfmpegPath();
    if (!ffmpeg_path.empty()) {
        cmd << " --ffmpeg-location \"" << ffmpeg_path << "\"";
    }
    
    // Output template - normalize path separators for platform
    // CRITICAL: Always use simple template without playlist_index
    // This prevents unwanted "01 - " prefix for single files
    // playlist_index is not needed - use simple template for all cases
    std::string output_template = YtDlpConfig::OUTPUT_TEMPLATE;  // Simple: %(title)s.%(ext)s
    
    std::string normalized_output_dir = output_dir;
#ifdef _WIN32
    // On Windows, replace forward slashes with backslashes
    std::replace(normalized_output_dir.begin(), normalized_output_dir.end(), '/', '\\');
    // Remove duplicate backslashes
    std::string result;
    for (size_t i = 0; i < normalized_output_dir.length(); i++) {
        if (normalized_output_dir[i] != '\\' || result.empty() || result.back() != '\\') {
            result += normalized_output_dir[i];
        }
    }
    normalized_output_dir = result;
    // Use backslash for path separator on Windows
    std::string output_path = normalized_output_dir + "\\" + output_template;
    // Escape path for Windows cmd.exe (escape quotes in path)
    output_path = escapePathForWindows(output_path);
#else
    // On Unix, use forward slash
    std::string output_path = normalized_output_dir + "/" + output_template;
#endif
    cmd << " -o \"" << output_path << "\"";
    
    // Format selection - extract audio only, best quality
    cmd << " -f \"" << YtDlpConfig::FORMAT_SELECTION << "\"";
    
    // Post-processing: extract audio
    cmd << " -x";
    
    // Audio format - convert "ogg" to "vorbis" for yt-dlp compatibility
    std::string ytdlp_format = convertFormatForYtDlp(format);
    cmd << " --audio-format " << ytdlp_format;
    
    // Audio quality
    if (quality == "best") {
        cmd << " --audio-quality " << YtDlpConfig::AUDIO_QUALITY_BEST;  // Best quality
    } else if (quality == "320k") {
        cmd << " --audio-quality " << YtDlpConfig::AUDIO_QUALITY_320K;
    } else if (quality == "256k") {
        cmd << " --audio-quality " << YtDlpConfig::AUDIO_QUALITY_256K;
    } else if (quality == "192k") {
        cmd << " --audio-quality " << YtDlpConfig::AUDIO_QUALITY_192K;
    } else if (quality == "128k") {
        cmd << " --audio-quality " << YtDlpConfig::AUDIO_QUALITY_128K;
    } else {
        cmd << " --audio-quality " << YtDlpConfig::AUDIO_QUALITY_BEST;  // Default to best
    }
    
    // Proxy (support socks5, socks4, https, http formats)
    if (!proxy.empty()) {
        cmd << " --proxy \"" << proxy << "\"";
    }
    
    // API Keys
    if (!spotify_api_key.empty()) {
        cmd << " --extractor-args \"spotify:client_id=" << spotify_api_key << "\"";
    }
    if (!youtube_api_key.empty()) {
        cmd << " --extractor-args \"youtube:api_key=" << youtube_api_key << "\"";
    }
    if (!soundcloud_api_key.empty()) {
        cmd << " --extractor-args \"soundcloud:api_key=" << soundcloud_api_key << "\"";
    }
    
    // Anti-bot / rate-limit friendly options
    bool is_youtube_url =
        (url.find("youtube.com") != std::string::npos) ||
        (url.find("youtu.be")   != std::string::npos);
    
    if (is_youtube_url) {
        // YouTube now requires cookies for all requests (not just playlists)
        // Apply cookies if setting is enabled (for both single videos and playlists)
        // Priority: cookies file > browser cookies
        if (settings.use_cookies_file && !settings.cookies_file_path.empty()) {
            // Use cookies file
            cmd << " --cookies \"" << settings.cookies_file_path << "\"";
            std::cout << "[DEBUG] Using cookies file: " << settings.cookies_file_path << std::endl;
        } else if (settings.use_cookies_for_playlists) {
            // Use selected browser from settings, or find available browser
            std::string browser_for_cookies = settings.selected_browser;
            if (browser_for_cookies.empty()) {
                browser_for_cookies = findAvailableBrowser();
            }
            if (!browser_for_cookies.empty()) {
                cmd << " --cookies-from-browser " << browser_for_cookies;
                std::cout << "[DEBUG] Using browser cookies: " << browser_for_cookies << std::endl;
            } else {
                std::cout << "[DEBUG] No browser available for cookies, proceeding without cookies" << std::endl;
            }
        }
        
        // Additional sleep options for playlists to avoid rate limiting
        if (download_playlist) {
            if (settings.use_sleep_requests) {
                cmd << " --sleep-requests " << settings.playlist_sleep_requests;
            }
            if (settings.use_sleep_intervals_playlist) {
                cmd << " --sleep-interval " << settings.playlist_sleep_interval 
                    << " --max-sleep-interval " << settings.playlist_max_sleep_interval;
            }
        }
    }
    
    // Other options (common for all platforms)
    if (!download_playlist) {
        cmd << " --no-playlist";  // Don't download playlists (download only single video)
        std::cout << "[DEBUG] *** PLAYLIST DOWNLOAD DISABLED (--no-playlist) - downloading single video only ***" << std::endl;
    } else {
        std::cout << "[DEBUG] *** PLAYLIST DOWNLOAD ENABLED (will download all items from playlist) ***" << std::endl;
        // If specific playlist items are specified, download only those
        if (!playlist_items.empty()) {
            cmd << " --playlist-items " << playlist_items;  // e.g., "11" or "1,3,5"
            std::cout << "[DEBUG] *** Downloading specific playlist items: " << playlist_items << " ***" << std::endl;
        }
    }
    cmd << " --no-warnings";
    cmd << " --progress";       // Show progress
    cmd << " --newline";        // One progress update per line
    cmd << " --no-overwrites";  // Don't overwrite existing files, report them instead
    cmd << " --print-json";     // Output JSON events for reliable parsing
    
    // Timeout settings - important for SoundCloud HLS streams and slow connections
    // Only add if enabled in settings
    if (settings.use_socket_timeout) {
        cmd << " --socket-timeout " << settings.socket_timeout;  // User-configurable timeout for downloads
    }
    if (settings.use_fragment_retries) {
        cmd << " --fragment-retries " << settings.fragment_retries;  // User-configurable retries for HLS fragments
    }
    if (settings.use_concurrent_fragments) {
        cmd << " --concurrent-fragments " << settings.concurrent_fragments;  // User-configurable parallel fragments for HLS
    }
    
    // URL - escape special characters for Windows cmd.exe
    std::string escaped_url = escapeUrlForWindows(url);
    cmd << " \"" << escaped_url << "\"";
    
    return cmd.str();
}

// Build yt-dlp command as a vector of arguments (for use with CreateProcessW on Windows)
std::vector<std::string> Downloader::buildYtDlpArguments(
    const std::string& url,
    const std::string& output_dir,
    const std::string& format,
    const std::string& quality,
    const std::string& proxy,
    const std::string& spotify_api_key,
    const std::string& youtube_api_key,
    const std::string& soundcloud_api_key,
    bool download_playlist,
    const YtDlpSettings& settings,
    const std::string& playlist_items
) {
    std::vector<std::string> args;
    
    // Prefer ffmpeg from app bundle, then system ffmpeg
    std::string ffmpeg_path = findFfmpegPath();
    if (!ffmpeg_path.empty()) {
        args.push_back("--ffmpeg-location");
        args.push_back(ffmpeg_path);
    }
    
    // Output template - normalize path separators for platform
    // CRITICAL: Always use simple template to avoid "01 - " prefix for single files
    // For playlists, yt-dlp will still work correctly, but single files won't get unwanted prefix
    // If user wants playlist_index for playlists, they can enable it in settings
    // But for now, use simple template to avoid issues with single files
    std::string output_template = YtDlpConfig::OUTPUT_TEMPLATE;  // Simple: %(title)s.%(ext)s
    
    std::string normalized_output_dir = output_dir;
#ifdef _WIN32
    // On Windows, replace forward slashes with backslashes
    std::replace(normalized_output_dir.begin(), normalized_output_dir.end(), '/', '\\');
    // Remove duplicate backslashes
    std::string result;
    for (size_t i = 0; i < normalized_output_dir.length(); i++) {
        if (normalized_output_dir[i] != '\\' || result.empty() || result.back() != '\\') {
            result += normalized_output_dir[i];
        }
    }
    normalized_output_dir = result;
    std::string output_path = normalized_output_dir + "\\" + output_template;
#else
    std::string output_path = normalized_output_dir + "/" + output_template;
#endif
    args.push_back("-o");
    args.push_back(output_path);
    
    // Format selection
    args.push_back("-f");
    args.push_back(YtDlpConfig::FORMAT_SELECTION);
    
    // Post-processing: extract audio
    args.push_back("-x");
    
    // Audio format - convert "ogg" to "vorbis" for yt-dlp compatibility
    std::string ytdlp_format = convertFormatForYtDlp(format);
    args.push_back("--audio-format");
    args.push_back(ytdlp_format);
    
    // Audio quality
    args.push_back("--audio-quality");
    if (quality == "best") {
        args.push_back(YtDlpConfig::AUDIO_QUALITY_BEST);
    } else if (quality == "320k") {
        args.push_back(YtDlpConfig::AUDIO_QUALITY_320K);
    } else if (quality == "256k") {
        args.push_back(YtDlpConfig::AUDIO_QUALITY_256K);
    } else if (quality == "192k") {
        args.push_back(YtDlpConfig::AUDIO_QUALITY_192K);
    } else if (quality == "128k") {
        args.push_back(YtDlpConfig::AUDIO_QUALITY_128K);
    } else {
        args.push_back(YtDlpConfig::AUDIO_QUALITY_BEST);
    }
    
    // Proxy
    if (!proxy.empty()) {
        args.push_back("--proxy");
        args.push_back(proxy);
    }
    
    // API Keys
    if (!spotify_api_key.empty()) {
        args.push_back("--extractor-args");
        args.push_back("spotify:client_id=" + spotify_api_key);
    }
    if (!youtube_api_key.empty()) {
        args.push_back("--extractor-args");
        args.push_back("youtube:api_key=" + youtube_api_key);
    }
    if (!soundcloud_api_key.empty()) {
        args.push_back("--extractor-args");
        args.push_back("soundcloud:api_key=" + soundcloud_api_key);
    }
    
    // Anti-bot / rate-limit friendly options
    bool is_youtube_url =
        (url.find("youtube.com") != std::string::npos) ||
        (url.find("youtu.be")   != std::string::npos);
    
    if (is_youtube_url) {
        // YouTube now requires cookies for all requests (not just playlists)
        // Apply cookies if setting is enabled (for both single videos and playlists)
        // Priority: cookies file > browser cookies
        if (settings.use_cookies_file && !settings.cookies_file_path.empty()) {
            args.push_back("--cookies");
            args.push_back(settings.cookies_file_path);
        } else if (settings.use_cookies_for_playlists) {
            std::string browser_for_cookies = settings.selected_browser;
            if (browser_for_cookies.empty()) {
                browser_for_cookies = findAvailableBrowser();
            }
            if (!browser_for_cookies.empty()) {
                args.push_back("--cookies-from-browser");
                args.push_back(browser_for_cookies);
            }
        }
        
        // Additional sleep options for playlists to avoid rate limiting
        if (download_playlist) {
            if (settings.use_sleep_requests) {
                args.push_back("--sleep-requests");
                args.push_back(std::to_string(settings.playlist_sleep_requests));
            }
            if (settings.use_sleep_intervals_playlist) {
                args.push_back("--sleep-interval");
                args.push_back(std::to_string(settings.playlist_sleep_interval));
                args.push_back("--max-sleep-interval");
                args.push_back(std::to_string(settings.playlist_max_sleep_interval));
            }
        }
    }
    
    // Other options
    if (!download_playlist) {
        args.push_back("--no-playlist");
    } else if (!playlist_items.empty()) {
        args.push_back("--playlist-items");
        args.push_back(playlist_items);
    }
    
    args.push_back("--no-warnings");
    args.push_back("--progress");
    args.push_back("--newline");
    args.push_back("--no-overwrites");
    args.push_back("--print-json");
    
    // Timeout settings
    if (settings.use_socket_timeout) {
        args.push_back("--socket-timeout");
        args.push_back(std::to_string(settings.socket_timeout));
    }
    if (settings.use_fragment_retries) {
        args.push_back("--fragment-retries");
        args.push_back(std::to_string(settings.fragment_retries));
    }
    if (settings.use_concurrent_fragments) {
        args.push_back("--concurrent-fragments");
        args.push_back(std::to_string(settings.concurrent_fragments));
    }
    
    // URL - no escaping needed when using CreateProcessW!
    args.push_back(url);
    
    return args;
}

Downloader::VideoInfo Downloader::getVideoInfo(
    const std::string& url,
    const std::string& output_dir,
    const std::string& format,
    const std::string& proxy,
    const YtDlpSettings& settings
) {
    std::cout << "[DEBUG] getVideoInfo called: URL=" << url << ", Format=" << format << std::endl;
    
    VideoInfo info;
    
    std::string ytdlp_path = findYtDlpPath();
    std::cout << "[DEBUG] yt-dlp path: " << ytdlp_path << std::endl;
    std::ostringstream cmd;
    
    // Escape path if needed
    if (ytdlp_path.find(' ') != std::string::npos || ytdlp_path.find('"') != std::string::npos) {
        std::string escaped_path;
        for (char c : ytdlp_path) {
            if (c == '"' || c == '\\') {
                escaped_path += '\\';
            }
            escaped_path += c;
        }
        cmd << "\"" << escaped_path << "\"";
    } else {
        cmd << ytdlp_path;
    }
    
    // Get JSON info only - don't download or convert
    cmd << " --print-json";
    cmd << " --skip-download";          // Don't download, just get info
    cmd << " -f \"" << YtDlpConfig::FORMAT_SELECTION << "\"";    // Still specify format for metadata
    
    // Proxy (support socks5, socks4, https, http formats)
    if (!proxy.empty()) {
        cmd << " --proxy \"" << proxy << "\"";
    }
    
    // Anti-bot / rate-limit friendly options for YouTube metadata as well
    bool is_youtube_url =
        (url.find("youtube.com") != std::string::npos) ||
        (url.find("youtu.be")   != std::string::npos);
    
    // YouTube now requires cookies for all requests (including metadata)
    if (is_youtube_url) {
        if (settings.use_cookies_file && !settings.cookies_file_path.empty()) {
            cmd << " --cookies \"" << settings.cookies_file_path << "\"";
        } else if (settings.use_cookies_for_playlists) {
            std::string browser_for_cookies = settings.selected_browser;
            if (browser_for_cookies.empty()) {
                browser_for_cookies = findAvailableBrowser();
            }
            if (!browser_for_cookies.empty()) {
                cmd << " --cookies-from-browser " << browser_for_cookies;
            }
        }
    }
    
    cmd << " --socket-timeout " << YtDlpConfig::VIDEO_INFO_TIMEOUT;  // Add timeout for getVideoInfo
    cmd << " 2>&1";  // Capture stderr to detect errors
    
#ifdef _WIN32
    // On Windows, use CreateProcessW via ProcessLauncher - no escaping needed!
    std::vector<std::string> args;
    args.push_back("--print-json");
    args.push_back("--skip-download");
    args.push_back("-f");
    args.push_back(YtDlpConfig::FORMAT_SELECTION);
    
    if (!proxy.empty()) {
        args.push_back("--proxy");
        args.push_back(proxy);
    }
    
    // YouTube now requires cookies for all requests (including metadata)
    if (is_youtube_url) {
        if (settings.use_cookies_file && !settings.cookies_file_path.empty()) {
            args.push_back("--cookies");
            args.push_back(settings.cookies_file_path);
        } else if (settings.use_cookies_for_playlists) {
            std::string browser_for_cookies = settings.selected_browser;
            if (browser_for_cookies.empty()) {
                browser_for_cookies = findAvailableBrowser();
            }
            if (!browser_for_cookies.empty()) {
                args.push_back("--cookies-from-browser");
                args.push_back(browser_for_cookies);
            }
        }
    }
    
    args.push_back("--socket-timeout");
    args.push_back(std::to_string(YtDlpConfig::VIDEO_INFO_TIMEOUT));
    
    // URL - no escaping needed when using CreateProcessW!
    args.push_back(url);
    
    std::cout << "[DEBUG] getVideoInfo: Using CreateProcessW (no cmd.exe escaping needed!)" << std::endl;
    ProcessInfo process_info = ProcessLauncher::launchProcess(ytdlp_path, args, true); // Redirect stderr
    if (!process_info.isValid()) {
        std::cout << "[DEBUG] ERROR: Failed to start yt-dlp process via CreateProcessW" << std::endl;
        return info;
    }
    
    // Use common PipeGuard RAII wrapper
    PipeGuard pipe_guard(process_info.pipe, process_info.process_handle);
#else
    // CRITICAL: Use 'exec' to replace shell process with yt-dlp, reducing to 1 process total
    std::string escaped_url = url;
    size_t pos = 0;
    while ((pos = escaped_url.find("'", pos)) != std::string::npos) {
        escaped_url.replace(pos, 1, "'\\''");
        pos += 4;
    }
    cmd << " '" << escaped_url << "'";
    std::string exec_cmd = "exec " + cmd.str();
    std::string escaped_cmd = exec_cmd;
    pos = 0;
    while ((pos = escaped_cmd.find("'", pos)) != std::string::npos) {
        escaped_cmd.replace(pos, 1, "'\\''");
        pos += 4;
    }
    std::string shell_cmd = "sh -c '" + escaped_cmd + "'";
    std::cout << "[DEBUG] getVideoInfo: Using 'exec' to replace shell with yt-dlp (reduces to 1 process)" << std::endl;
    FILE* pipe = popen(shell_cmd.c_str(), "r");
    if (!pipe) {
        return info;
    }
    
    // Use common PipeGuard RAII wrapper
    PipeGuard pipe_guard(pipe);
#endif
    
    std::string json_output;
    std::string error_output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe_guard.get()) != nullptr) {
        std::string line = buffer;
        // Check if this is an error line
        if (line.find("ERROR:") != std::string::npos || 
            line.find("Read timed out") != std::string::npos ||
            line.find("Connection timed out") != std::string::npos ||
            line.find("Unable to download") != std::string::npos ||
            line.find("HTTPSConnectionPool") != std::string::npos) {
            error_output += line;
        } else if (line.find("{") != std::string::npos || 
                   (line.find("\"") != std::string::npos && line.find("title") != std::string::npos)) {
            // Likely JSON output (starts with { or contains JSON-like structure)
            json_output += line;
        } else if (line.find("WARNING:") == std::string::npos) {
            // Other output (not warnings) - might be part of JSON or other info
            // Only add if it looks like JSON
            if (line.find("{") != std::string::npos || line.find("}") != std::string::npos) {
                json_output += line;
            }
        }
    }
    
    // Close pipe and get status
    int status = -1;
#ifdef _WIN32
    // On Windows, get status from ProcessLauncher
    if (process_info.isValid()) {
        status = ProcessLauncher::closeProcess(process_info);
        pipe_guard.release();  // Prevent double close (closeProcess already closed the pipe)
    }
#else
    if (pipe_guard.get()) {
        status = pclose(pipe_guard.get());
        pipe_guard.release();
    }
#endif
    
    // If we got errors but no JSON, log the error
    if (json_output.empty() && !error_output.empty()) {
        std::cout << "[DEBUG] getVideoInfo error: " << error_output << std::endl;
    }
    
    std::cout << "[DEBUG] JSON output length: " << json_output.length() << " bytes" << std::endl;
    // if (!json_output.empty()) {
    //     std::cout << "[DEBUG] JSON received (getVideoInfo): " << json_output << std::endl;
    // }
    
    // Simple JSON parsing (extract key fields) - use JsonUtils functions
    // For a more robust solution, consider using a JSON library
    if (!json_output.empty()) {
        // Extract title
        info.title = JsonUtils::extractJsonString(json_output, "title");
        
        // Extract artist/uploader (try both "uploader" and "artist" fields)
        info.artist = JsonUtils::extractJsonString(json_output, "uploader");
        // Also try "artist" field (used by some platforms like SoundCloud)
        if (info.artist.empty()) {
            info.artist = JsonUtils::extractJsonString(json_output, "artist");
        }
        
        // Extract duration
        size_t duration_pos = json_output.find("\"duration\":");
        if (duration_pos != std::string::npos) {
            size_t start = duration_pos + 11;
            while (start < json_output.length() && (json_output[start] == ' ' || json_output[start] == '\t')) {
                start++;
            }
            size_t end = start;
            while (end < json_output.length() && json_output[end] != ',' && json_output[end] != '}' && json_output[end] != '\n') {
                end++;
            }
            if (end > start) {
                info.duration = json_output.substr(start, end - start);
            }
        }
        
        // REMOVED: bitrate extraction from JSON in getVideoInfo
        // Bitrate will be calculated from file_size and duration after conversion
        // This ensures we get the actual bitrate of the converted file, not the source stream bitrate
        
        // NOTE: We don't calculate bitrate from JSON filesize here because:
        // 1. JSON filesize is for the source stream, not the converted file
        // 2. Bitrate should be calculated from the actual converted file size after conversion
        // 3. This gives accurate bitrate of the final file (mp3, m4a, etc.)
        
        // Bitrate will remain 0 here and will be calculated later from actual converted file
        
        // Extract thumbnail URL - use JsonUtils function
        info.thumbnail_url = JsonUtils::extractThumbnailUrl(json_output);
        if (!info.thumbnail_url.empty()) {
            std::cout << "[DEBUG] Extracted thumbnail URL: " << info.thumbnail_url << std::endl;
        }
        
        // Thumbnail URL extraction is handled by JsonUtils::extractThumbnailUrl above
        // JsonUtils::extractThumbnailUrl already handles YouTube URL normalization
        
        // Build expected filename from title
        if (!info.title.empty()) {
            // Sanitize filename using ValidationUtils
            std::string safe_title = ValidationUtils::sanitizeFilename(info.title);
            
            info.filename = safe_title + "." + format;
            info.filepath = output_dir + "/" + info.filename;
        }
        
        std::cout << "[DEBUG] Parsed video info: Title=" << info.title
                  << ", Artist=" << info.artist
                  << ", Duration=" << info.duration
                  << ", Bitrate=" << info.bitrate << " kbps"
                  << ", Filename=" << info.filename << std::endl;
    } else {
        std::cout << "[DEBUG] WARNING: Empty JSON output from yt-dlp" << std::endl;
    }
    
    return info;
}

Downloader::PlaylistInfo Downloader::getPlaylistItems(
    const std::string& url,
    const std::string& proxy,
    const YtDlpSettings& settings
) {
    PlaylistInfo result;
    std::vector<PlaylistItemInfo> items;
    
    std::string ytdlp_path = findYtDlpPath();
    std::ostringstream cmd;
    
    // Escape path if needed
    if (ytdlp_path.find(' ') != std::string::npos || ytdlp_path.find('"') != std::string::npos) {
        std::string escaped_path;
        for (char c : ytdlp_path) {
            if (c == '"' || c == '\\') {
                escaped_path += '\\';
            }
            escaped_path += c;
        }
        cmd << "\"" << escaped_path << "\"";
    } else {
        cmd << ytdlp_path;
    }
    
    // Get full JSON info for each playlist item BEFORE downloading
    // Use --skip-download so yt-dlp only fetches metadata, not media
    // This gives us: playlist_title, playlist_index, fulltitle, duration, duration_string, etc.
    cmd << " --skip-download";
    cmd << " --print-json";      // Output JSON for each item
    // We no longer need a separate --print for playlist title, it is available
    // in the JSON as \"playlist_title\"
    cmd << " --no-warnings";
    
    // Proxy (support socks5, socks4, https, http formats)
    if (!proxy.empty()) {
        cmd << " --proxy \"" << proxy << "\"";
    }
    
    // YouTube now requires cookies for all requests (including metadata)
    bool is_youtube_url =
        (url.find("youtube.com") != std::string::npos) ||
        (url.find("youtu.be")   != std::string::npos);
    
    if (is_youtube_url) {
        if (settings.use_cookies_file && !settings.cookies_file_path.empty()) {
            cmd << " --cookies \"" << settings.cookies_file_path << "\"";
        } else if (settings.use_cookies_for_playlists) {
            std::string browser_for_cookies = settings.selected_browser;
            if (browser_for_cookies.empty()) {
                browser_for_cookies = findAvailableBrowser();
            }
            if (!browser_for_cookies.empty()) {
                cmd << " --cookies-from-browser " << browser_for_cookies;
            }
        }
    }
    
    cmd << " 2>&1";  // Capture stderr
    
    std::cout << "[DEBUG] Getting playlist items BEFORE download: " << cmd.str() << std::endl;
    
#ifdef _WIN32
    // On Windows, use CreateProcessW via ProcessLauncher - no escaping needed!
    std::vector<std::string> args;
    args.push_back("--skip-download");
    args.push_back("--print-json");
    args.push_back("--no-warnings");
    
    if (!proxy.empty()) {
        args.push_back("--proxy");
        args.push_back(proxy);
    }
    
    // YouTube now requires cookies for all requests (including metadata)
    if (is_youtube_url) {
        if (settings.use_cookies_file && !settings.cookies_file_path.empty()) {
            args.push_back("--cookies");
            args.push_back(settings.cookies_file_path);
        } else if (settings.use_cookies_for_playlists) {
            std::string browser_for_cookies = settings.selected_browser;
            if (browser_for_cookies.empty()) {
                browser_for_cookies = findAvailableBrowser();
            }
            if (!browser_for_cookies.empty()) {
                args.push_back("--cookies-from-browser");
                args.push_back(browser_for_cookies);
            }
        }
    }
    
    // URL - no escaping needed when using CreateProcessW!
    args.push_back(url);
    
    std::cout << "[DEBUG] getPlaylistItems: Using CreateProcessW (no cmd.exe escaping needed!)" << std::endl;
    ProcessInfo process_info = ProcessLauncher::launchProcess(ytdlp_path, args, true); // Redirect stderr
    if (!process_info.isValid()) {
        std::cout << "[DEBUG] ERROR: Failed to start yt-dlp process via CreateProcessW" << std::endl;
        return result;
    }
    
    FILE* pipe = process_info.pipe;
    PipeGuard pipe_guard(pipe, process_info.process_handle);
    ProcessInfo process_info_for_cleanup = process_info;  // Store for cleanup after reading
#else
    // CRITICAL: Use 'exec' to replace shell process with yt-dlp, reducing to 1 process total
    std::string escaped_url = url;
    size_t pos = 0;
    while ((pos = escaped_url.find("'", pos)) != std::string::npos) {
        escaped_url.replace(pos, 1, "'\\''");
        pos += 4;
    }
    cmd << " '" << escaped_url << "'";
    std::string exec_cmd = "exec " + cmd.str();
    std::string escaped_cmd = exec_cmd;
    pos = 0;
    while ((pos = escaped_cmd.find("'", pos)) != std::string::npos) {
        escaped_cmd.replace(pos, 1, "'\\''");
        pos += 4;
    }
    std::string shell_cmd = "sh -c '" + escaped_cmd + "'";
    std::cout << "[DEBUG] getPlaylistItems: Using 'exec' to replace shell with yt-dlp (reduces to 1 process)" << std::endl;
    FILE* pipe = popen(shell_cmd.c_str(), "r");
    if (!pipe) {
        std::cout << "[DEBUG] Failed to get playlist items" << std::endl;
        return result;
    }
    
    PipeGuard pipe_guard(pipe);
#endif
    
    char buffer[4096];
    std::string json_line;
    int index = 0;
    bool playlist_name_found = false;
    int last_playlist_index = -1;  // Will store __last_playlist_index value
    
    // First pass: collect all JSON lines and find __last_playlist_index
    std::vector<std::string> json_lines;
    // Read complete lines (may be longer than buffer size)
    std::string current_line;
    std::string all_output;  // Store all output for debugging
    std::string error_lines;  // Store error messages
    while (fgets(buffer, sizeof(buffer), pipe_guard.get()) != nullptr) {
        all_output += buffer;  // Store for debugging
        current_line += buffer;
        // Check if line is complete (fgets always adds \n if line ends, or we've read a complete line)
        // If buffer doesn't end with \n, the line was truncated and we need to read more
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            // Line is complete
            std::string line = current_line;
            // Remove trailing newline/carriage return
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            current_line.clear();
        
        // Log all lines for debugging (especially errors)
        if (line.find("ERROR:") != std::string::npos || line.find("WARNING:") != std::string::npos) {
            std::cout << "[DEBUG] getPlaylistItems: " << line << std::endl;
        }
        
        // Check if this is an error line
        if (line.find("ERROR:") != std::string::npos) {
            std::cout << "[DEBUG] Error getting playlist: " << line << std::endl;
            // Store error message, especially bot detection errors
            if (error_lines.empty()) {
                // Extract error message after "ERROR:"
                size_t error_pos = line.find("ERROR:");
                if (error_pos != std::string::npos) {
                    std::string error_msg = line.substr(error_pos + 6);
                    // Trim whitespace
                    while (!error_msg.empty() && (error_msg.front() == ' ' || error_msg.front() == '\t')) {
                        error_msg.erase(0, 1);
                    }
                    error_lines = error_msg;
                }
            }
            continue;
        }
        
        // Each line for this command should be a JSON object for a playlist item
        // Skip non-JSON lines just in case
        // Check if this is a valid track JSON object (not a format object or partial JSON)
        // Valid track JSON should start with { and contain "id": near the beginning
        if (line.find("{") == 0 && line.find("\"id\":") != std::string::npos) {
            // Additional check: should contain playlist-related fields to distinguish from format objects
            // Format objects don't have "playlist_index" or "playlist_title"
            if (line.find("\"playlist_index\":") != std::string::npos || 
                line.find("\"playlist_title\":") != std::string::npos ||
                line.find("\"title\":") != std::string::npos ||
                line.find("\"fulltitle\":") != std::string::npos) {
                json_lines.push_back(line);
                
                // Try to extract __last_playlist_index from this JSON line
                if (last_playlist_index < 0) {
                    size_t last_index_pos = line.find("\"__last_playlist_index\":");
                    if (last_index_pos != std::string::npos) {
                        size_t start = last_index_pos + 24;
                        while (start < line.length() && (line[start] == ' ' || line[start] == '\t')) {
                            start++;
                        }
                        size_t end = start;
                        while (end < line.length() && line[end] != ',' && line[end] != '}' && line[end] != '\n') {
                            end++;
                        }
                        if (end > start) {
                            try {
                                last_playlist_index = std::stoi(line.substr(start, end - start));
                                std::cout << "[DEBUG] Found __last_playlist_index: " << last_playlist_index << std::endl;
                            } catch (...) {
                                // ignore
                            }
                        }
                    }
                }
            }
        }
        } // End of if (len > 0 && buffer[len - 1] == '\n')
    }
    
    // Process last line if it doesn't end with newline (end of file)
    if (!current_line.empty()) {
        std::string line = current_line;
        // Remove trailing newline/carriage return if any
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
            }
        
        // Check if this is an error line
        if (line.find("ERROR:") == std::string::npos) {
            // Check if this is a valid track JSON object
            if (line.find("{") == 0 && line.find("\"id\":") != std::string::npos) {
                if (line.find("\"playlist_index\":") != std::string::npos || 
                    line.find("\"playlist_title\":") != std::string::npos ||
                    line.find("\"title\":") != std::string::npos ||
                    line.find("\"fulltitle\":") != std::string::npos) {
                    json_lines.push_back(line);
                    
                    // Try to extract __last_playlist_index from this JSON line
                    if (last_playlist_index < 0) {
                        size_t last_index_pos = line.find("\"__last_playlist_index\":");
                        if (last_index_pos != std::string::npos) {
                            size_t start = last_index_pos + 24;
                            while (start < line.length() && (line[start] == ' ' || line[start] == '\t')) {
                                start++;
                            }
                            size_t end = start;
                            while (end < line.length() && line[end] != ',' && line[end] != '}' && line[end] != '\n') {
                                end++;
                            }
                            if (end > start) {
                                try {
                                    last_playlist_index = std::stoi(line.substr(start, end - start));
                                    std::cout << "[DEBUG] Found __last_playlist_index: " << last_playlist_index << std::endl;
                                } catch (...) {
                                    // ignore
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Close pipe and get status
#ifdef _WIN32
    // On Windows, get status from ProcessLauncher
    int status = -1;
    if (process_info_for_cleanup.isValid()) {
        status = ProcessLauncher::closeProcess(process_info_for_cleanup);
        pipe_guard.release();  // Prevent double close (closeProcess already closed the pipe)
    }
    std::cout << "[DEBUG] getPlaylistItems: Process finished with status: " << status << std::endl;
    if (status != 0) {
        std::cout << "[DEBUG] getPlaylistItems: WARNING - Process exited with error code: " << status << std::endl;
        // Log first 500 chars of output for debugging
        if (!all_output.empty()) {
            std::string output_preview = all_output.substr(0, (std::min)(all_output.length(), size_t(500)));
            std::cout << "[DEBUG] getPlaylistItems: Output preview: " << output_preview << std::endl;
        } else {
            std::cout << "[DEBUG] getPlaylistItems: No output received from process" << std::endl;
        }
    }
#else
    pclose(pipe);
#endif
    
    // Pre-allocate items vector based on __last_playlist_index
    if (last_playlist_index > 0) {
        items.resize(last_playlist_index);
        std::cout << "[DEBUG] Pre-allocated items vector with size: " << last_playlist_index << std::endl;
    }
    
    // Second pass: parse all JSON lines and fill items vector
    bool is_single_file_detected = false; // Track if we detected a single file (playlist fields are null)
    
    for (const std::string& line : json_lines) {
            json_line = line;
        // std::cout << "[DEBUG] JSON received (getPlaylistItems): " << json_line << std::endl;
            
            // Try to extract title and id from JSON
            PlaylistItemInfo item;
            item.index = index;
            
            // Use JsonUtils::extractJsonString instead of local lambda
        
        // Extract playlist title (same for all items). Use first non-empty value.
        // Try "playlist_title" first, then "playlist" as fallback
        // IMPORTANT: Check if playlist fields are null - if both "playlist" and "playlist_index" are null, this is NOT a playlist
        if (!playlist_name_found) {
            // First check if playlist fields are null (indicates single file, not playlist)
            bool playlist_is_null = false;
            size_t playlist_pos = json_line.find("\"playlist\":");
            if (playlist_pos != std::string::npos) {
                size_t null_pos = json_line.find("null", playlist_pos);
                size_t next_comma = json_line.find(",", playlist_pos);
                size_t next_brace = json_line.find("}", playlist_pos);
                size_t next_field = (next_comma != std::string::npos && next_comma < next_brace) ? next_comma : next_brace;
                if (null_pos != std::string::npos && null_pos < next_field) {
                    playlist_is_null = true;
                    std::cout << "[DEBUG] Found \"playlist\": null - this is NOT a playlist" << std::endl;
                }
            }
            
            bool playlist_index_is_null = false;
            size_t playlist_index_pos = json_line.find("\"playlist_index\":");
            if (playlist_index_pos != std::string::npos) {
                size_t null_pos = json_line.find("null", playlist_index_pos);
                size_t next_comma = json_line.find(",", playlist_index_pos);
                size_t next_brace = json_line.find("}", playlist_index_pos);
                size_t next_field = (next_comma != std::string::npos && next_comma < next_brace) ? next_comma : next_brace;
                if (null_pos != std::string::npos && null_pos < next_field) {
                    playlist_index_is_null = true;
                    std::cout << "[DEBUG] Found \"playlist_index\": null - this is NOT a playlist" << std::endl;
                }
            }
            
            // If both playlist and playlist_index are null, this is definitely NOT a playlist
            if (playlist_is_null && playlist_index_is_null) {
                std::cout << "[DEBUG] Both \"playlist\" and \"playlist_index\" are null - this is a SINGLE FILE, not a playlist" << std::endl;
                // Don't try to extract playlist name, and mark as single file
                result.playlist_name = "";
                is_single_file_detected = true; // Mark that this is a single file, not a playlist
            } else {
                // Try to extract playlist name - use JsonUtils::extractJsonString
                std::string playlist_title = JsonUtils::extractJsonString(json_line, "playlist_title");
                std::cout << "[DEBUG] extractJsonString(\"playlist_title\") returned: \"" << playlist_title << "\"" << std::endl;
                if (playlist_title.empty() && !playlist_is_null) {
                    // Fallback to "playlist" field if "playlist_title" is not available
                    // But only if "playlist" is not null
                    playlist_title = JsonUtils::extractJsonString(json_line, "playlist");
                    std::cout << "[DEBUG] extractJsonString(\"playlist\") returned: \"" << playlist_title << "\"" << std::endl;
                }
                if (!playlist_title.empty() && playlist_title != "playlist_index") {
                    // Check that we didn't accidentally extract "playlist_index" as playlist name
                    result.playlist_name = playlist_title;
                    playlist_name_found = true;
                    std::cout << "[DEBUG] Got playlist name from getPlaylistItems: \"" << result.playlist_name << "\"" << std::endl;
                } else {
                    std::cout << "[DEBUG] Failed to extract playlist name from JSON. Checking if fields exist..." << std::endl;
                    bool has_playlist_title = json_line.find("\"playlist_title\":") != std::string::npos;
                    bool has_playlist = json_line.find("\"playlist\":") != std::string::npos;
                    std::cout << "[DEBUG] JSON contains \"playlist_title:\": " << has_playlist_title << ", \"playlist:\": " << has_playlist << std::endl;
                }
            }
        }
        
        // Extract thumbnail URL from first playlist item for "already_exists" status
        // This is needed because for existing files, download doesn't happen and parseJsonProgress won't be called
        // Extract thumbnail from first item (playlist_index 1 or when we haven't extracted it yet)
        if (result.thumbnail_url.empty() && !is_single_file_detected) {
            // Extract playlist_index from JSON to determine if this is the first item
            int json_playlist_index = -1;
            size_t playlist_index_pos = json_line.find("\"playlist_index\":");
            if (playlist_index_pos != std::string::npos) {
                size_t start = playlist_index_pos + 17;
                while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
                    start++;
                }
                size_t end = start;
                while (end < json_line.length() && json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n') {
                    end++;
                }
                if (end > start) {
                    try {
                        json_playlist_index = std::stoi(json_line.substr(start, end - start));
                    } catch (...) {
                        // Ignore parse errors
                    }
                }
            }
            
            // Extract thumbnail from first item (playlist_index 1, or if playlist_index is not available, from first JSON line)
            bool is_first_item = (json_playlist_index == 1) || (json_playlist_index < 0 && result.thumbnail_url.empty());
            if (is_first_item) {
                std::string thumbnail_url = JsonUtils::extractThumbnailUrl(json_line);
                if (!thumbnail_url.empty()) {
                    result.thumbnail_url = thumbnail_url;
                    std::cout << "[DEBUG] getPlaylistItems: Extracted thumbnail URL from first item (playlist_index=" << json_playlist_index << "): " << result.thumbnail_url << std::endl;
                } else {
                    std::cout << "[DEBUG] getPlaylistItems: No thumbnail URL found in first item JSON (playlist_index=" << json_playlist_index << ")" << std::endl;
                }
            }
        }
            
            // Extract title - try multiple fields for different platforms (especially SoundCloud)
            // Try "title" field first - use JsonUtils::extractJsonString
            item.title = JsonUtils::extractJsonString(json_line, "title");
            if (!item.title.empty()) {
#ifdef _WIN32
                std::string debug_msg = "[DEBUG] getPlaylistItems: Item [" + std::to_string(index) + "] title from 'title' field: \"" + item.title + "\"";
                writeConsoleUtf8(debug_msg + "\n");
#else
                std::cout << "[DEBUG] getPlaylistItems: Item [" << index << "] title from 'title' field: \"" << item.title << "\"" << std::endl;
#endif
            }
            
            // Try "fulltitle" field (used by SoundCloud and some other platforms)
            if (item.title.empty()) {
                item.title = JsonUtils::extractJsonString(json_line, "fulltitle");
                if (!item.title.empty()) {
#ifdef _WIN32
                    std::string debug_msg = "[DEBUG] getPlaylistItems: Item [" + std::to_string(index) + "] title from 'fulltitle' field: \"" + item.title + "\"";
                    writeConsoleUtf8(debug_msg + "\n");
#else
                    std::cout << "[DEBUG] getPlaylistItems: Item [" << index << "] title from 'fulltitle' field: \"" << item.title << "\"" << std::endl;
#endif
                }
            }
            
            // Try "track.title" field (SoundCloud specific)
            if (item.title.empty()) {
                size_t track_pos = json_line.find("\"track\":");
                if (track_pos != std::string::npos) {
                    // Track might be an object, try to find "title" inside it
                    size_t track_end = json_line.find("}", track_pos);
                    if (track_end != std::string::npos) {
                        std::string track_obj = json_line.substr(track_pos, track_end - track_pos + 1);
                        size_t track_title_pos = track_obj.find("\"title\":");
                        if (track_title_pos != std::string::npos) {
                            size_t start = track_obj.find('"', track_title_pos + 8) + 1;
                            size_t end = start;
                            while (end < track_obj.length()) {
                                if (track_obj[end] == '"' && (end == start || track_obj[end - 1] != '\\')) {
                                    break;
                                }
                                end++;
                            }
                            if (end > start && end < track_obj.length()) {
                                std::string value = track_obj.substr(start, end - start);
                                // Use JsonUtils::unescapeJsonString
                                item.title = JsonUtils::unescapeJsonString(value);
                                if (!item.title.empty()) {
#ifdef _WIN32
                                    std::string debug_msg = "[DEBUG] getPlaylistItems: Item [" + std::to_string(index) + "] title from 'track.title' field: \"" + item.title + "\"";
                                    writeConsoleUtf8(debug_msg + "\n");
#else
                                    std::cout << "[DEBUG] getPlaylistItems: Item [" << index << "] title from 'track.title' field: \"" << item.title << "\"" << std::endl;
#endif
                                }
                            }
                        }
                    }
                }
            }
            
            // Debug: log if title is still empty
            if (item.title.empty()) {
                std::cout << "[DEBUG] getPlaylistItems: Item [" << index << "] title is EMPTY after trying all fields" << std::endl;
                std::cout << "[DEBUG] getPlaylistItems: JSON line preview: " << json_line.substr(0, 200) << std::endl;
            }
        
        // Extract duration (seconds) and human-readable duration_string if available
        // Duration in seconds (float) -> int
        size_t duration_pos = json_line.find("\"duration\":");
        if (duration_pos != std::string::npos) {
            size_t start = duration_pos + 11;
            while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
                start++;
            }
            size_t end = start;
            while (end < json_line.length() && json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n') {
                end++;
            }
            if (end > start) {
                try {
                    double dur = std::stod(json_line.substr(start, end - start));
                    if (dur > 0) {
                        item.duration = static_cast<int>(dur + 0.5); // round to nearest second
                    }
                } catch (...) {
                    // Ignore parse errors
                }
            }
        }
        // duration_string (e.g., "4:00") - use JsonUtils::extractJsonString
        item.duration_string = JsonUtils::extractJsonString(json_line, "duration_string");
            
            // Extract id
            size_t id_pos = json_line.find("\"id\":");
            if (id_pos != std::string::npos) {
                size_t start = json_line.find('"', id_pos + 5) + 1;
                if (start == std::string::npos || start == 0) {
                    // Try numeric ID
                    start = id_pos + 5;
                    while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
                        start++;
                    }
                    size_t end = start;
                    while (end < json_line.length() && (json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n')) {
                        end++;
                    }
                    if (end > start) {
                        item.id = json_line.substr(start, end - start);
                    }
                } else {
                    size_t end = json_line.find('"', start);
                    if (end != std::string::npos && end > start) {
                        item.id = json_line.substr(start, end - start);
                    }
                }
            }
            
            // Extract url if available
            size_t url_pos = json_line.find("\"url\":");
            if (url_pos != std::string::npos) {
                size_t start = json_line.find('"', url_pos + 6) + 1;
                size_t end = json_line.find('"', start);
                if (end != std::string::npos && end > start) {
                    item.url = json_line.substr(start, end - start);
                }
            }
            
        // Determine index from playlist_index if present; fallback to sequential order
        int json_playlist_index = -1;
        size_t playlist_index_pos = json_line.find("\"playlist_index\":");
        if (playlist_index_pos != std::string::npos) {
            size_t start = playlist_index_pos + 17;
            while (start < json_line.length() && (json_line[start] == ' ' || json_line[start] == '\t')) {
                start++;
            }
            size_t end = start;
            while (end < json_line.length() && json_line[end] != ',' && json_line[end] != '}' && json_line[end] != '\n') {
                end++;
            }
            if (end > start) {
                try {
                    int idx1 = std::stoi(json_line.substr(start, end - start));
                    if (idx1 > 0) {
                        json_playlist_index = idx1 - 1; // convert to 0-based
                    }
                } catch (...) {
                    // ignore
                }
            }
        }
        
        if (json_playlist_index >= 0) {
            item.index = json_playlist_index;
        } else {
            item.index = index;
        }
        
        // Add item if we have at least title or id (to avoid empty items)
        // For single files (is_single_file_detected = true), we still add 1 item to indicate it's a single file
        // This allows app.cpp to correctly determine: items.size() = 1 = single file, items.size() > 1 = playlist
        if (!item.title.empty() || !item.id.empty()) {
                // If no title, use ID as fallback
                if (item.title.empty()) {
                item.title = "Item " + std::to_string(item.index + 1);
                }
            
            // For single files, use index 0
            if (is_single_file_detected) {
                item.index = 0;
            }
            
            // Ensure items vector is large enough to place item at its index
            // (fallback if __last_playlist_index was not found)
            if (static_cast<int>(items.size()) <= item.index) {
                items.resize(item.index + 1);
            }
            
            // Add/overwrite item in vector at its playlist_index position
            if (item.index >= 0 && item.index < static_cast<int>(items.size())) {
                items[item.index] = item;
#ifdef _WIN32
                std::string debug_msg = "[DEBUG] Playlist item [" + std::to_string(item.index) + "]: title=\"" + item.title 
                                      + "\", id=\"" + item.id + "\"";
                writeConsoleUtf8(debug_msg + "\n");
#else
                std::cout << "[DEBUG] Playlist item [" << item.index << "]: title=\"" << item.title 
                          << "\", id=\"" << item.id << "\"" << std::endl;
#endif
            } else {
                std::cout << "[DEBUG] Skipping item - invalid index: " << item.index << std::endl;
            }
        } else {
            // Only log if it's not a single file (single files are now added)
            if (!is_single_file_detected) {
                std::cout << "[DEBUG] Skipping item [" << item.index << "] - no title or id" << std::endl;
            }
        }
        
                index++;
            }
    
    // Remove empty items only if we pre-allocated based on __last_playlist_index
    // and some items were not filled (this should be rare)
    if (last_playlist_index > 0 && static_cast<int>(items.size()) == last_playlist_index) {
        // Count how many items are actually filled
        size_t filled_count = 0;
        for (const auto& item : items) {
            if (!item.title.empty() || !item.id.empty()) {
                filled_count++;
        }
    }
    
        // If we have fewer filled items than expected, remove empty ones
        if (filled_count < items.size()) {
            items.erase(
                std::remove_if(items.begin(), items.end(), 
                    [](const PlaylistItemInfo& item) {
                        return item.title.empty() && item.id.empty();
                    }),
                items.end()
            );
            
            // Re-index items sequentially after removing empty ones
            for (size_t i = 0; i < items.size(); i++) {
                items[i].index = static_cast<int>(i);
            }
            
            std::cout << "[DEBUG] Removed empty items, final size: " << items.size() << std::endl;
        }
    }
    
    // IMPORTANT: Keep items as-is to allow app.cpp to determine type:
    // - items.size() = 0: error or no items found
    // - items.size() = 1: single file
    // - items.size() > 1: playlist
    // Don't clear items here - let app.cpp decide based on count
    if (is_single_file_detected) {
        std::cout << "[DEBUG] Single file detected - returning 1 item for type determination" << std::endl;
        // Keep the single item - app.cpp will use items.size() = 1 to determine it's a single file
    } else if (items.size() <= 1) {
        std::cout << "[DEBUG] Playlist has " << items.size() << " item(s) - treating as single file" << std::endl;
        // For playlists with 1 item, clear items to indicate it's effectively a single file
        items.clear();
        result.playlist_name.clear();
    }
    
    result.items = items;
    result.error_message = error_lines;  // Store any errors encountered during parsing
    std::cout << "[DEBUG] Got " << items.size() << " playlist items BEFORE download";
    if (!result.playlist_name.empty()) {
        std::cout << ", playlist name: \"" << result.playlist_name << "\"";
    }
    if (!error_lines.empty()) {
        std::cout << " (with error: " << error_lines << ")";
    }
    std::cout << std::endl;
    
    return result;
}

std::string Downloader::getPlaylistName(
    const std::string& url,
    const std::string& proxy
) {
    std::string playlist_name;
    
    std::string ytdlp_path = findYtDlpPath();
    std::ostringstream cmd;
    
    // Escape path if needed
    if (ytdlp_path.find(' ') != std::string::npos || ytdlp_path.find('"') != std::string::npos) {
        std::string escaped_path;
        for (char c : ytdlp_path) {
            if (c == '"' || c == '\\') {
                escaped_path += '\\';
            }
            escaped_path += c;
        }
        cmd << "\"" << escaped_path << "\"";
    } else {
        cmd << ytdlp_path;
    }
    
    // Get playlist title using --flat-playlist --print
    cmd << " --flat-playlist";
    cmd << " --print \"%(playlist_title)s\"";
    cmd << " --no-warnings";
    
    // Proxy (support socks5, socks4, https, http formats)
    if (!proxy.empty()) {
        cmd << " --proxy \"" << proxy << "\"";
    }
    
    cmd << " 2>&1";  // Capture stderr
    
    std::cout << "[DEBUG] Getting playlist name: " << cmd.str() << std::endl;
    
#ifdef _WIN32
    // On Windows, use CreateProcessW via ProcessLauncher - no escaping needed!
    std::vector<std::string> args;
    args.push_back("--flat-playlist");
    args.push_back("--print");
    args.push_back("%(playlist_title)s");
    args.push_back("--no-warnings");
    
    if (!proxy.empty()) {
        args.push_back("--proxy");
        args.push_back(proxy);
    }
    
    // URL - no escaping needed when using CreateProcessW!
    args.push_back(url);
    
    std::cout << "[DEBUG] getPlaylistName: Using CreateProcessW (no cmd.exe escaping needed!)" << std::endl;
    ProcessInfo process_info = ProcessLauncher::launchProcess(ytdlp_path, args, true); // Redirect stderr
    if (!process_info.isValid()) {
        std::cout << "[DEBUG] ERROR: Failed to start yt-dlp process via CreateProcessW" << std::endl;
        return playlist_name;
    }
    
    FILE* pipe = process_info.pipe;
    PipeGuard pipe_guard(pipe, process_info.process_handle);
#else
    std::string escaped_url = url;
    size_t pos = 0;
    while ((pos = escaped_url.find("'", pos)) != std::string::npos) {
        escaped_url.replace(pos, 1, "'\\''");
        pos += 4;
    }
    cmd << " '" << escaped_url << "'";
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        std::cout << "[DEBUG] Failed to get playlist name" << std::endl;
        return playlist_name;
    }
    
    PipeGuard pipe_guard(pipe);
#endif
    
    char buffer[1024];
    // Read first non-empty line (playlist title)
    while (fgets(buffer, sizeof(buffer), pipe_guard.get()) != nullptr) {
        std::string line = buffer;
        // Remove leading/trailing whitespace
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t' || line.front() == '\n' || line.front() == '\r')) {
            line.erase(0, 1);
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (!line.empty() && line != "NA" && line.find("ERROR") == std::string::npos) {
            playlist_name = line;
            break;
        }
    }
    
#ifdef _WIN32
    // On Windows, ProcessLauncher handles cleanup via PipeGuard
#else
    if (pipe_guard.get()) {
        pclose(pipe_guard.get());
        pipe_guard.release();
    }
#endif
    
    if (!playlist_name.empty()) {
        std::cout << "[DEBUG] Got playlist name: \"" << playlist_name << "\"" << std::endl;
    } else {
        std::cout << "[DEBUG] No playlist name found (might be a single video or playlist name unavailable)" << std::endl;
    }
    
    return playlist_name;
}

