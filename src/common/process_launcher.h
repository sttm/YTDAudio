#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>

// Structure to hold process information
struct ProcessInfo {
    FILE* pipe;
    HANDLE process_handle;
    
    ProcessInfo() : pipe(nullptr), process_handle(INVALID_HANDLE_VALUE) {}
    
    bool isValid() const {
        return pipe != nullptr && process_handle != INVALID_HANDLE_VALUE;
    }
};

// Helper class to launch processes on Windows using CreateProcessW
// This avoids all the cmd.exe escaping issues
class ProcessLauncher {
public:
    // Launch a process and return ProcessInfo for reading stdout/stderr
    // Returns ProcessInfo with pipe=nullptr on failure
    static ProcessInfo launchProcess(
        const std::string& executable_path,
        const std::vector<std::string>& arguments,
        bool redirect_stderr = false  // If true, stderr is merged with stdout
    );
    
    // Close the process handle and pipe
    // Returns the exit code of the process (0 = success, non-zero = error)
    // Returns -1 if process handle is invalid or error occurred
    static int closeProcess(ProcessInfo& info);
    
    // Terminate the process immediately (for cancellation)
    // Returns true if process was terminated successfully, false otherwise
    static bool terminateProcess(ProcessInfo& info);
    
    // Parse a command string into executable path and arguments
    // This is a simple parser - assumes arguments are space-separated and quoted strings are preserved
    static bool parseCommand(
        const std::string& command,
        std::string& executable,
        std::vector<std::string>& arguments
    );
    
private:
    // Convert UTF-8 string to UTF-16 (wide string)
    static std::wstring utf8ToWide(const std::string& utf8);
    
    // Convert UTF-16 string to UTF-8
    static std::string wideToUtf8(const std::wstring& wide);
    
    // Create a pipe for reading process output
    static bool createPipe(HANDLE& read_handle, HANDLE& write_handle);
};

#endif // _WIN32

