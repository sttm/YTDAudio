#pragma once

#include <string>
#include <chrono>
#include <mutex>
#include <atomic>
#ifdef _WIN32
#include "../common/process_launcher.h"
#endif

class ServiceChecker {
public:
    enum ServiceStatus {
        SERVICE_UNCHECKED,   // Not checked yet
        SERVICE_CHECKING,    // Currently checking
        SERVICE_AVAILABLE,   // Service is available
        SERVICE_UNAVAILABLE  // Service is unavailable
    };
    
    ServiceChecker();
    ~ServiceChecker();
    
    // Check service availability
    // force_check: ignore cache and recheck
    // is_startup: use longer timeout for startup
    void checkAvailability(bool force_check = false, bool is_startup = false);
    
    // Get current status (thread-safe)
    ServiceStatus getStatus() const;
    
    // Get last check time
    std::chrono::steady_clock::time_point getLastCheckTime() const;
    
    // Set proxy for checks
    void setProxy(const std::string& proxy);
    
    // Set shutdown flag
    void setShuttingDown(bool shutting_down);
    
    // Force terminate any active processes (public for App::cleanup)
    void terminateActiveProcess();
    
private:
    // REMOVED: SERVICE_CHECK_CACHE_MINUTES - no longer needed, checks only on startup or button click
    
    mutable std::mutex status_mutex_;
    ServiceStatus status_;
    std::chrono::steady_clock::time_point last_check_time_;
    std::string proxy_;
    std::atomic<bool> shutting_down_;
    std::atomic<bool> check_in_progress_;  // Additional protection against duplicate checks
    std::mutex process_mutex_;
    FILE* active_pipe_;  // Track active popen pipe to terminate it on shutdown (Unix) or ProcessInfo pipe (Windows)
#ifdef _WIN32
    ProcessInfo active_process_info_;  // Track active process on Windows for proper cleanup
#endif
    
    void performCheck(bool is_startup);
};

