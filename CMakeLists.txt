cmake_minimum_required(VERSION 3.16)
project(YTDAudio)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option for fast GUI-only build (skip downloading yt-dlp and copying ffmpeg)
option(FAST_BUILD "Skip downloading yt-dlp and copying ffmpeg for faster GUI-only builds" OFF)

# Paths
set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ImGui sources
set(IMGUI_SRC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdlrenderer2.cpp
)

# Platform-specific sources
set(PLATFORM_SRC "")
set(THUMBNAIL_SRC "")
if(APPLE)
    # macOS: Use .mm file for Objective-C++ code
    # Rename platform_utils.cpp to platform_utils_macos.mm for macOS
    set(PLATFORM_SRC ${SRC_DIR}/platform/platform_utils_macos.mm)
    # Use Objective-C++ implementation for thumbnail downloader on macOS
    set(THUMBNAIL_SRC ${SRC_DIR}/common/thumbnail_downloader_macos.mm)
elseif(WIN32)
    # Windows: platform_utils.cpp is regular C++
    set(PLATFORM_SRC ${SRC_DIR}/platform/platform_utils.cpp)
    set(THUMBNAIL_SRC ${SRC_DIR}/common/thumbnail_downloader.cpp)
else()
    # Linux/Unix: platform_utils.cpp is regular C++
    set(PLATFORM_SRC ${SRC_DIR}/platform/platform_utils.cpp)
    set(THUMBNAIL_SRC ${SRC_DIR}/common/thumbnail_downloader.cpp)
endif()

add_executable(ytdaudio
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/app.cpp
    ${SRC_DIR}/downloader.cpp
    ${SRC_DIR}/history/history_manager.cpp
    ${SRC_DIR}/ui/ui_renderer.cpp
    ${SRC_DIR}/download/download_manager.cpp
    ${SRC_DIR}/file/file_manager.cpp
    ${SRC_DIR}/metadata/metadata_manager.cpp
    ${SRC_DIR}/common/validation_utils.cpp
    ${SRC_DIR}/common/json_utils.cpp
    ${SRC_DIR}/common/audio_utils.cpp
    ${SRC_DIR}/common/browser_utils.cpp
    ${SRC_DIR}/common/url_parser.cpp
    ${SRC_DIR}/common/playlist_detector.cpp
    ${SRC_DIR}/common/path_utils.cpp
    ${SRC_DIR}/common/history_utils.cpp
    ${THUMBNAIL_SRC}
    ${SRC_DIR}/settings/settings.cpp
    ${SRC_DIR}/service/service_checker.cpp
    ${SRC_DIR}/window/window_manager.cpp
    ${SRC_DIR}/events/event_handler.cpp
    ${SRC_DIR}/platform/platform_detector.cpp
    ${SRC_DIR}/platform/path_finder.cpp
    ${PLATFORM_SRC}
    ${IMGUI_SRC}
)

target_include_directories(ytdaudio PRIVATE
    ${THIRD_PARTY_DIR}/imgui
    ${THIRD_PARTY_DIR}/imgui/backends
    ${SRC_DIR}
    ${SRC_DIR}/platform
    ${SRC_DIR}/history
    ${SRC_DIR}/common
    ${SRC_DIR}/ui
    ${SRC_DIR}/download
    ${SRC_DIR}/file
    ${SRC_DIR}/metadata
    ${SRC_DIR}/settings
    ${SRC_DIR}/service
    ${SRC_DIR}/window
    ${SRC_DIR}/events
    ${THIRD_PARTY_DIR}/json/single_include
)

# Find SDL2 (prefer explicitly provided universal lib)
set(SDL2_IMPORTED_TARGET "")
if(DEFINED SDL2_LIBRARY AND EXISTS "${SDL2_LIBRARY}")
    message(STATUS "Using provided SDL2_LIBRARY: ${SDL2_LIBRARY}")
    add_library(SDL2::SDL2 UNKNOWN IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION "${SDL2_LIBRARY}")
    set(SDL2_IMPORTED_TARGET "SDL2::SDL2")
    # Try common include paths; user can override with -DSDL2_INCLUDE_DIR
    if(NOT SDL2_INCLUDE_DIR)
        if(APPLE)
            find_path(SDL2_INCLUDE_DIR NAMES SDL.h PATHS /opt/homebrew/include/SDL2 /usr/local/include/SDL2)
        elseif(WIN32)
            find_path(SDL2_INCLUDE_DIR NAMES SDL.h PATHS "${THIRD_PARTY_DIR}/SDL2/include" "C:/SDL2/include" "C:/Program Files/SDL2/include")
        else()
            find_path(SDL2_INCLUDE_DIR NAMES SDL.h PATHS /usr/local/include/SDL2 /usr/include/SDL2)
        endif()
    endif()
    if(SDL2_INCLUDE_DIR)
        target_include_directories(ytdaudio PRIVATE ${SDL2_INCLUDE_DIR})
        message(STATUS "Using SDL2 includes: ${SDL2_INCLUDE_DIR}")
    endif()
else()
    # First, try to find SDL2 in third_party directory (Windows)
    if(WIN32)
        set(THIRD_PARTY_SDL2 "${THIRD_PARTY_DIR}/SDL2")
        set(THIRD_PARTY_SDL2_INCLUDE "${THIRD_PARTY_SDL2}/include")
        set(THIRD_PARTY_SDL2_LIB "${THIRD_PARTY_SDL2}/lib/x64")
        
        if(EXISTS "${THIRD_PARTY_SDL2_INCLUDE}/SDL.h" AND EXISTS "${THIRD_PARTY_SDL2_LIB}/SDL2.lib")
            message(STATUS "Found SDL2 in third_party: ${THIRD_PARTY_SDL2}")
            set(SDL2_INCLUDE_DIR "${THIRD_PARTY_SDL2_INCLUDE}")
            set(SDL2_LIBRARY "${THIRD_PARTY_SDL2_LIB}/SDL2.lib")
            set(SDL2_IMPORTED_TARGET "SDL2::SDL2")
            
            # Create imported target
            add_library(SDL2::SDL2 SHARED IMPORTED)
            set_target_properties(SDL2::SDL2 PROPERTIES
                IMPORTED_LOCATION "${THIRD_PARTY_SDL2_LIB}/SDL2.dll"
                IMPORTED_IMPLIB "${THIRD_PARTY_SDL2_LIB}/SDL2.lib"
                INTERFACE_INCLUDE_DIRECTORIES "${THIRD_PARTY_SDL2_INCLUDE}"
            )
            
            # Also link SDL2main for Windows
            if(EXISTS "${THIRD_PARTY_SDL2_LIB}/SDL2main.lib")
                add_library(SDL2::SDL2main STATIC IMPORTED)
                set_target_properties(SDL2::SDL2main PROPERTIES
                    IMPORTED_LOCATION "${THIRD_PARTY_SDL2_LIB}/SDL2main.lib"
                )
            endif()
            
            target_include_directories(ytdaudio PRIVATE ${SDL2_INCLUDE_DIR})
        endif()
    endif()
    
    # Fallback to auto-detection if not found in third_party
    if(NOT SDL2_IMPORTED_TARGET)
        find_package(SDL2 QUIET)
        if(NOT SDL2_FOUND)
            if(APPLE)
                find_path(SDL2_INCLUDE_DIR NAMES SDL.h PATHS /opt/homebrew/include/SDL2 /usr/local/include/SDL2)
                find_library(SDL2_LIB SDL2 PATHS /opt/homebrew/lib /usr/local/lib)
            elseif(WIN32)
                # Try common Windows paths
                find_path(SDL2_INCLUDE_DIR NAMES SDL.h 
                    PATHS 
                    "C:/SDL2/include"
                    "C:/Program Files/SDL2/include"
                    "C:/Program Files (x86)/SDL2/include"
                )
                find_library(SDL2_LIB SDL2 
                    PATHS 
                    "C:/SDL2/lib/x64"
                    "C:/SDL2/lib"
                    "C:/Program Files/SDL2/lib/x64"
                    "C:/Program Files/SDL2/lib"
                    "C:/Program Files (x86)/SDL2/lib/x64"
                    "C:/Program Files (x86)/SDL2/lib"
                )
            else()
                find_path(SDL2_INCLUDE_DIR NAMES SDL.h PATHS /usr/local/include/SDL2 /usr/include/SDL2)
                find_library(SDL2_LIB SDL2 PATHS /usr/local/lib /usr/lib)
            endif()
            
            if(SDL2_INCLUDE_DIR AND SDL2_LIB)
                target_include_directories(ytdaudio PRIVATE ${SDL2_INCLUDE_DIR})
                set(SDL2_LIBRARY ${SDL2_LIB})
                message(STATUS "Found SDL2 manually: ${SDL2_LIB}")
            endif()
        else()
            if(TARGET SDL2::SDL2)
                set(SDL2_IMPORTED_TARGET "SDL2::SDL2")
                get_target_property(SDL2_LIB_PATH SDL2::SDL2 LOCATION)
                if(NOT SDL2_LIB_PATH)
                    get_target_property(SDL2_LIB_PATH SDL2::SDL2 IMPORTED_LOCATION)
                endif()
                if(SDL2_LIB_PATH)
                    set(SDL2_LIBRARY ${SDL2_LIB_PATH})
                endif()
            endif()
        endif()
    endif()
endif()

# Link SDL2
if(SDL2_IMPORTED_TARGET)
    target_link_libraries(ytdaudio ${SDL2_IMPORTED_TARGET})
    if(TARGET SDL2::SDL2main)
        target_link_libraries(ytdaudio SDL2::SDL2main)
    endif()
elseif(SDL2_LIBRARY)
    target_link_libraries(ytdaudio ${SDL2_LIBRARY})
else()
    if(APPLE)
        message(FATAL_ERROR "SDL2 not found. Set SDL2_LIBRARY to your universal libSDL2.dylib")
    elseif(WIN32)
        message(FATAL_ERROR "SDL2 not found. Please ensure SDL2 is in third_party/SDL2 or set SDL2_LIBRARY")
    else()
        message(FATAL_ERROR "SDL2 not found. Please install SDL2 development libraries")
    endif()
endif()

# macOS specific
if(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(COCOA_FRAMEWORK Cocoa)
    if(FOUNDATION_FRAMEWORK)
        target_link_libraries(ytdaudio ${FOUNDATION_FRAMEWORK})
    endif()
    if(COCOA_FRAMEWORK)
        target_link_libraries(ytdaudio ${COCOA_FRAMEWORK})
    endif()
    
    # Build as app bundle
    set_target_properties(ytdaudio PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "YTDAudio"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.ytdaudio.app"
    )
    
    # Set minimum macOS version
    set_target_properties(ytdaudio PROPERTIES
        MACOSX_DEPLOYMENT_TARGET "10.15"
    )
    
    # Fix Info.plist after build (CMake doesn't always set CFBundleIdentifier correctly)
    add_custom_command(TARGET ytdaudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        "$<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Info.plist"
        COMMENT "Copying Info.plist with CFBundleIdentifier"
    )
    
    # Download/copy yt-dlp to bundle Resources (skip if FAST_BUILD is enabled)
    if(NOT FAST_BUILD)
        set(DOWNLOAD_YTDLP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/download_ytdlp.sh")
        
        add_custom_command(TARGET ytdaudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Resources
            COMMAND bash "${DOWNLOAD_YTDLP_SCRIPT}"
            COMMENT "Downloading/copying yt-dlp to app bundle"
        )
    else()
        message(STATUS "FAST_BUILD enabled: skipping yt-dlp download")
    endif()

    # Copy UI font into bundle Resources
    set(APP_FONT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/misc/fonts/Roboto-Light.ttf")
    if(EXISTS "${APP_FONT_SOURCE}")
        add_custom_command(TARGET ytdaudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Resources
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${APP_FONT_SOURCE}"
            $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Resources/Roboto-Light.ttf
            COMMENT "Copying Roboto-Light.ttf to app bundle"
        )
    else()
        message(WARNING "Roboto-Light.ttf not found at ${APP_FONT_SOURCE}")
    endif()
    
    # Try to find and copy ffmpeg if available (skip if FAST_BUILD is enabled)
    if(NOT FAST_BUILD)
        # First, try to find ffmpeg in third_party directory
        set(THIRD_PARTY_FFMPEG "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg")
        if(EXISTS "${THIRD_PARTY_FFMPEG}")
            message(STATUS "Found ffmpeg in third_party: ${THIRD_PARTY_FFMPEG}")
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${THIRD_PARTY_FFMPEG}"
                $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Resources/ffmpeg
                COMMAND chmod +x $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Resources/ffmpeg
                COMMENT "Copying ffmpeg from third_party to app bundle"
            )
        else()
            # Try to find system ffmpeg
            find_program(FFMPEG_EXECUTABLE ffmpeg
                PATHS
                /opt/homebrew/bin
                /usr/local/bin
                /usr/bin
            )
            
            if(FFMPEG_EXECUTABLE)
                message(STATUS "Found system ffmpeg: ${FFMPEG_EXECUTABLE}")
                add_custom_command(TARGET ytdaudio POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${FFMPEG_EXECUTABLE}"
                    $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Resources/ffmpeg
                    COMMAND chmod +x $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Resources/ffmpeg
                    COMMENT "Copying ffmpeg to app bundle"
                )
            else()
                message(WARNING "ffmpeg not found. Application will try to use system ffmpeg.")
            endif()
        endif()
    else()
        message(STATUS "FAST_BUILD enabled: skipping ffmpeg copy")
    endif()
    
    # Copy SDL2 framework to bundle for standalone distribution
    if(SDL2_LIBRARY)
        get_filename_component(SDL2_LIB_DIR "${SDL2_LIBRARY}" DIRECTORY)
        get_filename_component(SDL2_LIB_NAME "${SDL2_LIBRARY}" NAME)
        
        # Check if SDL2 is a framework
        if(SDL2_LIBRARY MATCHES "\\.framework$")
            # It's a framework, copy the whole framework
            get_filename_component(SDL2_FRAMEWORK_DIR "${SDL2_LIBRARY}" DIRECTORY)
            get_filename_component(SDL2_FRAMEWORK_NAME "${SDL2_LIBRARY}" NAME)
            set(SDL2_FRAMEWORK_PATH "${SDL2_FRAMEWORK_DIR}/${SDL2_FRAMEWORK_NAME}")
            
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${SDL2_FRAMEWORK_PATH}"
                $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Frameworks/${SDL2_FRAMEWORK_NAME}
                COMMENT "Copying SDL2 framework to app bundle"
            )
            
            # Fix framework install name
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND install_name_tool -id
                "@executable_path/../Frameworks/${SDL2_FRAMEWORK_NAME}/Versions/A/${SDL2_FRAMEWORK_NAME}"
                $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Frameworks/${SDL2_FRAMEWORK_NAME}/Versions/A/${SDL2_FRAMEWORK_NAME}
                COMMENT "Fixing SDL2 framework install name"
            )
            
            # Fix app's reference to SDL2
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND install_name_tool -change
                "${SDL2_FRAMEWORK_PATH}/Versions/A/${SDL2_FRAMEWORK_NAME}"
                "@executable_path/../Frameworks/${SDL2_FRAMEWORK_NAME}/Versions/A/${SDL2_FRAMEWORK_NAME}"
                $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/MacOS/ytdaudio
                COMMENT "Fixing app's reference to SDL2 framework"
            )
        else()
            # It's a dylib, copy to Frameworks
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Frameworks
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_LIBRARY}"
                $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Frameworks/${SDL2_LIB_NAME}
                COMMENT "Copying SDL2 library to app bundle"
            )
            
            # Fix dylib install name
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND install_name_tool -id
                "@executable_path/../Frameworks/${SDL2_LIB_NAME}"
                $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/Frameworks/${SDL2_LIB_NAME}
                COMMENT "Fixing SDL2 dylib install name"
            )
            
            # Fix app's reference to SDL2
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND install_name_tool -change
                "${SDL2_LIBRARY}"
                "@executable_path/../Frameworks/${SDL2_LIB_NAME}"
                $<TARGET_BUNDLE_DIR:ytdaudio>/Contents/MacOS/ytdaudio
                COMMENT "Fixing app's reference to SDL2 dylib"
            )
        endif()
        
        message(STATUS "SDL2 will be bundled with the app")
    else()
        message(WARNING "SDL2 library path not found - app may require system SDL2")
    endif()
endif()

# Windows specific
if(WIN32)
    target_link_libraries(ytdaudio
        winmm
        setupapi
        version
        imm32
        advapi32
        shell32
        user32
        gdi32
    )
    
    # Copy SDL2.dll from third_party to build directory
    set(THIRD_PARTY_SDL2_DLL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2/lib/x64/SDL2.dll")
    if(EXISTS "${THIRD_PARTY_SDL2_DLL}")
        message(STATUS "Found SDL2.dll in third_party: ${THIRD_PARTY_SDL2_DLL}")
        add_custom_command(TARGET ytdaudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${THIRD_PARTY_SDL2_DLL}"
            $<TARGET_FILE_DIR:ytdaudio>/SDL2.dll
            COMMENT "Copying SDL2.dll from third_party to build directory"
        )
    else()
        message(WARNING "SDL2.dll not found in third_party. Application may require system SDL2.")
    endif()
    
    # Copy Roboto-Light.ttf font to build directory
    set(APP_FONT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/misc/fonts/Roboto-Light.ttf")
    if(EXISTS "${APP_FONT_SOURCE}")
        message(STATUS "Found Roboto-Light.ttf: ${APP_FONT_SOURCE}")
        add_custom_command(TARGET ytdaudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${APP_FONT_SOURCE}"
            $<TARGET_FILE_DIR:ytdaudio>/Roboto-Light.ttf
            COMMENT "Copying Roboto-Light.ttf to build directory"
        )
    else()
        message(WARNING "Roboto-Light.ttf not found at ${APP_FONT_SOURCE}")
    endif()
    
    # Copy yt-dlp.exe and ffmpeg.exe from third_party to build directory (skip if FAST_BUILD is enabled)
    if(NOT FAST_BUILD)
        # Copy yt-dlp.exe from third_party
        set(THIRD_PARTY_YTDLP "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yt-dlp.exe")
        if(EXISTS "${THIRD_PARTY_YTDLP}")
            message(STATUS "Found yt-dlp.exe in third_party: ${THIRD_PARTY_YTDLP}")
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${THIRD_PARTY_YTDLP}"
                $<TARGET_FILE_DIR:ytdaudio>/yt-dlp.exe
                COMMENT "Copying yt-dlp.exe from third_party to build directory"
            )
        else()
            message(WARNING "yt-dlp.exe not found in third_party. Application will try to use system yt-dlp.")
        endif()
        
        # Copy ffmpeg.exe from third_party
        set(THIRD_PARTY_FFMPEG "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg.exe")
        if(EXISTS "${THIRD_PARTY_FFMPEG}")
            message(STATUS "Found ffmpeg.exe in third_party: ${THIRD_PARTY_FFMPEG}")
            add_custom_command(TARGET ytdaudio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${THIRD_PARTY_FFMPEG}"
                $<TARGET_FILE_DIR:ytdaudio>/ffmpeg.exe
                COMMENT "Copying ffmpeg.exe from third_party to build directory"
            )
        else()
            message(WARNING "ffmpeg.exe not found in third_party. Application will try to use system ffmpeg.")
        endif()
    else()
        message(STATUS "FAST_BUILD enabled: skipping yt-dlp.exe and ffmpeg.exe copy")
    endif()
endif()

