# Contributing to YTDAudio

First off, thank you for considering contributing to YTDAudio! 🎉

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [How Can I Contribute?](#how-can-i-contribute)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Submitting Changes](#submitting-changes)
- [Reporting Bugs](#reporting-bugs)
- [Suggesting Features](#suggesting-features)

---

## Code of Conduct

This project adheres to a simple code of conduct:
- Be respectful and considerate
- Welcome newcomers
- Focus on constructive feedback
- Help maintain a positive environment

---

## How Can I Contribute?

### 🐛 Reporting Bugs

Found a bug? Help us fix it!

**Before submitting:**
1. Check [existing issues](https://github.com/sttm/YTDAudio/issues)
2. Ensure you're using the latest version
3. Verify the bug is reproducible

**What to include:**
- **OS and version** (e.g., macOS 14.1, Windows 11)
- **YTDAudio version** (e.g., 1.0.0)
- **Steps to reproduce**
- **Expected behavior**
- **Actual behavior**
- **Screenshots** (if applicable)
- **Console output** (if available)

**Example:**
```markdown
**Environment:**
- OS: macOS 14.1
- Version: YTDAudio 1.0.0

**Steps to Reproduce:**
1. Paste YouTube playlist URL
2. Click Download
3. Observe error

**Expected:** Playlist downloads
**Actual:** Error "Service unavailable"

**Console Output:**
[ERROR] Failed to fetch playlist: Connection timeout
```

### 💡 Suggesting Features

Have an idea? We'd love to hear it!

**Before suggesting:**
1. Check [existing issues](https://github.com/sttm/YTDAudio/issues)
2. Search [discussions](https://github.com/sttm/YTDAudio/discussions)

**What to include:**
- **Clear description** of the feature
- **Use case** - why is it useful?
- **Proposed implementation** (if you have ideas)
- **Alternatives considered**
- **Mockups/examples** (if applicable)

### 📝 Improving Documentation

Documentation improvements are always welcome!

**What you can do:**
- Fix typos or unclear explanations
- Add examples
- Improve formatting
- Translate to other languages
- Add screenshots/videos

### 💻 Contributing Code

Ready to code? Awesome!

**Good first issues:**
Look for issues tagged with `good first issue` or `help wanted`

**Areas needing help:**
- UI improvements
- Platform-specific fixes (macOS/Windows)
- Performance optimizations
- Additional platform support
- Test coverage

---

## Development Setup

### Prerequisites

**All platforms:**
- CMake 3.16+
- Git
- C++17 compatible compiler

**macOS:**
```bash
brew install sdl2 cmake
xcode-select --install
```

**Linux:**
```bash
sudo apt-get install libsdl2-dev cmake build-essential git
```

**Windows:**
- Visual Studio 2019+ with C++ tools
- CMake

### Clone and Build

```bash
# Clone repository
git clone https://github.com/sttm/YTDAudio.git
cd YTDAudio

# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
make  # or cmake --build . --config Release on Windows

# Run
./ytdaudio.app/Contents/MacOS/ytdaudio  # macOS
./ytdaudio  # Linux
Release\ytdaudio.exe  # Windows
```

### Fast Development Build

For faster iterations (GUI-only, no yt-dlp/ffmpeg copy):

```bash
cmake .. -DFAST_BUILD=ON
make
```

**Note:** Requires system yt-dlp and ffmpeg:
```bash
# macOS
brew install yt-dlp ffmpeg

# Linux
sudo apt-get install yt-dlp ffmpeg
```

### Project Structure

```
YTDAudio/
├── src/
│   ├── main.cpp              # Entry point
│   ├── app.{h,cpp}           # Main application class
│   ├── downloader.{h,cpp}    # Download engine wrapper
│   ├── common/               # Shared utilities
│   ├── ui/                   # UI rendering
│   ├── download/             # Download management
│   ├── history/              # History management
│   ├── file/                 # File operations
│   ├── metadata/             # Metadata fetching
│   ├── settings/             # Settings management
│   ├── service/              # Service checking
│   ├── window/               # Window management
│   ├── events/               # Event handling
│   └── platform/             # Platform-specific code
├── third_party/
│   ├── imgui/                # ImGui library
│   ├── json/                 # JSON library
│   └── SDL2/                 # SDL2 (Windows only)
├── CMakeLists.txt            # Build configuration
└── scripts/                  # Build scripts
```

### Key Classes

- **App**: Main application controller, coordinates all managers
- **DownloadManager**: Manages download queue and tasks
- **HistoryManager**: Handles history persistence and caching
- **UIRenderer**: Renders the user interface
- **MetadataManager**: Fetches metadata and thumbnails
- **FileManager**: File operations and drag & drop
- **Settings**: Configuration management
- **ServiceChecker**: Checks service availability
- **WindowManager**: SDL window management
- **EventHandler**: Input event processing

---

## Coding Standards

### C++ Style

**General:**
- C++17 standard
- Use `std::` prefix (no `using namespace std`)
- RAII principles (smart pointers, no manual new/delete)
- `snake_case` for functions and variables
- `PascalCase` for classes
- `UPPER_CASE` for constants/macros

**Example:**
```cpp
class DownloadManager {
public:
    DownloadManager();
    ~DownloadManager();
    
    void startDownload(const std::string& url);
    bool isDownloading() const;
    
private:
    std::vector<std::unique_ptr<DownloadTask>> tasks_;
    std::mutex tasks_mutex_;
    static const int MAX_CONCURRENT = 3;
};
```

### File Organization

**Headers (.h):**
```cpp
#pragma once

// System includes
#include <string>
#include <vector>

// Third-party includes
#include <SDL.h>

// Project includes
#include "common/types.h"

class MyClass {
    // Public interface first
public:
    MyClass();
    ~MyClass();
    
    // Then private
private:
    void helperMethod();
    std::string data_;
};
```

**Implementation (.cpp):**
```cpp
#include "my_class.h"

// Other includes
#include <iostream>

MyClass::MyClass() {
    // Implementation
}
```

### Error Handling

**Prefer:**
- Return values (bool, optional, etc.)
- Error codes via struct/enum
- Log errors with context

**Avoid:**
- Exceptions (not used in this project)
- Silent failures
- Undefined behavior

**Example:**
```cpp
bool downloadFile(const std::string& url) {
    if (url.empty()) {
        std::cerr << "Error: Empty URL provided" << std::endl;
        return false;
    }
    
    // Download logic...
    
    if (failed) {
        std::cerr << "Error: Download failed: " << error << std::endl;
        return false;
    }
    
    return true;
}
```

### Threading

**Guidelines:**
- Use `std::thread` for background work
- Protect shared data with `std::mutex`
- Use `std::atomic` for simple flags
- Document thread safety
- Join threads in destructors

**Example:**
```cpp
class Worker {
public:
    Worker() : running_(false) {}
    
    ~Worker() {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
    void start() {
        running_ = true;
        thread_ = std::thread(&Worker::work, this);
    }
    
    void stop() {
        running_ = false;
    }
    
private:
    void work() {
        while (running_) {
            // Work...
        }
    }
    
    std::thread thread_;
    std::atomic<bool> running_;
};
```

### Comments

**Use comments for:**
- Complex algorithms
- Platform-specific workarounds
- Thread safety notes
- Public API documentation

**Example:**
```cpp
// Check if playlist files already exist on disk and match them to playlist items.
// Returns true if any files were found, false otherwise.
// Thread-safe: Uses tasks_mutex_ for task access.
bool checkExistingPlaylistFiles(DownloadTask* task, const PlaylistInfo& info);
```

### Platform-Specific Code

**Use preprocessor directives:**
```cpp
#ifdef __APPLE__
    // macOS-specific code
#elif defined(_WIN32)
    // Windows-specific code
#else
    // Linux/Unix code
#endif
```

**Or separate files:**
- `platform_utils_macos.mm` - Objective-C++ for macOS
- `platform_utils.cpp` - Windows/Linux C++

---

## Submitting Changes

### Workflow

1. **Fork** the repository
2. **Create branch** from `main`:
   ```bash
   git checkout -b feature/my-feature
   # or
   git checkout -b fix/my-bugfix
   ```
3. **Make changes** with clear commits
4. **Test** your changes
5. **Push** to your fork
6. **Open Pull Request**

### Commit Messages

**Format:**
```
<type>: <subject>

<body (optional)>

<footer (optional)>
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `refactor`: Code refactoring
- `perf`: Performance improvement
- `test`: Add/update tests
- `chore`: Build/tooling changes

**Examples:**
```
feat: Add yt-dlp update button in settings

Implements asynchronous yt-dlp update with UI feedback.
Shows progress spinner and version after update.

Closes #42
```

```
fix: Resolve playlist retry not working on Windows

The normalizePath function wasn't handling UNC paths correctly.
Now uses unified PathUtils::normalizePath.

Fixes #38
```

### Pull Request Guidelines

**Before submitting:**
- [ ] Code compiles without errors
- [ ] Changes tested on target platform(s)
- [ ] No new warnings introduced
- [ ] Code follows project style
- [ ] Comments added for complex code
- [ ] Documentation updated (if needed)

**PR Description should include:**
- **What** - What does this PR do?
- **Why** - Why is this change needed?
- **How** - How did you implement it?
- **Testing** - How did you test it?
- **Screenshots** - For UI changes

**Example:**
```markdown
## What
Adds a button in Settings to update yt-dlp

## Why
Users need to update yt-dlp to support new platform features and fixes

## How
- Added "Update yt-dlp" button in Settings panel
- Implemented async update using `yt-dlp -U`
- Shows spinner during update
- Displays version after update

## Testing
- [x] Tested on macOS 14.1
- [x] Tested update with bundled yt-dlp
- [x] Verified UI feedback

## Screenshots
[Screenshot here]
```

### Code Review

**What to expect:**
- Feedback on code style/structure
- Questions about implementation choices
- Suggestions for improvements
- Testing on different platforms

**How to respond:**
- Be open to feedback
- Ask questions if unclear
- Make requested changes
- Update PR description if scope changes

---

## Testing

### Manual Testing

**Before submitting:**
1. Test happy path (feature works as intended)
2. Test edge cases (empty input, invalid URLs, etc.)
3. Test error handling (network errors, file errors, etc.)
4. Test on target platforms (macOS/Windows if possible)

### Platform-Specific Testing

**macOS:**
- Test on Intel and Apple Silicon (if possible)
- Verify app bundle structure
- Check code signing/notarization (if modified)

**Windows:**
- Test on Windows 10 and 11
- Verify all DLLs are included
- Check for Visual C++ runtime dependencies

---

## Questions?

- **General questions**: [GitHub Discussions](https://github.com/sttm/YTDAudio/discussions)
- **Bug reports**: [GitHub Issues](https://github.com/sttm/YTDAudio/issues)
- **Security issues**: Email (create GitHub Security Advisory)

---

Thank you for contributing to YTDAudio! 🎵

**[← Back to README](README.md)**


