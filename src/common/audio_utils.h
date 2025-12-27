#pragma once

#include <cstdint>
#include <string>

namespace AudioUtils {
    /**
     * Calculate bitrate from file size and duration
     * @param file_size_bytes File size in bytes
     * @param duration_seconds Duration in seconds
     * @return Bitrate in kbps, or 0 if calculation is not possible
     */
    int calculateBitrate(int64_t file_size_bytes, int duration_seconds);
    
    /**
     * Get file size from file path
     * @param file_path Path to the file
     * @return File size in bytes, or 0 if file doesn't exist or error occurred
     */
    int64_t getFileSize(const std::string& file_path);
    
    /**
     * Format file size in human-readable format (B, KB, MB, GB, TB)
     * Uses decimal units (1000) to match macOS Finder display
     * @param bytes File size in bytes
     * @return Formatted string (e.g., "1.23 MB")
     */
    std::string formatFileSize(int64_t bytes);
    
    /**
     * Format duration in human-readable format (MM:SS or HH:MM:SS)
     * @param seconds Duration in seconds
     * @return Formatted string (e.g., "3:45" or "1:23:45")
     */
    std::string formatDuration(int seconds);
}


