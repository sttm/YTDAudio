#!/bin/bash

# Script to download yt-dlp and ffmpeg to third_party directory
# This allows bundling them with the app without downloading during each build

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"

# Create third_party directory if it doesn't exist
mkdir -p "$THIRD_PARTY_DIR"

echo "Downloading dependencies to third_party directory..."

# Download yt-dlp
if [ ! -f "$THIRD_PARTY_DIR/yt-dlp" ] || [ ! -x "$THIRD_PARTY_DIR/yt-dlp" ]; then
    echo "Downloading yt-dlp..."
    LATEST_URL="https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos"
    
    if curl -L -f -o "$THIRD_PARTY_DIR/yt-dlp" "$LATEST_URL" 2>/dev/null; then
        chmod +x "$THIRD_PARTY_DIR/yt-dlp"
        echo "✓ Downloaded yt-dlp to third_party"
    else
        echo "✗ Failed to download yt-dlp"
        exit 1
    fi
else
    echo "✓ yt-dlp already exists in third_party"
fi

# Download ffmpeg (macOS static build)
if [ ! -f "$THIRD_PARTY_DIR/ffmpeg" ] || [ ! -x "$THIRD_PARTY_DIR/ffmpeg" ]; then
    echo "Downloading ffmpeg..."
    # Using static build from evermeet.cx (official macOS builds)
    FFMPEG_URL="https://evermeet.cx/ffmpeg/ffmpeg-7.1.zip"
    
    TEMP_DIR=$(mktemp -d)
    trap "rm -rf $TEMP_DIR" EXIT
    
    if curl -L -f -o "$TEMP_DIR/ffmpeg.zip" "$FFMPEG_URL" 2>/dev/null; then
        cd "$TEMP_DIR"
        unzip -q ffmpeg.zip
        if [ -f "ffmpeg" ]; then
            cp ffmpeg "$THIRD_PARTY_DIR/ffmpeg"
            chmod +x "$THIRD_PARTY_DIR/ffmpeg"
            echo "✓ Downloaded ffmpeg to third_party"
        else
            echo "✗ ffmpeg binary not found in zip"
            exit 1
        fi
    else
        echo "✗ Failed to download ffmpeg"
        echo "  You can manually download ffmpeg from https://evermeet.cx/ffmpeg/"
        echo "  or install via Homebrew: brew install ffmpeg"
        exit 1
    fi
else
    echo "✓ ffmpeg already exists in third_party"
fi

echo ""
echo "All dependencies downloaded successfully to third_party directory!"
echo "Files:"
ls -lh "$THIRD_PARTY_DIR"/yt-dlp "$THIRD_PARTY_DIR"/ffmpeg 2>/dev/null || true








