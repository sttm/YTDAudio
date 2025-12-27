# Changelog

All notable changes to YTDAudio will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-12-27

### 🎉 Initial Release

YTDAudio v1.0.0 is the first stable release of this cross-platform audio downloader!

### ✨ Features Added

#### Core Functionality
- **Multi-Platform Downloads** - Support for YouTube, SoundCloud, Spotify, TikTok, Instagram, Twitter, Vimeo, and more
- **High-Quality Audio** - Download audio in best available quality (up to 320kbps)
- **Multiple Formats** - Support for MP3, M4A, FLAC, Opus, OGG
- **Quality Selection** - Choose between Best, 320k, 256k, 192k, 128k bitrates
- **Real-Time Progress** - Live progress bars with download speed and ETA
- **Concurrent Downloads** - Support for up to 3 simultaneous downloads

#### Playlist Support
- **Playlist Detection** - Automatic detection of playlists from URLs
- **Playlist Tracking** - Visual progress for individual tracks in playlists
- **Smart Retry** - Retry failed playlist items individually
- **Separate Folders** - Optional separate folders for playlist downloads
- **Resume Support** - Resume interrupted playlist downloads

#### User Interface
- **Modern GUI** - Beautiful interface built with ImGui
- **Responsive Design** - Smooth 30 FPS rendering with adaptive frame limiting
- **Settings Panel** - Comprehensive settings with instant apply
- **History Tab** - View and manage all past downloads
- **Drag & Drop** - Drag downloaded files to other applications
- **File Operations** - Quick "Open File" and "Open Folder" buttons

#### Network & Proxy
- **Proxy Support** - HTTP and SOCKS5 proxy configuration
- **Auto-Detection** - Smart proxy URL normalization
- **Service Checks** - Automatic availability checks for download services
- **Retry Mechanism** - Automatic retry with exponential backoff

#### Metadata & Thumbnails
- **Automatic Metadata** - Extract title, artist, duration, file size
- **Thumbnail Downloads** - Download and display video/track artwork
- **Background Processing** - Asynchronous metadata fetching
- **Platform Detection** - Automatic platform/service detection

#### History Management
- **Persistent History** - JSON-based download history
- **Duplicate Prevention** - Prevent re-downloading same URLs
- **History Search** - (Coming in future release)
- **Delete Entries** - Remove items from history
- **Retry from History** - Re-download failed items

#### Advanced Configuration
- **yt-dlp Settings** - Full control over yt-dlp behavior:
  - Rate limiting (sleep intervals)
  - Cookie support (browser integration)
  - Socket timeouts
  - Fragment retries
  - Concurrent fragments
- **API Keys** - Optional Spotify, YouTube, SoundCloud API keys
- **Custom Downloads Path** - Choose download destination
- **Persistent Settings** - All settings saved between sessions

#### Platform-Specific Features
##### macOS
- **Native App Bundle** - Proper `.app` bundle with all dependencies
- **Code Signing Ready** - Prepared for notarization
- **Retina Support** - High DPI display support
- **Objective-C++ Integration** - Native macOS APIs for thumbnails
- **Bundled Tools** - Includes yt-dlp and ffmpeg in app bundle

##### Windows
- **Standalone Executable** - No installation required
- **Bundled Dependencies** - All DLLs, yt-dlp.exe, ffmpeg.exe included
- **Windows 10+ Support** - Compatible with modern Windows versions
- **Path Handling** - Proper UNC path support

### 🏗️ Architecture

#### Code Organization
- **Modular Design** - Clean separation of concerns with manager classes:
  - `App` - Main application controller
  - `DownloadManager` - Download queue and task management
  - `HistoryManager` - History persistence and caching
  - `UIRenderer` - Interface rendering and layout
  - `MetadataManager` - Metadata and thumbnail processing
  - `FileManager` - File operations and drag & drop
  - `Settings` - Configuration management
  - `ServiceChecker` - Service availability monitoring
  - `WindowManager` - Window and SDL management
  - `EventHandler` - Input event processing

#### Utilities
- **PathUtils** - Cross-platform path normalization
- **HistoryUtils** - ID generation for history items
- **AudioUtils** - File size and duration formatting
- **JsonUtils** - JSON parsing and validation
- **ValidationUtils** - URL and path validation
- **BrowserUtils** - Browser detection for cookies
- **ProcessLauncher** - Safe subprocess execution

### 🔧 Technical Details

#### Dependencies
- **SDL2** - Cross-platform window and rendering
- **ImGui** - Immediate mode GUI (docking branch)
- **yt-dlp** - Download engine
- **FFmpeg** - Audio processing
- **nlohmann/json** - JSON parsing

#### Build System
- **CMake 3.16+** - Cross-platform build configuration
- **C++17** - Modern C++ standard
- **Fast Build Mode** - Optional GUI-only builds for development

#### Performance
- **Efficient Threading** - Background workers for downloads, metadata
- **Adaptive FPS** - Smart frame rate limiting (30 FPS)
- **Memory Management** - Smart pointers and RAII patterns
- **Async Operations** - Non-blocking UI during downloads

### 📦 Distribution

#### Included Files
**macOS Package:**
- `ytdaudio.app` - Main application bundle
  - Bundled SDL2 framework
  - Bundled yt-dlp binary
  - Bundled ffmpeg binary
  - Roboto-Light font

**Windows Package:**
- `ytdaudio.exe` - Main executable
- `SDL2.dll` - SDL2 library
- `yt-dlp.exe` - Download engine
- `ffmpeg.exe` - Audio processor
- `Roboto-Light.ttf` - UI font

### 🐛 Known Issues

- Progress bar may not update smoothly for all platforms (depends on yt-dlp output format)
- Spotify requires cookie authentication for most content
- Some platforms may require proxy configuration in restricted regions
- First download after app launch includes service check delay (2-3 seconds)

### 📝 Notes

- Default download location is user's Downloads folder
- History stored in platform-specific config directory:
  - macOS: `~/Library/Application Support/ytdaudio/`
  - Windows: `%APPDATA%\ytdaudio\`
- Settings persist between sessions
- All downloads include metadata when available

---

## [Unreleased]

### Coming Soon
- yt-dlp update button in settings
- Download history search and filtering
- Batch URL import from file
- Custom keyboard shortcuts
- Dark/Light theme toggle
- Download scheduler
- Format conversion without re-download

---

### Legend
- ✨ Features - New features
- 🐛 Bug Fixes - Bug fixes
- 🔧 Changes - Changes in existing functionality
- 🗑️ Deprecated - Soon-to-be removed features
- ❌ Removed - Removed features
- 🔒 Security - Security fixes
- 📝 Documentation - Documentation changes

[1.0.0]: https://github.com/sttm/YTDAudio/releases/tag/v1.0.0
[Unreleased]: https://github.com/sttm/YTDAudio/compare/v1.0.0...HEAD


