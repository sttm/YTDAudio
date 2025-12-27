#ifndef PLATFORM_DETECTOR_H
#define PLATFORM_DETECTOR_H

#include <string>

namespace PlatformDetector {

enum class Platform {
    Windows,
    macOS,
    Linux,
    Unknown
};

// Get current platform
Platform getCurrentPlatform();

// Platform checks
bool isWindows();
bool isMacOS();
bool isLinux();

// Get platform name as string
std::string getPlatformName();

} // namespace PlatformDetector

#endif // PLATFORM_DETECTOR_H

