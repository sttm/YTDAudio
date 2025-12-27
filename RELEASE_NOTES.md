# YTDAudio v1.0.0 Release Notes

**Release Date:** December 27, 2024

## 🎉 Welcome to YTDAudio v1.0.0!

We're excited to announce the first stable release of YTDAudio - a modern, cross-platform audio downloader with a beautiful graphical interface.

---

## 📥 Downloads

### macOS (10.15+)
**File:** `YTDAudio_mac.zip` (63 MB)

**What's included:**
- Universal app bundle (Intel + Apple Silicon ready via Rosetta 2)
- Bundled yt-dlp binary
- Bundled ffmpeg binary
- SDL2 framework
- All required fonts and resources

**Installation:**
1. Download and extract `YTDAudio_mac.zip`
2. Move `ytdaudio.app` to Applications
3. Right-click and select "Open" (first launch only)

### Windows (10+)
**File:** `YTDAudio_win.zip` (77 MB)

**What's included:**
- Standalone executable (no installation required)
- Bundled yt-dlp.exe
- Bundled ffmpeg.exe
- SDL2.dll and all required libraries
- All required fonts and resources

**Installation:**
1. Download and extract `YTDAudio_win.zip`
2. Run `ytdaudio.exe`

---

## ✨ Key Features

### 🎵 Multi-Platform Support
Download audio from:
- **YouTube** - Videos and playlists
- **SoundCloud** - Tracks and sets
- **Spotify** - Songs and playlists (requires cookies)
- **TikTok** - Audio extraction
- **Instagram** - Audio from videos/reels
- **Twitter/X** - Audio extraction
- **Vimeo** - Professional content
- And 1000+ more platforms via yt-dlp

### 🎛️ Advanced Features
- ✅ **High-Quality Audio** - Best available quality, up to 320kbps
- 📊 **Real-Time Progress** - Live download speed, ETA, and progress
- 🔄 **Playlist Support** - Download entire playlists with individual tracking
- 🌐 **Proxy Support** - HTTP and SOCKS5 for bypassing restrictions
- 📁 **Smart Organization** - Automatic folder management for playlists
- 🖼️ **Metadata & Thumbnails** - Automatic artwork and info extraction
- 💾 **Multiple Formats** - MP3, M4A, FLAC, Opus, OGG
- 🎚️ **Quality Selection** - Best, 320k, 256k, 192k, 128k
- 📜 **Download History** - Track all downloads with retry support
- 🎨 **Modern UI** - Beautiful, responsive ImGui interface

### 🛠️ Technical Highlights
- Cross-platform C++17 codebase
- Modular architecture with clean separation of concerns
- Asynchronous downloads (up to 3 concurrent)
- Smart retry mechanism with exponential backoff
- Service availability checking
- Comprehensive yt-dlp configuration options

---

## 📖 Documentation

Comprehensive documentation is included:

- **[README.md](README.md)** - Overview and quick start
- **[INSTALLATION.md](INSTALLATION.md)** - Detailed installation instructions
- **[USAGE.md](USAGE.md)** - Complete usage guide with tips & tricks
- **[CHANGELOG.md](CHANGELOG.md)** - Full changelog
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Contributing guidelines
- **[LICENSE](LICENSE)** - MIT License

---

## 🚀 Quick Start

1. Download the appropriate package for your platform
2. Extract and install/run the application
3. Paste a URL in the input field
4. Click "Download"
5. Monitor progress and enjoy your audio!

For detailed instructions, see [USAGE.md](USAGE.md).

---

## ⚙️ Configuration

YTDAudio offers extensive configuration options:

### Download Settings
- Custom download directory
- Audio format selection (MP3, M4A, FLAC, Opus, OGG)
- Quality/bitrate selection
- Playlist folder organization

### Network Settings
- HTTP/SOCKS5 proxy support
- Automatic service availability checks
- Configurable timeouts and retries

### Advanced Settings
- yt-dlp rate limiting
- Browser cookie integration
- Fragment retry options
- Concurrent fragment downloads
- API key support (Spotify, YouTube, SoundCloud)

---

## 🐛 Known Issues

- Progress bar may not update for some platforms (depends on yt-dlp output format)
- Spotify requires cookie authentication for most content
- Some platforms may require proxy in restricted regions
- First download after app launch includes ~2-3s service check

These are expected limitations based on platform APIs and will be addressed in future releases where possible.

---

## 🗺️ Roadmap

Planned features for future releases:

### v1.1.0 (Coming Soon)
- **yt-dlp Update Button** - Update yt-dlp directly from settings
- **Search History** - Filter and search download history
- **Batch Import** - Import multiple URLs from text file
- **Dark/Light Theme** - Theme customization

### v1.2.0
- **Download Scheduler** - Schedule downloads for specific times
- **Format Conversion** - Convert without re-downloading
- **Custom Shortcuts** - Configurable keyboard shortcuts
- **Download Profiles** - Save preset configurations

### Future
- **Embedded Metadata Editor** - Edit tags after download
- **Download Speed Limiter** - Cap download bandwidth
- **Multi-Language UI** - Internationalization support
- **Cloud Sync** - Sync history and settings across devices

---

## 🤝 Contributing

YTDAudio is open source! Contributions are welcome.

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on:
- Reporting bugs
- Suggesting features
- Contributing code
- Development setup

---

## 🙏 Acknowledgments

YTDAudio is built on the shoulders of giants:

- **[yt-dlp](https://github.com/yt-dlp/yt-dlp)** - Powerful download engine powering all downloads
- **[ImGui](https://github.com/ocornut/imgui)** - Excellent immediate mode GUI library
- **[SDL2](https://www.libsdl.org/)** - Cross-platform multimedia library
- **[nlohmann/json](https://github.com/nlohmann/json)** - Modern JSON library for C++

Special thanks to all the maintainers and contributors of these projects!

---

## 📄 License

YTDAudio is licensed under the MIT License. See [LICENSE](LICENSE) for full text.

**In summary:**
- ✅ Commercial use
- ✅ Modification
- ✅ Distribution
- ✅ Private use
- ⚠️ No warranty
- ⚠️ No liability

---

## ⚠️ Legal Notice

This software is provided for personal, educational, and research purposes only.

**Please:**
- Respect copyright and intellectual property rights
- Follow platform terms of service
- Support content creators you enjoy
- Use downloads for personal use only

**Users are responsible for complying with the terms of service of the platforms they download from.**

---

## 📞 Support

Need help?

- **Bug Reports**: [GitHub Issues](https://github.com/sttm/YTDAudio/issues)
- **Feature Requests**: [GitHub Issues](https://github.com/sttm/YTDAudio/issues)
- **Discussions**: [GitHub Discussions](https://github.com/sttm/YTDAudio/discussions)
- **Documentation**: See included documentation files

---

## 🔒 Security

Found a security issue? Please report it responsibly:
- Create a [GitHub Security Advisory](https://github.com/sttm/YTDAudio/security/advisories/new)
- Do not open public issues for security vulnerabilities

---

<div align="center">

**Thank you for using YTDAudio!** 🎵

Made with ❤️ by [sttm](https://github.com/sttm)

[⬆ Back to Top](#ytdaudio-v100-release-notes)

</div>

