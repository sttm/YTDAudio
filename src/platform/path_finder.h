#ifndef PATH_FINDER_H
#define PATH_FINDER_H

#include <string>

class PathFinder {
public:
    // Find yt-dlp executable
    static std::string findYtDlpPath();
    
    // Find ffmpeg executable
    static std::string findFfmpegPath();
    
private:
    // Platform-specific implementations
    static std::string findInAppBundle(const std::string& filename);
    static std::string findInPath(const std::string& filename);
    static std::string findInSystemPaths(const std::string& filename);
    
    // Helper: Check if file exists and is executable
    static bool isExecutable(const std::string& path);
};

#endif // PATH_FINDER_H

