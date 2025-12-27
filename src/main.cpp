#include "app.h"
#include <iostream>

#ifdef __APPLE__
#include <cstdlib>
#endif

int main(int argc, char** argv) {
#ifdef __APPLE__
    // Set environment variables for macOS GUI applications
    setenv("SDL_VIDEODRIVER", "cocoa", 0);
    // Ensure we're running as a GUI app
    setenv("NSHighResolutionCapable", "true", 0);
#endif
    
    std::cout << "[DEBUG] main: Starting YTDAudio application..." << std::endl;
    std::cout << "[DEBUG] main: Creating App instance..." << std::endl;
    App app;
    
    std::cout << "[DEBUG] main: Initializing application..." << std::endl;
    if (!app.initialize()) {
        std::cerr << "[DEBUG] main: Failed to initialize application" << std::endl;
        return 1;
    }
    
    std::cout << "[DEBUG] main: Application initialized, starting main loop..." << std::endl;
    app.run();
    
    std::cout << "[DEBUG] main: Application main loop ended, exiting..." << std::endl;
    return 0;
}












