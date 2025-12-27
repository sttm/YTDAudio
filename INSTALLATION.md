# Installation Guide

This guide provides detailed installation instructions for YTDAudio on different platforms.

## Table of Contents

- [macOS Installation](#macos-installation)
- [Windows Installation](#windows-installation)
- [Building from Source](#building-from-source)
- [Troubleshooting](#troubleshooting)

---

## macOS Installation

### System Requirements

- **Operating System**: macOS 10.15 (Catalina) or later
- **Architecture**: Intel (x86_64) or Apple Silicon (arm64, via Rosetta 2)
- **RAM**: 4 GB minimum, 8 GB recommended
- **Disk Space**: 100 MB for application + space for downloads

### Installation Steps

1. **Download the Application**
   - Go to [Releases](https://github.com/sttm/YTDAudio/releases/latest)
   - Download `YTDAudio_mac.zip`

2. **Extract the Archive**
   - Double-click `YTDAudio_mac.zip` to extract
   - Or use Terminal: `unzip YTDAudio_mac.zip`

3. **Move to Applications**
   ```bash
   mv ytdaudio.app /Applications/
   ```
   Or drag `ytdaudio.app` to your Applications folder

4. **First Launch**
   - **Important**: Don't double-click the app yet!
   - Right-click (or Ctrl+click) on `ytdaudio.app`
   - Select **"Open"** from the context menu
   - Click **"Open"** in the security dialog
   
   This is required only once due to macOS Gatekeeper (the app is not code-signed).

5. **Grant Permissions** (if prompted)
   - Allow network access if macOS asks
   - Allow file access for downloads folder

### Alternative Launch Methods

**Via Terminal:**
```bash
open /Applications/ytdaudio.app
```

**Via Spotlight:**
- Press `⌘ + Space`
- Type "ytdaudio"
- Press Enter

### Included Dependencies

The macOS package includes:
- ✅ SDL2 framework (bundled)
- ✅ yt-dlp binary (bundled)
- ✅ ffmpeg binary (bundled)
- ✅ Roboto-Light font (bundled)

**No additional installation required!**

### Uninstallation

To completely remove YTDAudio:

```bash
# Remove application
rm -rf /Applications/ytdaudio.app

# Remove settings and history (optional)
rm -rf ~/Library/Application\ Support/ytdaudio/
```

---

## Windows Installation

### System Requirements

- **Operating System**: Windows 10 (64-bit) or later
- **Architecture**: x86_64 (64-bit)
- **RAM**: 4 GB minimum, 8 GB recommended
- **Disk Space**: 150 MB for application + space for downloads

### Installation Steps

1. **Download the Application**
   - Go to [Releases](https://github.com/sttm/YTDAudio/releases/latest)
   - Download `YTDAudio_win.zip`

2. **Extract the Archive**
   - Right-click `YTDAudio_win.zip`
   - Select **"Extract All..."**
   - Choose destination folder (e.g., `C:\Program Files\YTDAudio`)
   - Click **"Extract"**

3. **Launch the Application**
   - Navigate to extracted folder
   - Double-click `ytdaudio.exe`

4. **Windows SmartScreen** (if prompted)
   - Click **"More info"**
   - Click **"Run anyway"**
   
   This is normal for unsigned applications.

5. **Grant Permissions** (if prompted)
   - Allow network access through Windows Firewall
   - Allow file access for downloads folder

### Optional: Create Desktop Shortcut

1. Right-click `ytdaudio.exe`
2. Select **"Send to"** → **"Desktop (create shortcut)"**
3. Rename shortcut to "YTDAudio"

### Optional: Add to Start Menu

1. Right-click `ytdaudio.exe`
2. Select **"Pin to Start"**

### Included Dependencies

The Windows package includes:
- ✅ SDL2.dll (bundled)
- ✅ yt-dlp.exe (bundled)
- ✅ ffmpeg.exe (bundled)
- ✅ Roboto-Light.ttf (bundled)
- ✅ All required Visual C++ Runtime DLLs

**No additional installation required!**

### Uninstallation

To completely remove YTDAudio:

1. Delete the installation folder (e.g., `C:\Program Files\YTDAudio`)
2. Delete settings and history (optional):
   ```
   %APPDATA%\ytdaudio\
   ```
   - Press `Win + R`
   - Type `%APPDATA%\ytdaudio`
   - Delete folder

---

## Building from Source

### Prerequisites

#### All Platforms
- **CMake**: 3.16 or later
- **C++ Compiler**: Supporting C++17
  - macOS: Clang (Xcode Command Line Tools)
  - Windows: MSVC 2019+ (Visual Studio)
  - Linux: GCC 7+ or Clang 5+

#### Platform-Specific Dependencies

**macOS:**
```bash
# Install dependencies via Homebrew
brew install sdl2 cmake

# Install Xcode Command Line Tools (if not already)
xcode-select --install
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install -y \
    libsdl2-dev \
    cmake \
    build-essential \
    git
```

**Windows:**
- Install [Visual Studio 2019](https://visualstudio.microsoft.com/) or later
  - Include "Desktop development with C++"
- Install [CMake](https://cmake.org/download/)
- SDL2 is included in `third_party/SDL2/`

### Build Steps

#### 1. Clone Repository

```bash
git clone https://github.com/sttm/YTDAudio.git
cd YTDAudio
```

#### 2. Configure Build

**macOS/Linux:**
```bash
mkdir build
cd build
cmake ..
```

**Windows (Visual Studio):**
```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64
```

#### 3. Build

**macOS/Linux:**
```bash
make -j$(nproc)
```

**Windows:**
```cmd
cmake --build . --config Release
```

#### 4. Run

**macOS:**
```bash
./ytdaudio.app/Contents/MacOS/ytdaudio
```

**Linux:**
```bash
./ytdaudio
```

**Windows:**
```cmd
Release\ytdaudio.exe
```

### Fast Build (Development)

For faster GUI-only builds (skip yt-dlp/ffmpeg download/copy):

```bash
cmake .. -DFAST_BUILD=ON
make
```

**Note**: You'll need system yt-dlp and ffmpeg installed:
```bash
# macOS
brew install yt-dlp ffmpeg

# Linux
sudo apt-get install yt-dlp ffmpeg

# Windows
# Download from official sites
```

### Building Universal Binary (macOS)

To build a universal binary supporting both Intel and Apple Silicon:

```bash
cmake .. -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
make
```

---

## Troubleshooting

### macOS

#### Problem: "App is damaged and can't be opened"

**Solution:**
```bash
xattr -cr /Applications/ytdaudio.app
```

Then right-click and select "Open".

#### Problem: "yt-dlp not found"

**Solution:** The app should include yt-dlp. If missing:
```bash
# Check if bundled
ls /Applications/ytdaudio.app/Contents/Resources/yt-dlp

# If missing, install system-wide
brew install yt-dlp
```

#### Problem: "ffmpeg not found"

**Solution:**
```bash
# Check if bundled
ls /Applications/ytdaudio.app/Contents/Resources/ffmpeg

# If missing, install system-wide
brew install ffmpeg
```

#### Problem: App crashes on launch

**Solution:**
1. Check Console.app for crash logs
2. Ensure macOS 10.15+
3. Try removing settings:
   ```bash
   rm -rf ~/Library/Application\ Support/ytdaudio/
   ```

### Windows

#### Problem: "VCRUNTIME140.dll missing"

**Solution:**
Install [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe)

#### Problem: "SDL2.dll missing"

**Solution:**
Ensure `SDL2.dll` is in the same folder as `ytdaudio.exe`

#### Problem: "yt-dlp.exe is not recognized"

**Solution:**
Ensure `yt-dlp.exe` is in the same folder as `ytdaudio.exe`

#### Problem: Windows Defender blocks the app

**Solution:**
1. Click **"More info"**
2. Click **"Run anyway"**
3. Add folder to Windows Defender exclusions (optional):
   ```
   Settings → Update & Security → Windows Security → Virus & threat protection
   → Manage settings → Add or remove exclusions
   ```

### General Issues

#### Problem: Downloads fail immediately

**Possible causes:**
1. Network connectivity issues
2. Service (YouTube, etc.) is blocked/unavailable
3. Invalid URL
4. yt-dlp needs update

**Solutions:**
- Check internet connection
- Try enabling proxy in settings
- Verify URL is correct and accessible
- Wait for yt-dlp update feature (coming soon)

#### Problem: Progress bar doesn't update

**Note:** This is expected for some platforms where yt-dlp doesn't output progress.
The file will still download - check the file size in the destination folder.

#### Problem: Very slow downloads

**Solutions:**
- Check your internet speed
- Disable proxy if not needed
- Try different quality settings
- Check yt-dlp rate limiting settings

#### Problem: "Service check failed"

**Solutions:**
- Check internet connection
- Try again in a few seconds
- Check if platform (YouTube, etc.) is accessible
- Configure proxy if platform is blocked

---

## Configuration Files Location

### macOS
```
~/Library/Application Support/ytdaudio/
├── config.json      # Settings
└── history.json     # Download history
```

### Windows
```
%APPDATA%\ytdaudio\
├── config.json      # Settings
└── history.json     # Download history
```

### Linux (if building from source)
```
~/.config/ytdaudio/
├── config.json      # Settings
└── history.json     # Download history
```

---

## Getting Help

If you encounter issues not covered here:

1. Check [Known Issues](https://github.com/sttm/YTDAudio#-known-issues)
2. Search [GitHub Issues](https://github.com/sttm/YTDAudio/issues)
3. Create a [New Issue](https://github.com/sttm/YTDAudio/issues/new) with:
   - Operating system and version
   - Steps to reproduce
   - Error messages or screenshots
   - Console output (if available)

---

**[← Back to README](README.md)**

