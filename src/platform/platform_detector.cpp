#include "platform_detector.h"

#ifdef _WIN32
#define IS_WINDOWS 1
#else
#define IS_WINDOWS 0
#endif

#ifdef __APPLE__
#define IS_MACOS 1
#else
#define IS_MACOS 0
#endif

#ifdef __linux__
#define IS_LINUX 1
#else
#define IS_LINUX 0
#endif

namespace PlatformDetector {

Platform getCurrentPlatform() {
#if IS_WINDOWS
    return Platform::Windows;
#elif IS_MACOS
    return Platform::macOS;
#elif IS_LINUX
    return Platform::Linux;
#else
    return Platform::Unknown;
#endif
}

bool isWindows() {
    return getCurrentPlatform() == Platform::Windows;
}

bool isMacOS() {
    return getCurrentPlatform() == Platform::macOS;
}

bool isLinux() {
    return getCurrentPlatform() == Platform::Linux;
}

std::string getPlatformName() {
    switch (getCurrentPlatform()) {
        case Platform::Windows:
            return "Windows";
        case Platform::macOS:
            return "macOS";
        case Platform::Linux:
            return "Linux";
        default:
            return "Unknown";
    }
}

} // namespace PlatformDetector

