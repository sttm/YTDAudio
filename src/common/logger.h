#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Logger {

// Log levels
enum class Level {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    None = 4  // Disable all logging
};

// Current log level (can be changed at runtime)
// Set to Debug by default for development, change to Info or Warning for release
inline Level& currentLevel() {
    static Level level = Level::Debug;
    return level;
}

// Set the current log level
inline void setLevel(Level level) {
    currentLevel() = level;
}

// Check if a level should be logged
inline bool shouldLog(Level level) {
    return static_cast<int>(level) >= static_cast<int>(currentLevel());
}

// Platform-specific output
inline void output(const std::string& message) {
#ifdef _WIN32
    // On Windows, also output to debug console
    OutputDebugStringA(message.c_str());
#endif
    std::cout << message;
}

// Log message with level
inline void log(Level level, const std::string& tag, const std::string& message) {
    if (!shouldLog(level)) return;
    
    std::ostringstream oss;
    switch (level) {
        case Level::Debug:   oss << "[DEBUG] "; break;
        case Level::Info:    oss << "[INFO] "; break;
        case Level::Warning: oss << "[WARN] "; break;
        case Level::Error:   oss << "[ERROR] "; break;
        default: break;
    }
    
    if (!tag.empty()) {
        oss << tag << ": ";
    }
    oss << message << std::endl;
    
    output(oss.str());
}

// Convenience functions
inline void debug(const std::string& tag, const std::string& message) {
    log(Level::Debug, tag, message);
}

inline void info(const std::string& tag, const std::string& message) {
    log(Level::Info, tag, message);
}

inline void warn(const std::string& tag, const std::string& message) {
    log(Level::Warning, tag, message);
}

inline void error(const std::string& tag, const std::string& message) {
    log(Level::Error, tag, message);
}

// Simple macros for common use cases
// Usage: LOG_DEBUG("MyClass", "Something happened: " << value);
#define LOG_DEBUG(tag, msg) do { \
    if (Logger::shouldLog(Logger::Level::Debug)) { \
        std::ostringstream _log_oss; \
        _log_oss << msg; \
        Logger::debug(tag, _log_oss.str()); \
    } \
} while(0)

#define LOG_INFO(tag, msg) do { \
    if (Logger::shouldLog(Logger::Level::Info)) { \
        std::ostringstream _log_oss; \
        _log_oss << msg; \
        Logger::info(tag, _log_oss.str()); \
    } \
} while(0)

#define LOG_WARN(tag, msg) do { \
    if (Logger::shouldLog(Logger::Level::Warning)) { \
        std::ostringstream _log_oss; \
        _log_oss << msg; \
        Logger::warn(tag, _log_oss.str()); \
    } \
} while(0)

#define LOG_ERROR(tag, msg) do { \
    if (Logger::shouldLog(Logger::Level::Error)) { \
        std::ostringstream _log_oss; \
        _log_oss << msg; \
        Logger::error(tag, _log_oss.str()); \
    } \
} while(0)

} // namespace Logger

#endif // LOGGER_H

