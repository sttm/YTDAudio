#ifndef WINDOWS_UTILS_H
#define WINDOWS_UTILS_H

/**
 * Cross-platform file utilities
 * 
 * Despite the name "windows_utils", this header provides cross-platform
 * implementations for common file operations. All functions work on
 * Windows, macOS, and Linux.
 * 
 * On Windows: Uses UTF-16 wide-string APIs for proper Unicode support
 * On Unix/macOS: Uses standard POSIX APIs
 * 
 * Key functions:
 *   - fileExists(path)           - Check if file exists
 *   - getFileSize(path)          - Get file size in bytes
 *   - fileExistsAndGetSize()     - Check existence and get size atomically
 *   - getFileMetadata()          - Get size and modification time
 *   - isDirectory(path)          - Check if path is a directory
 *   - isRegularFile(path)        - Check if path is a regular file
 * 
 * Windows-only functions:
 *   - utf8_to_utf16()            - Convert UTF-8 to UTF-16
 *   - writeConsoleUtf8()         - Write UTF-8 to Windows console
 *   - escapeWindowsCommand()     - Escape cmd.exe special characters
 */

#include <string>

#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>

// Convert UTF-8 string to UTF-16 (std::wstring)
inline std::wstring utf8_to_utf16(const std::string& utf8) {
    if (utf8.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring w(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], size);
    return w;
}

// Write UTF-8 string to console using WriteConsoleW (proper Unicode support)
inline void writeConsoleUtf8(const std::string& utf8_str) {
    std::wstring wstr = utf8_to_utf16(utf8_str);
    if (!wstr.empty()) {
        DWORD written = 0;
        WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE),
                     wstr.c_str(),
                     static_cast<DWORD>(wstr.size()),
                     &written,
                     nullptr);
    }
}
// Helper function to escape Windows cmd.exe command
// Handles: paths with spaces, ampersands in URLs, backslashes before quotes
// This function properly escapes commands for use with cmd /c "..."
// 
// CRITICAL: cmd.exe has complex quoting rules:
// 1. Inside "cmd /c "...", inner quotes must be doubled: " becomes ""
// 2. Backslash before quote is treated as escape, so \" ends the quoted string
// 3. To include a literal quote inside a quoted string, we need: "" (doubled quote)
// 4. But backslash-quote \" is interpreted as end of quoted string, so we need to escape backslashes
// 5. Ampersand & is treated as command separator, must be escaped with ^
//
// Solution: 
// 1. First escape backslashes before quotes: \" -> \\"
// 2. Then escape ampersands: & -> ^&
// 3. Finally double all quotes: " -> ""
inline std::string escapeWindowsCommand(const std::string& command) {
    std::string escaped = command;
    
    // Step 1: Escape backslashes before quotes FIRST (prevents path truncation)
    // Replace \" with \\" to prevent cmd.exe from treating \" as end of quoted string
    // This must be done BEFORE doubling quotes, otherwise we'll double quotes that are part of \\"
    size_t pos = 0;
    while ((pos = escaped.find("\\\"", pos)) != std::string::npos) {
        escaped.replace(pos, 2, "\\\\\"");  // \" becomes \\"
        pos += 3; // Move past \\"
    }
    
    // Step 2: Escape ALL ampersands with ^& (before we modify quotes)
    // This prevents & from being treated as command separator by cmd.exe
    pos = 0;
    while ((pos = escaped.find("&", pos)) != std::string::npos) {
        // Only escape if not already escaped
        if (pos == 0 || escaped[pos - 1] != '^') {
            escaped.insert(pos, "^");
            pos += 2; // Move past ^&
        } else {
            pos++; // Already escaped, skip
        }
    }
    
    // Step 3: Double all quotes (cmd.exe requires doubled quotes inside cmd /c "...")
    // CRITICAL FIX: For paths with spaces, we need special handling
    // When we have: -o "C:/path with spaces/%(title)s.%(ext)s"
    // After doubling: -o ""C:/path with spaces/%(title)s.%(ext)s""
    // cmd.exe may interpret the space as an argument separator
    // Solution: Ensure quotes are properly doubled, but be extra careful with paths
    // The key insight: cmd.exe needs doubled quotes, but we must ensure the entire quoted string
    // (including the path with spaces) is treated as one argument
    pos = 0;
    while ((pos = escaped.find("\"", pos)) != std::string::npos) {
        // Check if this quote is part of an escaped backslash-quote sequence (\\")
        // If so, skip it - we've already handled it
        if (pos > 0 && escaped[pos - 1] == '\\') {
            // This is part of \\", skip it
            pos++;
            continue;
        }
        // Double this quote - required by cmd.exe for cmd /c "..."
        // This should work correctly if the path is properly formatted with forward slashes
        escaped.replace(pos, 1, "\"\"");
        pos += 2;
    }
    
    return escaped;
}

// Check if file exists (cross-platform, Unicode-aware on Windows)
inline bool fileExists(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    int path_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (path_size <= 0) return false;
    std::wstring wide_path(path_size, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide_path[0], path_size);
    wide_path.resize(path_size - 1);
    
    struct _stat64i32 file_stat;
    return (_wstat(wide_path.c_str(), &file_stat) == 0);
#else
    struct stat file_stat;
    return (stat(path.c_str(), &file_stat) == 0);
#endif
}

// Get file size (cross-platform, Unicode-aware on Windows)
// Returns -1 if file doesn't exist or error occurred
inline int64_t getFileSize(const std::string& path) {
    if (path.empty()) return -1;
#ifdef _WIN32
    int path_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (path_size <= 0) return -1;
    std::wstring wide_path(path_size, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide_path[0], path_size);
    wide_path.resize(path_size - 1);
    
    struct _stat64i32 file_stat;
    if (_wstat(wide_path.c_str(), &file_stat) == 0) {
        return file_stat.st_size;
    }
    return -1;
#else
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        return file_stat.st_size;
    }
    return -1;
#endif
}

// Check if file exists and get its size (cross-platform, Unicode-aware on Windows)
// Returns true if file exists, size is stored in out_size
inline bool fileExistsAndGetSize(const std::string& path, int64_t& out_size) {
    if (path.empty()) {
        out_size = -1;
        return false;
    }
#ifdef _WIN32
    int path_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (path_size <= 0) {
        out_size = -1;
        return false;
    }
    std::wstring wide_path(path_size, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide_path[0], path_size);
    wide_path.resize(path_size - 1);
    
    struct _stat64i32 file_stat;
    if (_wstat(wide_path.c_str(), &file_stat) == 0) {
        out_size = file_stat.st_size;
        return true;
    }
    out_size = -1;
    return false;
#else
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        out_size = file_stat.st_size;
        return true;
    }
    out_size = -1;
    return false;
#endif
}
#else
// Non-Windows implementations
#include <sys/stat.h>

// Check if file exists (cross-platform)
inline bool fileExists(const std::string& path) {
    if (path.empty()) return false;
    struct stat file_stat;
    return (stat(path.c_str(), &file_stat) == 0);
}

// Get file size (cross-platform)
// Returns -1 if file doesn't exist or error occurred
inline int64_t getFileSize(const std::string& path) {
    if (path.empty()) return -1;
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        return file_stat.st_size;
    }
    return -1;
}

// Check if file exists and get its size (cross-platform)
// Returns true if file exists, size is stored in out_size
inline bool fileExistsAndGetSize(const std::string& path, int64_t& out_size) {
    if (path.empty()) {
        out_size = -1;
        return false;
    }
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        out_size = file_stat.st_size;
        return true;
    }
    out_size = -1;
    return false;
}
#endif

// Get file metadata (size and modification time) - cross-platform, Unicode-aware on Windows
// Returns true if file exists, metadata is stored in out_size and out_mtime
inline bool getFileMetadata(const std::string& path, int64_t& out_size, int64_t& out_mtime) {
    if (path.empty()) {
        out_size = -1;
        out_mtime = -1;
        return false;
    }
#ifdef _WIN32
    int path_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (path_size <= 0) {
        out_size = -1;
        out_mtime = -1;
        return false;
    }
    std::wstring wide_path(path_size, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide_path[0], path_size);
    wide_path.resize(path_size - 1);
    
    struct _stat64i32 file_stat;
    if (_wstat(wide_path.c_str(), &file_stat) == 0) {
        out_size = file_stat.st_size;
        out_mtime = file_stat.st_mtime;
        return true;
    }
    out_size = -1;
    out_mtime = -1;
    return false;
#else
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        out_size = file_stat.st_size;
        out_mtime = file_stat.st_mtime;
        return true;
    }
    out_size = -1;
    out_mtime = -1;
    return false;
#endif
}

// Check if path is a directory - cross-platform, Unicode-aware on Windows
inline bool isDirectory(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    int path_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (path_size <= 0) return false;
    std::wstring wide_path(path_size, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide_path[0], path_size);
    wide_path.resize(path_size - 1);
    
    struct _stat64i32 file_stat;
    if (_wstat(wide_path.c_str(), &file_stat) == 0) {
        return (file_stat.st_mode & S_IFDIR) != 0;
    }
    return false;
#else
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        return S_ISDIR(file_stat.st_mode);
    }
    return false;
#endif
}

// Check if path is a regular file - cross-platform, Unicode-aware on Windows
inline bool isRegularFile(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    int path_size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (path_size <= 0) return false;
    std::wstring wide_path(path_size, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide_path[0], path_size);
    wide_path.resize(path_size - 1);
    
    struct _stat64i32 file_stat;
    if (_wstat(wide_path.c_str(), &file_stat) == 0) {
        return (file_stat.st_mode & S_IFREG) != 0;
    }
    return false;
#else
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) == 0) {
        return S_ISREG(file_stat.st_mode);
    }
    return false;
#endif
}

#endif // WINDOWS_UTILS_H

