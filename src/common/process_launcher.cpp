#include "process_launcher.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <io.h>
#include <fcntl.h>

#ifdef _WIN32

std::wstring ProcessLauncher::utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.length(), NULL, 0);
    if (size_needed <= 0) return std::wstring();
    
    std::wstring wide(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.length(), &wide[0], size_needed);
    return wide;
}

std::string ProcessLauncher::wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.length(), NULL, 0, NULL, NULL);
    if (size_needed <= 0) return std::string();
    
    std::string utf8(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.length(), &utf8[0], size_needed, NULL, NULL);
    return utf8;
}

bool ProcessLauncher::createPipe(HANDLE& read_handle, HANDLE& write_handle) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    if (!CreatePipe(&read_handle, &write_handle, &sa, 0)) {
        return false;
    }
    
    // Make sure the read handle is not inherited by child processes
    if (!SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        return false;
    }
    
    return true;
}

bool ProcessLauncher::parseCommand(const std::string& command, std::string& executable, std::vector<std::string>& arguments) {
    arguments.clear();
    executable.clear();
    
    std::istringstream iss(command);
    std::string token;
    bool in_quotes = false;
    std::string current_arg;
    
    while (iss.good()) {
        char c = iss.get();
        
        if (c == '"') {
            in_quotes = !in_quotes;
            if (!in_quotes && !current_arg.empty()) {
                // End of quoted argument
                if (executable.empty()) {
                    executable = current_arg;
                } else {
                    arguments.push_back(current_arg);
                }
                current_arg.clear();
            }
        } else if (c == ' ' && !in_quotes) {
            // Space outside quotes - end of argument
            if (!current_arg.empty()) {
                if (executable.empty()) {
                    executable = current_arg;
                } else {
                    arguments.push_back(current_arg);
                }
                current_arg.clear();
            }
        } else if (c != EOF) {
            current_arg += c;
        }
    }
    
    // Add last argument if any
    if (!current_arg.empty()) {
        if (executable.empty()) {
            executable = current_arg;
        } else {
            arguments.push_back(current_arg);
        }
    }
    
    return !executable.empty();
}

ProcessInfo ProcessLauncher::launchProcess(const std::string& executable_path, const std::vector<std::string>& arguments, bool redirect_stderr) {
    ProcessInfo info;
    HANDLE stdout_read = NULL;
    HANDLE stdout_write = NULL;
    
    // Create pipe for stdout
    if (!createPipe(stdout_read, stdout_write)) {
        std::cerr << "[ERROR] ProcessLauncher: Failed to create stdout pipe" << std::endl;
        return info;  // Return invalid ProcessInfo
    }
    
    // Note: When redirect_stderr is true, we merge stderr with stdout
    // So we don't need a separate stderr pipe - stderr will go to stdout_write
    // This matches shell behavior: 2>&1 (redirect stderr to stdout)
    
    // Convert executable path to wide string
    std::wstring exe_wide = utf8ToWide(executable_path);
    if (exe_wide.empty()) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        std::cerr << "[ERROR] ProcessLauncher: Failed to convert executable path to UTF-16" << std::endl;
        return info;  // Return invalid ProcessInfo
    }
    
    // Build command line: executable + arguments
    std::wstring cmd_line = L"\"" + exe_wide + L"\"";
    for (const auto& arg : arguments) {
        std::wstring arg_wide = utf8ToWide(arg);
        // Quote argument if it contains spaces or special characters
        if (arg_wide.find(L' ') != std::wstring::npos || 
            arg_wide.find(L'\t') != std::wstring::npos ||
            arg_wide.find(L'&') != std::wstring::npos ||
            arg_wide.find(L'|') != std::wstring::npos) {
            // Escape quotes in argument and wrap in quotes
            std::wstring escaped_arg;
            for (wchar_t c : arg_wide) {
                if (c == L'"') {
                    escaped_arg += L"\\\"";
                } else {
                    escaped_arg += c;
                }
            }
            cmd_line += L" \"" + escaped_arg + L"\"";
        } else {
            cmd_line += L" " + arg_wide;
        }
    }
    
    // Create a modifiable copy of the command line (CreateProcessW may modify it)
    std::vector<wchar_t> cmd_line_buffer(cmd_line.begin(), cmd_line.end());
    cmd_line_buffer.push_back(L'\0');
    
    // Prepare startup info
    STARTUPINFOW si = {0};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = stdout_write;
    // If redirect_stderr is true, merge stderr with stdout (like 2>&1 in shell)
    // This ensures all output (including errors) goes to stdout pipe
    si.hStdError = redirect_stderr ? stdout_write : GetStdHandle(STD_ERROR_HANDLE);
    
    // Prepare process info
    PROCESS_INFORMATION pi = {0};
    
    // Create process
    // Use CREATE_NO_WINDOW to prevent console window from appearing
    // This is important for GUI applications that don't want to show console windows
    BOOL success = CreateProcessW(
        exe_wide.c_str(),           // Application name
        cmd_line_buffer.data(),    // Command line (modifiable copy)
        NULL,                       // Process security attributes
        NULL,                       // Thread security attributes
        TRUE,                       // Inherit handles
        CREATE_NO_WINDOW,           // Creation flags - don't show console window
        NULL,                       // Environment (use parent's)
        NULL,                       // Current directory (use parent's)
        &si,                        // Startup info
        &pi                         // Process info
    );
    
    // Close write handle in parent process (child has its own copy)
    CloseHandle(stdout_write);
    
    if (!success) {
        DWORD error = GetLastError();
        CloseHandle(stdout_read);
        std::cerr << "[ERROR] ProcessLauncher: CreateProcessW failed with error " << error << std::endl;
        return info;  // Return invalid ProcessInfo
    }
    
    // Close thread handle (we don't need it)
    CloseHandle(pi.hThread);
    
    // Store process handle
    info.process_handle = pi.hProcess;
    
    // Convert read handle to FILE*
    int fd = _open_osfhandle((intptr_t)stdout_read, _O_RDONLY | _O_TEXT);
    if (fd == -1) {
        CloseHandle(stdout_read);
        CloseHandle(pi.hProcess);
        std::cerr << "[ERROR] ProcessLauncher: Failed to convert handle to file descriptor" << std::endl;
        return info;  // Return invalid ProcessInfo
    }
    
    FILE* pipe = _fdopen(fd, "r");
    if (!pipe) {
        _close(fd);
        CloseHandle(stdout_read);
        CloseHandle(pi.hProcess);
        std::cerr << "[ERROR] ProcessLauncher: Failed to create FILE* from file descriptor" << std::endl;
        return info;  // Return invalid ProcessInfo
    }
    
    info.pipe = pipe;
    return info;
}

int ProcessLauncher::closeProcess(ProcessInfo& info) {
    int exit_code = -1;
    
    // Wait for process to finish and get exit code
    if (info.process_handle != INVALID_HANDLE_VALUE) {
        // Check if process is already finished (non-blocking check)
        DWORD wait_result = WaitForSingleObject(info.process_handle, 0);
        if (wait_result == WAIT_OBJECT_0) {
            // Process already completed, get exit code
            DWORD process_exit_code = 0;
            if (GetExitCodeProcess(info.process_handle, &process_exit_code)) {
                exit_code = static_cast<int>(process_exit_code);
            }
        } else if (wait_result == WAIT_TIMEOUT) {
            // Process is still running, wait for it to complete (with timeout to avoid blocking)
            wait_result = WaitForSingleObject(info.process_handle, 30000); // 30 second timeout
            if (wait_result == WAIT_OBJECT_0) {
                // Process completed, get exit code
                DWORD process_exit_code = 0;
                if (GetExitCodeProcess(info.process_handle, &process_exit_code)) {
                    exit_code = static_cast<int>(process_exit_code);
                }
            } else if (wait_result == WAIT_TIMEOUT) {
                // Process is taking too long, terminate it
                std::cerr << "[WARNING] ProcessLauncher: Process timeout, terminating..." << std::endl;
                TerminateProcess(info.process_handle, 1);
                exit_code = -1;
            } else {
                DWORD error = GetLastError();
                std::cerr << "[WARNING] ProcessLauncher: WaitForSingleObject failed with error " << error << std::endl;
            }
        } else {
            DWORD error = GetLastError();
            std::cerr << "[WARNING] ProcessLauncher: WaitForSingleObject failed with error " << error << std::endl;
        }
        
        CloseHandle(info.process_handle);
        info.process_handle = INVALID_HANDLE_VALUE;
    }
    
    // Close pipe after process has finished
    if (info.pipe) {
        fclose(info.pipe);
        info.pipe = nullptr;
    }
    
    return exit_code;
}

bool ProcessLauncher::terminateProcess(ProcessInfo& info) {
    bool terminated = false;
    
    // Terminate process immediately (for cancellation)
    if (info.process_handle != INVALID_HANDLE_VALUE) {
        // Check if process is already finished (non-blocking check)
        DWORD wait_result = WaitForSingleObject(info.process_handle, 0);
        if (wait_result == WAIT_OBJECT_0) {
            // Process already finished
            terminated = true;
            std::cout << "[DEBUG] ProcessLauncher: Process already finished" << std::endl;
        } else {
            // Process is still running, force terminate it immediately
            // CRITICAL: Use TerminateProcess for immediate termination (no graceful shutdown)
            // This ensures the process is killed quickly when user cancels download
            if (TerminateProcess(info.process_handle, 1)) {
                terminated = true;
                std::cout << "[DEBUG] ProcessLauncher: Process terminated successfully" << std::endl;
                
                // Wait briefly to ensure process is fully terminated
                WaitForSingleObject(info.process_handle, 1000); // 1 second max wait
            } else {
                DWORD error = GetLastError();
                // ERROR_ACCESS_DENIED or ERROR_INVALID_HANDLE are common if process already finished
                if (error != ERROR_ACCESS_DENIED && error != ERROR_INVALID_HANDLE) {
                    std::cerr << "[WARNING] ProcessLauncher: TerminateProcess failed with error " << error << std::endl;
                } else {
                    // Process likely already finished, consider it terminated
                    terminated = true;
                    std::cout << "[DEBUG] ProcessLauncher: Process already finished (error " << error << ")" << std::endl;
                }
            }
        }
        
        CloseHandle(info.process_handle);
        info.process_handle = INVALID_HANDLE_VALUE;
    }
    
    // Close pipe after process has been terminated
    if (info.pipe) {
        fclose(info.pipe);
        info.pipe = nullptr;
    }
    
    return terminated;
}

#endif // _WIN32

