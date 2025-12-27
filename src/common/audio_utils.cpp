#include "audio_utils.h"
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>

namespace AudioUtils {
    int calculateBitrate(int64_t file_size_bytes, int duration_seconds) {
        if (file_size_bytes <= 0 || duration_seconds <= 0) {
            return 0;
        }
        
        // Calculate bitrate: (file_size_bytes * 8 bits/byte) / (duration_seconds * 1000 ms/second)
        // Result is in kbps (kilobits per second)
        double calculated_bitrate = (static_cast<double>(file_size_bytes) * 8.0) / 
                                   (static_cast<double>(duration_seconds) * 1000.0);
        
        return static_cast<int>(calculated_bitrate);
    }
    
    int64_t getFileSize(const std::string& file_path) {
        if (file_path.empty()) {
            return 0;
        }
        
        struct stat st;
        if (stat(file_path.c_str(), &st) == 0 && st.st_size > 0) {
            return st.st_size;
        }
        
        return 0;
    }
    
    std::string formatFileSize(int64_t bytes) {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        int unit_idx = 0;
        double size = static_cast<double>(bytes);
        
        // Use decimal units (1000) to match macOS Finder display
        // Finder uses SI units: 1 KB = 1000 bytes, 1 MB = 1000 KB, etc.
        while (size >= 1000.0 && unit_idx < 4) {
            size /= 1000.0;
            unit_idx++;
        }
        
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f %s", size, units[unit_idx]);
        return buf;
    }
    
    std::string formatDuration(int seconds) {
        if (seconds < 0) {
            return "0:00";
        }
        
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int secs = seconds % 60;
        
        char buf[32];
        if (hours > 0) {
            snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, minutes, secs);
        } else {
            snprintf(buf, sizeof(buf), "%d:%02d", minutes, secs);
        }
        return buf;
    }
}


