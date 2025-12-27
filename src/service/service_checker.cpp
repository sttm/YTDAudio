#include "service_checker.h"
#include "../downloader.h"
#include "../common/validation_utils.h"
#include "../platform/path_finder.h"
#ifdef _WIN32
#define NOMINMAX  // Prevent Windows min/max macros from conflicting with std::min/max
#include "../common/process_launcher.h"
#endif
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstdio>
#include <cstdlib>  // For system()
#include <thread>
#include <sstream>
#include <algorithm>
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

using json = nlohmann::json;

// Escape URL for Windows cmd.exe - escape special characters that cmd.exe interprets
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

ServiceChecker::ServiceChecker()
    : status_(SERVICE_UNCHECKED)
    , last_check_time_((std::chrono::steady_clock::time_point::min)())  // Use parentheses to avoid Windows min/max macro conflict
    , shutting_down_(false)
    , check_in_progress_(false)
    , active_pipe_(nullptr)
#ifdef _WIN32
    , active_process_info_()
#endif
{
}

ServiceChecker::~ServiceChecker() {
    shutting_down_ = true;
    terminateActiveProcess();
}

ServiceChecker::ServiceStatus ServiceChecker::getStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
}

std::chrono::steady_clock::time_point ServiceChecker::getLastCheckTime() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return last_check_time_;
}

void ServiceChecker::setProxy(const std::string& proxy) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    proxy_ = proxy;
}

void ServiceChecker::setShuttingDown(bool shutting_down) {
    shutting_down_ = shutting_down;
    if (shutting_down) {
        terminateActiveProcess();
    }
}

void ServiceChecker::checkAvailability(bool force_check, bool is_startup) {
    // CRITICAL: Don't start new checks if shutdown is in progress
    if (shutting_down_) {
        std::cout << "[DEBUG] ServiceChecker::checkAvailability: Shutdown in progress, skipping check" << std::endl;
        return;
    }
    
    // CRITICAL: Use atomic flag for additional protection against duplicate checks
    // This prevents race conditions where two threads might pass the mutex check simultaneously
    std::cout << "[DEBUG] ServiceChecker::checkAvailability: Called with force_check=" << force_check 
              << ", is_startup=" << is_startup 
              << ", check_in_progress_=" << check_in_progress_.load() << std::endl;
    
    bool expected = false;
    if (!check_in_progress_.compare_exchange_strong(expected, true)) {
        std::cout << "[DEBUG] ServiceChecker: Check already in progress, skipping duplicate call" << std::endl;
        return;
    }
    
    std::cout << "[DEBUG] ServiceChecker::checkAvailability: Atomic flag set, proceeding with check" << std::endl;
    
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        
        // Check if we're already checking
        if (status_ == SERVICE_CHECKING) {
            check_in_progress_ = false;  // Reset flag before returning
            return;
        }
        
        // OPTIMIZATION: Check only on startup or button click - no automatic periodic checks
        // If not force_check and we already have a status, use cached result (no automatic recheck)
        // This ensures checks only happen:
        // 1. On startup (is_startup=true, force_check=false) - first check
        // 2. On button click (force_check=true) - manual check
        if (!force_check) {
            // If we already have a status (from startup or previous check), don't recheck automatically
            // Only check if status is SERVICE_UNCHECKED (never checked before)
            if (status_ != SERVICE_UNCHECKED) {
                check_in_progress_ = false;  // Reset flag before returning
                return;  // Use cached result, no automatic recheck
            }
        } else {
            // Force check - reset cache timestamp to force new check
            last_check_time_ = (std::chrono::steady_clock::time_point::min)();  // Use parentheses to avoid Windows min/max macro conflict
        }
        
        status_ = SERVICE_CHECKING;
    }
    
    // Run check in background thread
    std::thread([this, is_startup]() {
        performCheck(is_startup);
        // NOTE: check_in_progress_ is reset inside performCheck() at the end
    }).detach();
}

void ServiceChecker::performCheck(bool is_startup) {
    try {
        if (shutting_down_) {
            std::cout << "[DEBUG] ServiceChecker: Skipping check (shutdown in progress)" << std::endl;
            return;
        }
        
        std::cout << "[DEBUG] ServiceChecker: Starting availability check (is_startup=" << (is_startup ? "true" : "false") << ")" << std::endl;
        
        // Use PathFinder for centralized path finding (which internally uses Downloader::findYtDlpPath)
        std::string ytdlp_path = PathFinder::findYtDlpPath();
        bool available = false;
        
        if (ytdlp_path.empty()) {
            std::cout << "[DEBUG] ServiceChecker: yt-dlp not found, marking service as unavailable" << std::endl;
        } else {
            std::cout << "[DEBUG] ServiceChecker: Found yt-dlp at: " << ytdlp_path << std::endl;
            
            std::string test_url = "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
            int timeout = is_startup ? 15 : 5;
            // Use -J --simulate for structured JSON output and reliable error detection
            // OPTIMIZATION: Suppress warnings and redirect stderr to reduce output size (warnings can be 600KB+)
            // We only need JSON output, warnings are not critical for availability check
            // CRITICAL: Use --no-warnings --quiet AND redirect stderr
            std::string test_cmd;
            
#ifdef _WIN32
            // Windows: quote the path and use 2>nul for stderr redirection
            test_cmd = "\"" + ytdlp_path + "\" -J --simulate --no-warnings --quiet --retries 0 --socket-timeout " + std::to_string(timeout);
#else
            // Unix: use exec to replace shell process with yt-dlp, reducing to 1 process total
            test_cmd = ytdlp_path + " -J --simulate --no-warnings --quiet --retries 0 --socket-timeout " + std::to_string(timeout);
#endif
            
            // Add proxy if configured
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                if (!proxy_.empty()) {
                    std::string normalized_proxy = ValidationUtils::normalizeProxy(proxy_);
                    test_cmd += " --proxy \"" + normalized_proxy + "\"";
                    std::cout << "[DEBUG] ServiceChecker: Using proxy: " << normalized_proxy << std::endl;
                }
            }
            
            // Build arguments for ProcessLauncher (Windows) or command string (Unix)
#ifdef _WIN32
            // On Windows, use ProcessLauncher to avoid console window
            std::vector<std::string> args;
            args.push_back("-J");
            args.push_back("--simulate");
            args.push_back("--no-warnings");
            args.push_back("--quiet");
            args.push_back("--retries");
            args.push_back("0");
            args.push_back("--socket-timeout");
            args.push_back(std::to_string(timeout));
            
            // Add proxy if configured
            {
                std::lock_guard<std::mutex> lock(status_mutex_);
                if (!proxy_.empty()) {
                    std::string normalized_proxy = ValidationUtils::normalizeProxy(proxy_);
                    args.push_back("--proxy");
                    args.push_back(normalized_proxy);
                    std::cout << "[DEBUG] ServiceChecker: Using proxy: " << normalized_proxy << std::endl;
                }
            }
            
            // Add URL (no need to escape - ProcessLauncher handles it)
            args.push_back(test_url);
            
            std::cout << "[DEBUG] ServiceChecker: Executing yt-dlp with ProcessLauncher (no console window)" << std::endl;
            ProcessInfo process_info = ProcessLauncher::launchProcess(ytdlp_path, args, true);  // redirect_stderr = true
            FILE* pipe = process_info.pipe;
            std::cout << "[DEBUG] ServiceChecker: ProcessLauncher returned, pipe=" << (pipe ? "VALID" : "NULL") << std::endl;
            
            if (pipe) {
                // CRITICAL: Track active pipe and process handle to allow termination on shutdown
                {
                    std::lock_guard<std::mutex> lock(process_mutex_);
                    active_pipe_ = pipe;
                    active_process_info_ = process_info;  // Store ProcessInfo for cleanup
                }
#else
            // Escape URL for shell (Unix)
            std::string escaped_test_url = test_url;
            // Escape single quotes for shell
            size_t pos = 0;
            while ((pos = escaped_test_url.find("'", pos)) != std::string::npos) {
                escaped_test_url.replace(pos, 1, "'\\''");
                pos += 4;
            }
            test_cmd += " '" + escaped_test_url + "'";
            test_cmd += " 2>/dev/null";
            
            // CRITICAL: Use 'exec' to replace shell process with yt-dlp, reducing to 1 process total
            // This avoids having both sh and yt-dlp processes running simultaneously
            std::string exec_cmd = "exec " + test_cmd;
            
            // Escape single quotes for shell
            std::string escaped_cmd = exec_cmd;
            pos = 0;
            while ((pos = escaped_cmd.find("'", pos)) != std::string::npos) {
                escaped_cmd.replace(pos, 1, "'\\''");
                pos += 4;
            }
            std::string final_cmd = "sh -c '" + escaped_cmd + "'";
            std::cout << "[DEBUG] ServiceChecker: Using 'exec' to replace shell with yt-dlp (reduces to 1 process)" << std::endl;
            
            std::cout << "[DEBUG] ServiceChecker: Executing command: " << test_cmd << std::endl;
            std::cout << "[DEBUG] ServiceChecker: Final command: " << final_cmd << std::endl;
            
            std::cout << "[DEBUG] ServiceChecker: About to call popen()..." << std::endl;
            FILE* pipe = popen(final_cmd.c_str(), "r");
            std::cout << "[DEBUG] ServiceChecker: popen() returned, pipe=" << (pipe ? "VALID" : "NULL") << std::endl;
            std::cout << "[DEBUG] ServiceChecker: Process created - if you see 2 processes, it's because sh hasn't exec'd yet (normal)" << std::endl;
            if (pipe) {
                // CRITICAL: Track active pipe to allow termination on shutdown
                {
                    std::lock_guard<std::mutex> lock(process_mutex_);
                    active_pipe_ = pipe;
                }
#endif
                
                std::stringstream json_output;
                char buffer[4096];
                
                // Read all output - yt-dlp may output warnings first, then JSON at the end
                // CRITICAL: Check shutting_down_ during reading to allow early exit
                while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                    if (shutting_down_) {
                        std::cout << "[DEBUG] ServiceChecker: Shutdown detected during output reading, aborting check" << std::endl;
                        // CRITICAL: Close pipe immediately to terminate yt-dlp process
#ifdef _WIN32
                        {
                            std::lock_guard<std::mutex> lock(process_mutex_);
                            ProcessInfo info_to_close = active_process_info_;
                            active_pipe_ = nullptr;
                            active_process_info_ = ProcessInfo();  // Reset to invalid
                            ProcessLauncher::closeProcess(info_to_close);
                        }
#else
                        // pclose() will send SIGTERM to the process group
                        pclose(pipe);
                        {
                            std::lock_guard<std::mutex> lock(process_mutex_);
                            active_pipe_ = nullptr;
                        }
#endif
                        std::cout << "[DEBUG] ServiceChecker: Process terminated, exiting check" << std::endl;
                        return; // Exit early if shutting down
                    }
                    json_output << buffer;
                }
                // CRITICAL: Check shutting_down_ before processing output
                if (shutting_down_) {
                    std::cout << "[DEBUG] ServiceChecker: Shutdown detected after reading, aborting check" << std::endl;
#ifdef _WIN32
                    {
                        std::lock_guard<std::mutex> lock(process_mutex_);
                        ProcessInfo info_to_close = active_process_info_;
                        active_pipe_ = nullptr;
                        active_process_info_ = ProcessInfo();  // Reset to invalid
                        ProcessLauncher::closeProcess(info_to_close);
                    }
#else
                    pclose(pipe);
                    {
                        std::lock_guard<std::mutex> lock(process_mutex_);
                        active_pipe_ = nullptr;
                    }
#endif
                    return;
                }
                // CRITICAL: Close pipe and wait for process to terminate
                std::cout << "[DEBUG] ServiceChecker: Closing pipe and waiting for process termination..." << std::endl;
                std::cout << "[DEBUG] ServiceChecker: Before close - process should still be running" << std::endl;
#ifdef _WIN32
                // On Windows, use ProcessLauncher::closeProcess
                {
                    std::lock_guard<std::mutex> lock(process_mutex_);
                    ProcessInfo info_to_close = active_process_info_;
                    active_pipe_ = nullptr;
                    active_process_info_ = ProcessInfo();  // Reset to invalid
                    ProcessLauncher::closeProcess(info_to_close);
                }
                std::cout << "[DEBUG] ServiceChecker: ProcessLauncher::closeProcess completed" << std::endl;
#else
                int close_status = pclose(pipe);
                std::cout << "[DEBUG] ServiceChecker: pclose() returned with status: " << close_status << std::endl;
                {
                    std::lock_guard<std::mutex> lock(process_mutex_);
                    active_pipe_ = nullptr;
                }
#endif
                std::cout << "[DEBUG] ServiceChecker: After close - process should be terminated" << std::endl;
                
                std::string full_output = json_output.str();
                std::cout << "[DEBUG] ServiceChecker: Received output length: " << full_output.length() << " bytes" << std::endl;
                
                // CRITICAL: Extract JSON from the END of output, not the beginning
                // yt-dlp outputs warnings first, then JSON at the end
                // Look for the last complete JSON object in the output
                std::string json_str;
                size_t json_start = std::string::npos;
                
                // Try to find JSON object starting from the end
                // Look for patterns like {"id": or {"title": which are typical yt-dlp JSON starts
                size_t search_pos = full_output.length();
                
                while (search_pos > 0) {
                    search_pos = full_output.rfind("{\"id\"", search_pos - 1);
                    if (search_pos != std::string::npos) {
                        json_start = search_pos;
                        break;
                    }
                    if (search_pos == std::string::npos) break;
                }
                
                if (json_start == std::string::npos) {
                    // Try other patterns
                    search_pos = full_output.length();
                    while (search_pos > 0) {
                        search_pos = full_output.rfind("{\"title\"", search_pos - 1);
                        if (search_pos != std::string::npos) {
                            json_start = search_pos;
                            break;
                        }
                        if (search_pos == std::string::npos) break;
                    }
                }
                
                if (json_start == std::string::npos) {
                    // Fallback: look for last {" in entire output
                    search_pos = full_output.length();
                    while (search_pos > 0) {
                        search_pos = full_output.rfind("{\"", search_pos - 1);
                        if (search_pos != std::string::npos) {
                            json_start = search_pos;
                            break;
                        }
                        if (search_pos == std::string::npos) break;
                    }
                }
                
                if (json_start != std::string::npos) {
                    // Extract from json_start to the end
                    json_str = full_output.substr(json_start);
                    std::cout << "[DEBUG] ServiceChecker: Found JSON start at position " << json_start 
                              << ", extracted " << json_str.length() << " bytes" << std::endl;
                    
                    // Find the last } that completes the JSON object
                    // But if JSON was truncated, we need to find the last complete } before truncation
                    size_t last_brace = json_str.rfind('}');
                    if (last_brace != std::string::npos && last_brace > 100) {
                        // Verify it's a complete JSON object by checking brace balance
                        int brace_count = 0;
                        bool in_string = false;
                        bool escaped = false;
                        size_t check_end = last_brace;
                        
                        // Check brace balance up to the last }
                        for (size_t i = 0; i <= check_end; i++) {
                            if (!escaped && json_str[i] == '"') {
                                in_string = !in_string;
                            }
                            if (!in_string) {
                                if (json_str[i] == '{') brace_count++;
                                if (json_str[i] == '}') brace_count--;
                            }
                            escaped = (!escaped && json_str[i] == '\\');
                        }
                        
                        if (brace_count == 0) {
                            // Complete JSON object found
                            json_str = json_str.substr(0, last_brace + 1);
                            std::cout << "[DEBUG] ServiceChecker: Extracted complete JSON from position " << json_start 
                                      << " to " << (json_start + json_str.length()) << " (length: " << json_str.length() << " bytes)" << std::endl;
                        } else {
                            // JSON is incomplete - try to find a complete JSON object by searching backwards
                            std::cout << "[DEBUG] ServiceChecker: JSON appears incomplete (brace balance: " << brace_count 
                                      << "), searching for complete JSON..." << std::endl;
                            
                            // Search backwards for a } that balances to 0
                            bool found_complete = false;
                            for (size_t try_brace = last_brace; try_brace > 100 && try_brace > last_brace - 10000; try_brace--) {
                                brace_count = 0;
                                in_string = false;
                                escaped = false;
                                
                                for (size_t i = 0; i <= try_brace; i++) {
                                    if (!escaped && json_str[i] == '"') {
                                        in_string = !in_string;
                                    }
                                    if (!in_string) {
                                        if (json_str[i] == '{') brace_count++;
                                        if (json_str[i] == '}') brace_count--;
                                    }
                                    escaped = (!escaped && json_str[i] == '\\');
                                }
                                
                                if (brace_count == 0) {
                                    json_str = json_str.substr(0, try_brace + 1);
                                    std::cout << "[DEBUG] ServiceChecker: Found complete JSON at brace position " << try_brace 
                                              << " (length: " << json_str.length() << " bytes)" << std::endl;
                                    found_complete = true;
                                    break;
                                }
                            }
                            
                            if (!found_complete) {
                                std::cout << "[DEBUG] ServiceChecker: Could not find complete JSON object, marking as unavailable" << std::endl;
                                json_str.clear();
                            }
                        }
                    } else {
                        std::cout << "[DEBUG] ServiceChecker: Found JSON start but no closing brace found" << std::endl;
                        json_str.clear();
                    }
                } else {
                    std::cout << "[DEBUG] ServiceChecker: Could not find JSON start pattern in output" << std::endl;
                    json_str.clear();
                }
                
                // Check if we successfully extracted JSON
                if (json_str.empty()) {
                    std::cout << "[DEBUG] ServiceChecker: Could not extract JSON from output" << std::endl;
                    available = false;
                } else {
                    // Trim whitespace from beginning (should already be clean, but just in case)
                    size_t first_non_ws = json_str.find_first_not_of(" \t\n\r");
                    if (first_non_ws != std::string::npos && first_non_ws > 0) {
                        json_str = json_str.substr(first_non_ws);
                    }
                    
                    // Verify it looks like valid JSON
                    if (json_str.empty() || (json_str[0] != '{' && json_str[0] != '[')) {
                        std::cout << "[DEBUG] ServiceChecker: Extracted string doesn't look like JSON (starts with: " 
                                  << (json_str.length() > 50 ? json_str.substr(0, 50) : json_str) << ")" << std::endl;
                        available = false;
                    }
                }
                
                // Try to parse JSON
                if (!json_str.empty() && (json_str[0] == '{' || json_str[0] == '[')) {
                    try {
                        json j = json::parse(json_str);
                        std::cout << "[DEBUG] ServiceChecker: Successfully parsed JSON" << std::endl;
                        
                        // Check if there's an error field
                        if (j.contains("error")) {
                            std::cout << "[DEBUG] ServiceChecker: JSON contains error field, service unavailable" << std::endl;
                            available = false;
                        } else if (j.contains("title") || j.contains("id")) {
                            std::string title = j.contains("title") ? j["title"].get<std::string>() : "";
                            std::string id = j.contains("id") ? j["id"].get<std::string>() : "";
                            std::cout << "[DEBUG] ServiceChecker: JSON contains valid data (title=" << (title.empty() ? "N/A" : title.substr(0, 50)) 
                                      << ", id=" << (id.empty() ? "N/A" : id) << "), service available" << std::endl;
                            available = true;
                        } else {
                            std::cout << "[DEBUG] ServiceChecker: JSON parsed but missing expected fields (title/id), treating as unavailable" << std::endl;
                            available = false;
                        }
                    } catch (const json::parse_error& e) {
                        std::cout << "[DEBUG] ServiceChecker: JSON parsing failed: " << e.what() << std::endl;
                        // JSON parsing failed - check if output contains error messages
                        std::string output_lower = json_str;
                        std::transform(output_lower.begin(), output_lower.end(), output_lower.begin(), ::tolower);
                        
                        if (output_lower.find("error") != std::string::npos ||
                            output_lower.find("unable") != std::string::npos ||
                            output_lower.find("http error") != std::string::npos ||
                            output_lower.find("timed out") != std::string::npos ||
                            output_lower.find("403") != std::string::npos ||
                            output_lower.find("429") != std::string::npos) {
                            std::cout << "[DEBUG] ServiceChecker: Output contains error indicators, service unavailable" << std::endl;
                            available = false;
                        } else {
                            std::cout << "[DEBUG] ServiceChecker: Couldn't parse JSON and no obvious errors found, marking as unavailable (conservative)" << std::endl;
                            available = false;
                        }
                    }
                } else {
                    std::cout << "[DEBUG] ServiceChecker: Output doesn't look like JSON (first 200 chars: " 
                              << json_str.substr(0, 200) << "), marking as unavailable" << std::endl;
                    available = false;
                }
            } else {
                std::cout << "[DEBUG] ServiceChecker: Failed to open pipe for command execution" << std::endl;
            }
        }
        
        // Only update status if not shutting down
        if (!shutting_down_) {
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            status_ = available ? SERVICE_AVAILABLE : SERVICE_UNAVAILABLE;
            last_check_time_ = std::chrono::steady_clock::now();
        }
        
            std::cout << "[DEBUG] ServiceChecker: Check completed, service status: " 
                      << (available ? "AVAILABLE" : "UNAVAILABLE") << std::endl;
        } else {
            std::cout << "[DEBUG] ServiceChecker: Check interrupted by shutdown, not updating status" << std::endl;
        }
        
        // Reset check_in_progress_ flag when check completes (always, even on shutdown)
        check_in_progress_ = false;
    } catch (const std::exception& e) {
        std::cout << "[DEBUG] ServiceChecker: Exception during check: " << e.what() << std::endl;
        if (!shutting_down_) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = SERVICE_UNAVAILABLE;
        last_check_time_ = std::chrono::steady_clock::now();
        }
        check_in_progress_ = false;  // Always reset flag on exception
    } catch (...) {
        std::cout << "[DEBUG] ServiceChecker: Unknown exception during check, marking as unavailable" << std::endl;
        if (!shutting_down_) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = SERVICE_UNAVAILABLE;
        last_check_time_ = std::chrono::steady_clock::now();
        }
        check_in_progress_ = false;  // Always reset flag on exception
    }
}

void ServiceChecker::terminateActiveProcess() {
    std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: Called" << std::endl;
    
#ifdef _WIN32
    ProcessInfo info_to_close;
    {
        std::lock_guard<std::mutex> lock(process_mutex_);
        if (active_pipe_) {
            info_to_close = active_process_info_;
            active_pipe_ = nullptr;
            active_process_info_ = ProcessInfo();  // Reset to invalid
            std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: Found active process, will close it" << std::endl;
        } else {
            std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: No active process found" << std::endl;
        }
    }
    
    if (info_to_close.isValid()) {
        std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: Closing process to terminate yt-dlp..." << std::endl;
        ProcessLauncher::closeProcess(info_to_close);
        std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: ProcessLauncher::closeProcess completed" << std::endl;
    }
#else
    FILE* pipe_to_close = nullptr;
    {
        std::lock_guard<std::mutex> lock(process_mutex_);
        if (active_pipe_) {
            pipe_to_close = active_pipe_;
            active_pipe_ = nullptr;  // Clear before closing to avoid race conditions
            std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: Found active pipe, will close it" << std::endl;
        } else {
            std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: No active pipe found" << std::endl;
        }
    }
    
    if (pipe_to_close) {
        std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: Closing pipe to terminate yt-dlp process..." << std::endl;
        int close_status = pclose(pipe_to_close);
        std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: pclose() returned with status: " << close_status << std::endl;
    }
#endif
    
    // CRITICAL: Also kill any remaining yt-dlp processes that might be orphaned
    // This handles the case where pclose() didn't properly terminate child processes
    #ifndef _WIN32
    // On Unix-like systems, use pkill to terminate yt-dlp processes
    // Only kill processes that match our yt-dlp path to avoid killing other instances
    std::string ytdlp_path = PathFinder::findYtDlpPath();
    if (!ytdlp_path.empty()) {
        // Extract just the executable name
        size_t last_slash = ytdlp_path.find_last_of("/\\");
        std::string ytdlp_name = (last_slash != std::string::npos) ? ytdlp_path.substr(last_slash + 1) : ytdlp_path;
        
        std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: Checking for orphaned processes matching: " << ytdlp_name << std::endl;
        
        // First, check how many processes are running (with timeout to avoid blocking)
        std::string check_cmd = "timeout 1 pgrep -f '" + ytdlp_name + ".*--simulate.*--socket-timeout' 2>/dev/null | wc -l || echo 0";
        FILE* check_pipe = popen(check_cmd.c_str(), "r");
        if (check_pipe) {
            char buffer[32];
            if (fgets(buffer, sizeof(buffer), check_pipe) != nullptr) {
                int count = atoi(buffer);
                std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: Found " << count << " orphaned yt-dlp processes" << std::endl;
                if (count > 0) {
                    // Kill processes matching our yt-dlp executable with --simulate flag (service check processes)
                    // Use -9 (SIGKILL) to force termination if process is stuck
                    // Use timeout to prevent blocking
                    std::string kill_cmd = "timeout 1 pkill -9 -f '" + ytdlp_name + ".*--simulate.*--socket-timeout' 2>/dev/null || true";
                    int result = system(kill_cmd.c_str());
                    std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: pkill returned: " << result << std::endl;
                }
            }
            pclose(check_pipe);
        }
    }
    #endif
    
    std::cout << "[DEBUG] ServiceChecker::terminateActiveProcess: Completed" << std::endl;
}



