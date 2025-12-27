#include "browser_utils.h"
#include <cstring>
#include <algorithm>

namespace BrowserUtils {
#ifdef __APPLE__
    static const char* browsers[] = {"firefox", "safari", "edge", "opera", "brave", "chrome", "chromium"};
    static const int browser_count = sizeof(browsers) / sizeof(browsers[0]);
#else
    static const char* browsers[] = {"firefox", "edge", "opera", "brave", "chrome", "chromium"};
    static const int browser_count = sizeof(browsers) / sizeof(browsers[0]);
#endif

    const char* getBrowserName(int index) {
        if (index >= 0 && index < browser_count) {
            return browsers[index];
        }
        return "";
    }
    
    int getBrowserCount() {
        return browser_count;
    }
    
    int getBrowserIndex(const char* name) {
        if (!name) {
            return -1;
        }
        
        for (int i = 0; i < browser_count; i++) {
            if (strcmp(browsers[i], name) == 0) {
                return i;
            }
        }
        
        return -1;
    }
}


