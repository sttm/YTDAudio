#include "window_manager.h"
#include "../common/path_utils.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <iostream>
#include <sys/stat.h>
#include <vector>
#ifdef _WIN32
#include <errno.h>
#else
#include <errno.h>
#endif
#ifdef SDL_IMAGE_FOUND
#include <SDL_image.h>
#endif

WindowManager::WindowManager()
    : window_(nullptr)
    , renderer_(nullptr)
    , width_(900)
    , height_(650)
    , sdl_initialized_(false)
    , imgui_initialized_(false)
{
}

WindowManager::~WindowManager() {
    cleanup();
}

bool WindowManager::initialize(int width, int height, const std::string& title) {
    std::cout << "[DEBUG] WindowManager::initialize: Starting initialization (size: " << width << "x" << height << ")" << std::endl;
    width_ = width;
    height_ = height;
    
    // Initialize SDL
    std::cout << "[DEBUG] WindowManager::initialize: Initializing SDL..." << std::endl;
    std::cout << "[DEBUG] WindowManager::initialize: Calling SDL_Init(SDL_INIT_VIDEO)..." << std::endl;
    int sdl_result = SDL_Init(SDL_INIT_VIDEO);
    std::cout << "[DEBUG] WindowManager::initialize: SDL_Init returned: " << sdl_result << std::endl;
    if (sdl_result < 0) {
        std::cerr << "[DEBUG] WindowManager::initialize: Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return false;
    }
    sdl_initialized_ = true;
    std::cout << "[DEBUG] WindowManager::initialize: SDL initialized successfully" << std::endl;
    
#ifdef SDL_IMAGE_FOUND
    // Initialize SDL_image
    std::cout << "[DEBUG] WindowManager::initialize: Initializing SDL_image..." << std::endl;
    int img_flags = IMG_INIT_JPG | IMG_INIT_PNG;
    int img_init_result = IMG_Init(img_flags);
    if ((img_init_result & img_flags) != img_flags) {
        std::cerr << "[DEBUG] WindowManager::initialize: Warning - SDL_image initialization incomplete. Requested: " << img_flags << ", Got: " << img_init_result << std::endl;
        std::cerr << "[DEBUG] WindowManager::initialize: IMG_GetError: " << IMG_GetError() << std::endl;
    } else {
        std::cout << "[DEBUG] WindowManager::initialize: SDL_image initialized successfully (JPG: " << ((img_init_result & IMG_INIT_JPG) ? "yes" : "no") << ", PNG: " << ((img_init_result & IMG_INIT_PNG) ? "yes" : "no") << ")" << std::endl;
    }
#endif
    
    // Create window
    std::cout << "[DEBUG] WindowManager::initialize: Creating window..." << std::endl;
    window_ = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width_,
        height_,
        SDL_WINDOW_RESIZABLE
    );
    
    // Set minimum window size
    if (window_) {
        SDL_SetWindowMinimumSize(window_, 600, 400);
    }
    
    if (!window_) {
        std::cerr << "[DEBUG] WindowManager::initialize: Failed to create window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        sdl_initialized_ = false;
        return false;
    }
    std::cout << "[DEBUG] WindowManager::initialize: Window created successfully" << std::endl;
    
    // Create renderer (without VSYNC since we're limiting FPS manually)
    std::cout << "[DEBUG] WindowManager::initialize: Creating renderer..." << std::endl;
    renderer_ = SDL_CreateRenderer(
        window_,
        -1,
        SDL_RENDERER_ACCELERATED
    );
    
    if (!renderer_) {
        std::cerr << "[DEBUG] WindowManager::initialize: Failed to create renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        sdl_initialized_ = false;
        return false;
    }
    std::cout << "[DEBUG] WindowManager::initialize: Renderer created successfully" << std::endl;
    
    // Enable drag and drop
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    std::cout << "[DEBUG] WindowManager::initialize: Drag and drop enabled" << std::endl;
    
    std::cout << "[DEBUG] WindowManager::initialize: Initialization completed successfully" << std::endl;
    return true;
}

void WindowManager::cleanup() {
    if (imgui_initialized_) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        imgui_initialized_ = false;
    }
    
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    
#ifdef SDL_IMAGE_FOUND
    // Quit SDL_image
    IMG_Quit();
#endif
    
    if (sdl_initialized_) {
        SDL_Quit();
        sdl_initialized_ = false;
    }
}

void WindowManager::updateSize() {
    if (window_) {
        SDL_GetWindowSize(window_, &width_, &height_);
    }
}

bool WindowManager::setupImGui() {
    std::cout << "[DEBUG] WindowManager::setupImGui: Starting ImGui setup..." << std::endl;
    if (!window_ || !renderer_) {
        std::cerr << "[DEBUG] WindowManager::setupImGui: Window or renderer not initialized" << std::endl;
        return false;
    }
    
    // Setup ImGui
    std::cout << "[DEBUG] WindowManager::setupImGui: Creating ImGui context..." << std::endl;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    std::cout << "[DEBUG] WindowManager::setupImGui: ImGui context created" << std::endl;
    
    // Docking is optional
    #ifdef ImGuiConfigFlags_DockingEnable
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    #endif
    
    // Disable ini file to prevent saved window sizes from interfering
    io.IniFilename = nullptr;
    
    // Load custom font
    auto font_candidates = findFontCandidates();
    std::cout << "[DEBUG] WindowManager::setupImGui: Attempting to load font from " << font_candidates.size() << " candidate paths..." << std::endl;
    bool font_loaded = false;
    for (const auto& font_path : font_candidates) {
        std::cout << "[DEBUG] WindowManager::setupImGui: Trying to load font from: " << font_path << std::endl;
        if (loadFont(font_path, 16.0f)) {
            std::cout << "[DEBUG] WindowManager::setupImGui: Successfully loaded font from: " << font_path << std::endl;
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded) {
        std::cout << "[DEBUG] WindowManager::setupImGui: WARNING - Failed to load custom font, using default font" << std::endl;
    }
    
    // Setup platform/renderer bindings
    std::cout << "[DEBUG] WindowManager::setupImGui: Initializing SDL2 and renderer bindings..." << std::endl;
    ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer2_Init(renderer_);
    std::cout << "[DEBUG] WindowManager::setupImGui: Bindings initialized" << std::endl;
    
    // Setup style
    std::cout << "[DEBUG] WindowManager::setupImGui: Setting up ImGui style..." << std::endl;
    setupImGuiStyle();
    
    imgui_initialized_ = true;
    std::cout << "[DEBUG] WindowManager::setupImGui: ImGui setup completed successfully" << std::endl;
    return true;
}

void WindowManager::beginImGuiFrame() {
    if (!imgui_initialized_) return;
    
    // Update window size
    updateSize();
    
    // Get renderer size (physical pixels for Retina)
    int render_w, render_h;
    SDL_GetRendererOutputSize(renderer_, &render_w, &render_h);
    
    // Prepare ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    
    // Override DisplaySize to use renderer size (physical pixels)
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(render_w), static_cast<float>(render_h));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    
    ImGui::NewFrame();
}

void WindowManager::endImGuiFrame() {
    if (!imgui_initialized_) return;
    
    ImGui::Render();
    
    // Set mid-dark background color for renderer
    SDL_SetRenderDrawColor(renderer_, 46, 46, 51, 255);  // #2E2E33
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
}

bool WindowManager::loadFont(const std::string& font_path, float size) {
    std::cout << "[DEBUG] WindowManager::loadFont: Checking font file: " << font_path << std::endl;
    
    struct stat font_stat;
    int stat_result = stat(font_path.c_str(), &font_stat);
    if (stat_result != 0) {
#ifdef _WIN32
        int error = errno;
        std::cout << "[DEBUG] WindowManager::loadFont: stat() failed for: " << font_path << " (errno: " << error << ")" << std::endl;
#else
        std::cout << "[DEBUG] WindowManager::loadFont: stat() failed for: " << font_path << " (errno: " << errno << ")" << std::endl;
#endif
        return false;
    }
    
    std::cout << "[DEBUG] WindowManager::loadFont: Font file found! Size: " << font_stat.st_size << " bytes" << std::endl;
    std::cout << "[DEBUG] WindowManager::loadFont: Attempting to load font from: " << font_path << std::endl;
    
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontFromFileTTF(font_path.c_str(), size);
    if (font != nullptr) {
        std::cout << "[DEBUG] WindowManager::loadFont: SUCCESS - Font loaded successfully from: " << font_path << std::endl;
    } else {
        std::cout << "[DEBUG] WindowManager::loadFont: FAILED - AddFontFromFileTTF returned nullptr for: " << font_path << std::endl;
    }
    return font != nullptr;
}

void WindowManager::setupImGuiStyle() {
    setupMidDarkStyle();
    
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(12.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarSize = 16.0f;
    style.GrabMinSize = 12.0f;
}

std::vector<std::string> WindowManager::findFontCandidates() {
    std::vector<std::string> candidates;
    std::string base_path;
    
    if (char* sdl_base = SDL_GetBasePath()) {
        base_path = sdl_base;
        SDL_free(sdl_base);
    }
    
    std::cout << "[DEBUG] WindowManager::findFontCandidates: Base path from SDL_GetBasePath(): " << (base_path.empty() ? "(empty)" : base_path) << std::endl;
    
    // Use PathUtils::normalizePath for consistent path normalization across platforms
    
#ifdef __APPLE__
    if (!base_path.empty()) {
        candidates.push_back(PathUtils::normalizePath(base_path + "../Resources/Roboto-Light.ttf"));
    }
#endif
    
    if (!base_path.empty()) {
#ifdef _WIN32
        // On Windows, try res directory first (Release builds)
        candidates.push_back(PathUtils::normalizePath(base_path + "res\\Roboto-Light.ttf"));
#endif
        // Try relative to executable directory
        candidates.push_back(PathUtils::normalizePath(base_path + "../third_party/imgui/misc/fonts/Roboto-Light.ttf"));
        // Try in same directory as executable (for Windows Release build)
        candidates.push_back(PathUtils::normalizePath(base_path + "third_party/imgui/misc/fonts/Roboto-Light.ttf"));
        // Try in parent directory (for build directory structure)
        candidates.push_back(PathUtils::normalizePath(base_path + "..\\third_party\\imgui\\misc\\fonts\\Roboto-Light.ttf"));
        // Try directly in base path (if font is copied there)
        candidates.push_back(PathUtils::normalizePath(base_path + "Roboto-Light.ttf"));
        // Try in fonts subdirectory
        candidates.push_back(PathUtils::normalizePath(base_path + "fonts\\Roboto-Light.ttf"));
        // Try going up two levels from Release folder
        candidates.push_back(PathUtils::normalizePath(base_path + "..\\..\\third_party\\imgui\\misc\\fonts\\Roboto-Light.ttf"));
    }
    
    // Try relative to current working directory
    candidates.push_back(PathUtils::normalizePath("third_party/imgui/misc/fonts/Roboto-Light.ttf"));
    candidates.push_back(PathUtils::normalizePath("../third_party/imgui/misc/fonts/Roboto-Light.ttf"));
    candidates.push_back(PathUtils::normalizePath("..\\third_party\\imgui\\misc\\fonts\\Roboto-Light.ttf"));
    
    // Debug: print all candidates
    std::cout << "[DEBUG] WindowManager::findFontCandidates: Checking " << candidates.size() << " font paths:" << std::endl;
    for (size_t i = 0; i < candidates.size(); i++) {
        std::cout << "[DEBUG]   [" << i << "] " << candidates[i] << std::endl;
    }
    
    return candidates;
}

void WindowManager::setupMidDarkStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Mid-dark color scheme
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.35f, 0.37f, 1.00f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.45f, 0.47f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.60f, 0.80f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.50f, 0.70f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.60f, 0.80f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.30f, 0.32f, 0.50f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.35f, 0.35f, 0.37f, 1.00f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.40f, 0.40f, 0.42f, 1.00f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.00f);
}

