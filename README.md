# YTDAudio - Audio Downloader

<div align="center">

![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-blue)
![Version](https://img.shields.io/badge/version-1.0.0-green)
![License](https://img.shields.io/badge/license-MIT-orange)

**Modern cross-platform audio downloader with beautiful GUI**

[Download](https://github.com/sttm/YTDAudio/releases/latest) • [Features](#-features) • [Installation](#-installation) • [Usage](#-usage)

</div>

---

## 📖 About

YTDAudio is a cross-platform desktop application for downloading high-quality audio from various streaming platforms. Built with C++17, SDL2, and ImGui, it provides a modern and intuitive user interface for downloading audio content.

## ✨ Features

### 🎵 **Multi-Platform Support**
- **YouTube** - Videos and playlists
- **SoundCloud** - Tracks and albums
- **Spotify** - Songs and playlists (requires cookies)
- **TikTok** - Audio extraction
- **Instagram** - Audio from videos
- **Twitter/X** - Audio extraction
- **Vimeo** - Professional content
- And many more via [yt-dlp](https://github.com/yt-dlp/yt-dlp)

### 🎛️ **Advanced Features**
- ✅ **High-Quality Audio** - Download in best available quality
- 📊 **Real-time Progress** - Live progress bars and download stats
- 🔄 **Playlist Support** - Download entire playlists with tracking
- 🌐 **Proxy Support** - Bypass geo-restrictions (HTTP/SOCKS5)
- 📁 **Smart File Management** - Organized downloads with history
- 🖼️ **Metadata & Thumbnails** - Automatic artwork and info extraction
- 💾 **Multiple Formats** - MP3, M4A, FLAC, Opus, OGG
- 🎚️ **Quality Options** - Best, 320k, 256k, 192k, 128k
- 📜 **Download History** - Track all your downloads
- 🔄 **Retry Failed Downloads** - Smart retry mechanism
- 🎨 **Modern UI** - Beautiful, responsive interface

### 🛠️ **Technical Features**
- Cross-platform (macOS 10.15+, Windows 10+)
- Native GUI built with SDL2 and ImGui
- Powered by yt-dlp for reliable downloads
- FFmpeg integration for audio processing
- Asynchronous downloads (up to 3 concurrent)
- Automatic service availability checks
- Configurable yt-dlp settings (rate limiting, cookies, etc.)

## 📥 Installation

### macOS

1. Download `YTDAudio_mac.zip` from [Releases](https://github.com/sttm/YTDAudio/releases/latest)
2. Extract the archive
3. Move `ytdaudio.app` to your Applications folder
4. Right-click and select "Open" (first launch only, due to Gatekeeper)
5. Grant necessary permissions if prompted

**Note**: The app includes bundled yt-dlp and ffmpeg - no additional dependencies required!

### Windows

1. Download `YTDAudio_win.zip` from [Releases](https://github.com/sttm/YTDAudio/releases/latest)
2. Extract the archive to your preferred location
3. Run `ytdaudio.exe`

**Note**: The app includes all required DLLs, yt-dlp, and ffmpeg.

## 🚀 Usage

### Basic Download

1. Launch YTDAudio
2. Paste a URL in the input field
3. Click **"Download"**
4. Monitor progress in the downloads list
5. Use **"Open File"** or **"Open Folder"** when complete

### Settings

Click the **⚙️ Settings** button to configure:

#### 📁 **Download Settings**
- **Downloads Directory** - Choose where files are saved
- **Audio Format** - MP3, M4A, FLAC, Opus, OGG
- **Quality** - Best, 320k, 256k, 192k, 128k
- **Playlist Options** - Separate folder for playlists

#### 🌐 **Proxy Settings**
- **Enable Proxy** - Use HTTP/SOCKS5 proxy
- **Proxy URL** - Format: `socks5://127.0.0.1:1080` or `http://proxy.example.com:8080`


#### ⚙️ **Advanced yt-dlp Settings**
- **Rate Limiting** - Sleep intervals between downloads
- **Cookie Support** - Use browser cookies for authentication
- **Retry Options** - Fragment retries and timeouts
- **Concurrent Downloads** - Parallel fragment downloads

### Supported URL Examples

```
YouTube Video:    https://www.youtube.com/watch?v=dQw4w9WgXcQ
YouTube Playlist: https://www.youtube.com/playlist?list=PLx0sYbCqOb8TBPRdmBHs5Iftvv9TPboYG
SoundCloud Track: https://soundcloud.com/artist/track-name
Spotify Track:    https://open.spotify.com/track/...
TikTok Video:     https://www.tiktok.com/@user/video/...
Instagram Post:   https://www.instagram.com/p/...
```

### History

- View all past downloads in the **History** tab
- Retry failed downloads with one click
- Delete entries from history
- Open downloaded files directly

## 🔧 Building from Source



## 🔒 Privacy & Security

- **No Data Collection** - All downloads happen locally
- **No Analytics** - Your usage is private
- **Open Source** - Inspect the code yourself
- **Secure** - No network requests except to download services

## ⚠️ Legal Notice

This software is provided for personal, educational, and research purposes only. Users are responsible for complying with the terms of service of the platforms they download from. Please respect copyright laws and content creators' rights.

**Always:**
- Respect copyright and intellectual property
- Follow platform terms of service
- Support content creators you enjoy
- Use downloads for personal use only

## 🐛 Known Issues

- Progress bar may not update for some platforms (depends on yt-dlp output)
- Spotify requires cookies/API keys for full functionality
- Some geo-restricted content may require proxy configuration
- First download after launch may take longer (service checks)

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- [yt-dlp](https://github.com/yt-dlp/yt-dlp) - Powerful download engine
- [ImGui](https://github.com/ocornut/imgui) - Immediate mode GUI library
- [SDL2](https://www.libsdl.org/) - Cross-platform multimedia library
- [nlohmann/json](https://github.com/nlohmann/json) - JSON library for C++

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/sttm/YTDAudio/issues)
- **Discussions**: [GitHub Discussions](https://github.com/sttm/YTDAudio/discussions)

---

<div align="center">

Made with ❤️ by [Statum](https://github.com/sttm)

**[⬆ back to top](#ytdaudio---audio-downloader)**

</div>
