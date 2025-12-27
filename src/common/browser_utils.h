#pragma once

namespace BrowserUtils {
    /**
     * Get browser name by index
     * @param index Browser index (0-based)
     * @return Browser name string, or empty string if index is invalid
     */
    const char* getBrowserName(int index);
    
    /**
     * Get total number of available browsers for current platform
     * @return Number of browsers
     */
    int getBrowserCount();
    
    /**
     * Get browser index by name (case-insensitive)
     * @param name Browser name
     * @return Browser index, or -1 if not found
     */
    int getBrowserIndex(const char* name);
}


