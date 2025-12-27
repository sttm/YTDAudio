# Usage Guide

Complete guide to using YTDAudio for downloading audio from various platforms.

## Table of Contents

- [Quick Start](#quick-start)
- [Basic Usage](#basic-usage)
- [Settings Configuration](#settings-configuration)
- [Advanced Features](#advanced-features)
- [Platform-Specific Tips](#platform-specific-tips)
- [Tips & Tricks](#tips--tricks)
- [FAQ](#faq)

---

## Quick Start

1. **Launch YTDAudio**
2. **Paste URL** in the input field
3. **Click "Download"**
4. **Wait** for completion
5. **Open File** or **Open Folder**

That's it! 🎉

---

## Basic Usage

### Downloading a Single Track/Video

1. **Copy URL** from your browser
   - YouTube: `https://www.youtube.com/watch?v=...`
   - SoundCloud: `https://soundcloud.com/artist/track`
   - Spotify: `https://open.spotify.com/track/...`
   - TikTok: `https://www.tiktok.com/@user/video/...`

2. **Paste in YTDAudio**
   - Paste in the input field at the top
   - URL will be validated automatically

3. **Click "Download"**
   - Download starts immediately
   - Progress bar shows real-time status

4. **Monitor Progress**
   - **Speed**: Current download speed (MB/s)
   - **Progress**: Percentage complete
   - **ETA**: Estimated time remaining
   - **Status**: Current operation

5. **Access File**
   - **Open File**: Opens the downloaded audio file
   - **Open Folder**: Opens containing folder
   - **Drag & Drop**: Drag file to other apps

### Downloading Playlists

YTDAudio automatically detects playlists and handles them intelligently.

1. **Paste Playlist URL**
   ```
   YouTube: https://www.youtube.com/playlist?list=...
   SoundCloud: https://soundcloud.com/user/sets/...
   Spotify: https://open.spotify.com/playlist/...
   ```

2. **Playlist Detection**
   - App shows "Detecting playlist..." status
   - Fetches all tracks in playlist
   - Shows total number of items

3. **Individual Track Progress**
   - Click "Show Details" to see individual tracks
   - Green checkmark ✓ = Downloaded
   - Progress bar = Currently downloading
   - Gray = Pending

4. **Resume Failed Downloads**
   - If some tracks fail, click "Retry Missing"
   - Only failed/missing tracks will be re-downloaded
   - Already downloaded tracks are skipped

### Managing Downloads

#### Cancel Download
- Click **"Cancel"** button next to the download
- Partially downloaded files are removed

#### Clear Completed Downloads
- Click **"Clear All"** at the bottom
- Only clears the list, files remain on disk

#### Remove Single Download
- Click **"Remove"** button (X icon)
- Removes from list only

---

## Settings Configuration

Click **⚙️ Settings** button to access configuration.

### Download Settings

#### Downloads Directory
```
Default: ~/Downloads (macOS) or %USERPROFILE%\Downloads (Windows)
```

**To Change:**
1. Click **"Browse..."** button
2. Select folder
3. New downloads go to new location
4. Existing downloads remain in place

#### Audio Format

Choose output format:

| Format | Quality | Compatibility | File Size |
|--------|---------|---------------|-----------|
| **MP3** | Good | Universal | Medium |
| **M4A** | Very Good | Apple, Modern | Small |
| **FLAC** | Lossless | Audiophile | Large |
| **Opus** | Excellent | Modern | Very Small |
| **OGG** | Good | Open Source | Small |

**Recommendations:**
- **General use**: MP3 (universal compatibility)
- **Apple devices**: M4A (best quality/size ratio)
- **Audiophiles**: FLAC (lossless)
- **Space-conscious**: Opus (best compression)

#### Audio Quality

Choose bitrate:

| Quality | Bitrate | File Size | Use Case |
|---------|---------|-----------|----------|
| **Best** | Variable | Largest | Maximum quality |
| **320k** | 320 kbps | Large | High quality |
| **256k** | 256 kbps | Medium-Large | Very good quality |
| **192k** | 192 kbps | Medium | Good quality |
| **128k** | 128 kbps | Small | Acceptable quality |

**Recommendations:**
- **Audiophiles**: Best or 320k
- **General use**: 256k or 192k
- **Mobile/Storage**: 128k

#### Playlist Options

**Save playlists to separate folder:**
- ☑️ Enabled: Creates subfolder named after playlist
- ☐ Disabled: All tracks in downloads folder

Example structure:
```
Downloads/
  ├── My Favorite Songs/
  │   ├── Track 1.mp3
  │   ├── Track 2.mp3
  │   └── Track 3.mp3
  └── single-track.mp3
```

### Proxy Settings

Use proxy to bypass geo-restrictions or network limitations.

#### Enable Proxy
Toggle **"Enable Proxy"** checkbox

#### Proxy URL Format

**HTTP Proxy:**
```
http://proxy.example.com:8080
http://username:password@proxy.example.com:8080
```

**SOCKS5 Proxy:**
```
socks5://127.0.0.1:1080
socks5://username:password@proxy.example.com:1080
```

**Common Proxy Ports:**
- HTTP: 8080, 3128, 80
- SOCKS5: 1080, 1081

**Testing Proxy:**
1. Enable proxy
2. Enter proxy URL
3. Try downloading
4. Check status messages

### API Keys (Optional)

API keys enhance functionality but are **not required** for basic use.

#### Spotify API Key
**Purpose:** Enhanced metadata, private playlists

**How to get:**
1. Go to [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
2. Create app
3. Copy Client ID and Client Secret
4. Paste in format: `client_id:client_secret`

#### YouTube API Key
**Purpose:** Quota management, faster metadata

**How to get:**
1. Go to [Google Cloud Console](https://console.cloud.google.com/)
2. Create project
3. Enable YouTube Data API v3
4. Create credentials (API key)
5. Paste in field

#### SoundCloud API Key
**Purpose:** Private tracks, better reliability

**Note:** SoundCloud API is restricted. Use browser cookies instead (see Advanced Settings).

### Advanced yt-dlp Settings

Fine-tune download behavior.

#### Rate Limiting

**Sleep Intervals for Playlists:**
- ☑️ Enable: Add delay between playlist items
- **Interval**: Minimum seconds between downloads (default: 1)
- **Max Interval**: Maximum seconds (default: 5)
- **Sleep Requests**: Delay after N requests (default: 1)

**Purpose:** Avoid rate limiting from platforms

**Recommended:**
- Small playlists (<10): Disabled or 1 second
- Medium playlists (10-50): 2-3 seconds
- Large playlists (>50): 3-5 seconds

#### Cookie Support

**Use Browser Cookies:**
- ☑️ Enable: Use cookies from browser
- **Select Browser**: Choose browser to extract cookies from

**Supported Browsers:**
- Chrome
- Firefox
- Safari
- Edge
- Brave
- Opera

**Purpose:** Access content requiring login (private playlists, age-restricted content)

**Use Cookie File:**
- ☑️ Enable: Load cookies from file
- **Browse**: Select cookies.txt file

**How to export cookies:**
1. Install browser extension: "Get cookies.txt"
2. Visit platform (YouTube, SoundCloud, etc.)
3. Export cookies.txt
4. Select file in YTDAudio

#### Connection Settings

**Socket Timeout:**
- ☑️ Enable: Set connection timeout
- **Timeout**: Seconds before timeout (default: 30)

**Purpose:** Prevent hanging on slow connections

**Fragment Retries:**
- ☑️ Enable: Retry failed fragments
- **Retries**: Number of retry attempts (default: 10)

**Purpose:** Recover from transient network errors

**Concurrent Fragments:**
- ☑️ Enable: Download multiple fragments in parallel
- **Fragments**: Number of simultaneous connections (default: 1)

**Purpose:** Faster downloads for large files

**Recommended values:**
- Fast connection: 4-8 fragments
- Medium connection: 2-4 fragments
- Slow connection: 1 fragment

---

## Advanced Features

### Download History

Access via **History** tab.

**Features:**
- View all past downloads
- Open files directly
- Retry failed downloads
- Delete history entries

**History Actions:**
- **Open File**: Launch downloaded file
- **Open Folder**: Show in file browser
- **Retry**: Re-download (if failed or deleted)
- **Delete**: Remove from history

**History Persistence:**
- Stored in: `~/Library/Application Support/ytdaudio/history.json` (macOS)
- Survives app restarts
- No limit on entries

### Service Availability Check

YTDAudio automatically checks if download services are accessible.

**When:**
- On app launch (one-time check)
- On demand (click "Check Services")

**Status Indicators:**
- ✅ **Available**: Service is accessible
- ⚠️ **Warning**: Service may be slow/limited
- ❌ **Unavailable**: Service is blocked/down

**If service unavailable:**
1. Check internet connection
2. Try enabling proxy
3. Wait and retry (may be temporary)

### Concurrent Downloads

YTDAudio supports up to **3 simultaneous downloads**.

**Behavior:**
- Downloads 1-3: Start immediately
- Downloads 4+: Queue automatically
- Complete downloads free up slots
- Queue processes automatically

**Benefits:**
- Faster batch downloads
- Efficient resource usage
- No manual management needed

### Retry Mechanism

Intelligent retry for failed downloads.

**Automatic Retry:**
- Network errors: Auto-retry with exponential backoff
- Transient errors: Up to 3 attempts
- Permanent errors: Fail with message

**Manual Retry:**
- **From active downloads**: Click "Retry"
- **From history**: Click "Retry" in History tab
- **Playlist items**: Click "Retry Missing"

---

## Platform-Specific Tips

### YouTube

**Best practices:**
- Use direct video URLs, not shortened (youtu.be)
- Playlists work great (up to 1000 videos)
- Age-restricted: Use browser cookies
- Private videos: Ensure logged in with cookies

**URL formats:**
```
Video:    https://www.youtube.com/watch?v=VIDEO_ID
Playlist: https://www.youtube.com/playlist?list=PLAYLIST_ID
Channel:  https://www.youtube.com/@channel/videos
```

### SoundCloud

**Best practices:**
- Use direct track/playlist URLs
- Private tracks: Use browser cookies
- Playlists: Enable rate limiting (2-3 sec)

**URL formats:**
```
Track:    https://soundcloud.com/artist/track-name
Playlist: https://soundcloud.com/artist/sets/playlist-name
```

### Spotify

**Requirements:**
- Browser cookies (mandatory for most content)
- Spotify premium (for high quality)

**Setup:**
1. Enable "Use Browser Cookies"
2. Select browser with Spotify login
3. Ensure logged in to Spotify in browser

**URL formats:**
```
Track:    https://open.spotify.com/track/TRACK_ID
Playlist: https://open.spotify.com/playlist/PLAYLIST_ID
Album:    https://open.spotify.com/album/ALBUM_ID
```

### TikTok

**Best practices:**
- Works with video URLs
- Extracts audio from video
- No login required

**URL format:**
```
https://www.tiktok.com/@username/video/VIDEO_ID
```

### Instagram

**Best practices:**
- Works with post/reel URLs
- Extracts audio from videos
- May require cookies for private accounts

**URL formats:**
```
Post: https://www.instagram.com/p/POST_ID/
Reel: https://www.instagram.com/reel/REEL_ID/
```

---

## Tips & Tricks

### 1. Batch Downloads

To download multiple tracks:
1. Download first track
2. Paste second URL while first downloads
3. Downloads queue automatically
4. Up to 3 parallel downloads

### 2. Organize with Playlists

Create playlists on the platform first:
1. Add tracks to playlist on YouTube/SoundCloud
2. Download entire playlist in YTDAudio
3. Enable "separate folder" option
4. All tracks organized in subfolder

### 3. Quick Access

**macOS:**
- Add to Dock for quick access
- Use Spotlight: `⌘ + Space`, type "ytdaudio"

**Windows:**
- Pin to Taskbar
- Create desktop shortcut
- Add to Start Menu

### 4. Proxy for Geo-Restrictions

If content is blocked in your region:
1. Get SOCKS5 proxy (or VPN)
2. Enable proxy in settings
3. Enter proxy URL
4. All downloads use proxy

### 5. Monitor File Sizes

Large file sizes may indicate:
- High quality (good!)
- Wrong format selected
- Download error

Check expected sizes:
- 3-minute MP3 @ 320k ≈ 7-9 MB
- 3-minute FLAC ≈ 20-30 MB

### 6. Drag & Drop Integration

After download:
1. Click and hold file in YTDAudio
2. Drag to another app:
   - Music player
   - DAW (Ableton, Logic, etc.)
   - Video editor
3. Release to import

---

## FAQ

### Q: Can I download videos?
**A:** No, YTDAudio is audio-only. For videos, use yt-dlp directly.

### Q: Are downloads legal?
**A:** You are responsible for complying with platform terms of service and copyright laws. Download only content you have rights to.

### Q: Why is progress stuck at 0%?
**A:** Some platforms don't provide progress info. File is still downloading - check folder for growing file size.

### Q: Can I change quality after download?
**A:** No, you need to re-download. Future feature: conversion without re-download.

### Q: Where are settings stored?
**A:** 
- macOS: `~/Library/Application Support/ytdaudio/config.json`
- Windows: `%APPDATA%\ytdaudio\config.json`

### Q: How do I update yt-dlp?
**A:** Coming soon! For now:
- macOS: `brew upgrade yt-dlp`
- Windows: Download latest from [yt-dlp releases](https://github.com/yt-dlp/yt-dlp/releases)

### Q: Can I schedule downloads?
**A:** Not yet. Planned for future release.

### Q: Does it work offline?
**A:** No, internet connection required for downloading. History viewing works offline.

### Q: Can I pause/resume downloads?
**A:** Pause is not supported. Cancel and retry will restart from beginning.

### Q: Maximum concurrent downloads?
**A:** 3 simultaneous downloads. More queue automatically.

### Q: Does it support torrents?
**A:** No, only direct downloads via yt-dlp.

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `⌘/Ctrl + V` | Paste URL in input field |
| `Enter` | Start download |
| `⌘/Ctrl + Q` | Quit application |
| `⌘/Ctrl + ,` | Open settings (planned) |

---

## Getting Help

Still need help?

1. **Check Known Issues**: [README → Known Issues](README.md#-known-issues)
2. **Installation Issues**: See [INSTALLATION.md](INSTALLATION.md#troubleshooting)
3. **Report Bug**: [GitHub Issues](https://github.com/sttm/YTDAudio/issues)

---

**[← Back to README](README.md)**

