#!/bin/bash

# Script to download yt-dlp standalone binary for macOS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RESOURCES_DIR="$PROJECT_ROOT/build/ytdaudio.app/Contents/Resources"
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"

# Create Resources directory if it doesn't exist
mkdir -p "$RESOURCES_DIR"

# Check if yt-dlp already exists in bundle
if [ -f "$RESOURCES_DIR/yt-dlp" ] && [ -x "$RESOURCES_DIR/yt-dlp" ]; then
    echo "yt-dlp already exists in bundle"
    exit 0
fi

# First, try to copy from third_party directory
if [ -f "$THIRD_PARTY_DIR/yt-dlp" ] && [ -x "$THIRD_PARTY_DIR/yt-dlp" ]; then
    echo "Found yt-dlp in third_party, copying to bundle..."
    cp "$THIRD_PARTY_DIR/yt-dlp" "$RESOURCES_DIR/yt-dlp"
    chmod +x "$RESOURCES_DIR/yt-dlp"
    echo "Copied yt-dlp from third_party to bundle"
    exit 0
fi

# Try to find system yt-dlp
if command -v yt-dlp &> /dev/null; then
    YTDLP_PATH=$(which yt-dlp)
    echo "Found system yt-dlp: $YTDLP_PATH"
    cp "$YTDLP_PATH" "$RESOURCES_DIR/yt-dlp"
    chmod +x "$RESOURCES_DIR/yt-dlp"
    echo "Copied yt-dlp to bundle"
    exit 0
fi

# Download standalone yt-dlp for macOS
echo "Downloading yt-dlp standalone binary..."

# Get latest release URL
LATEST_URL="https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos"

# Try to download
if curl -L -f -o "$RESOURCES_DIR/yt-dlp" "$LATEST_URL" 2>/dev/null; then
    chmod +x "$RESOURCES_DIR/yt-dlp"
    echo "Downloaded yt-dlp to bundle"
    exit 0
fi

# If download fails, try alternative method with Python
echo "Standalone download failed, trying Python method..."

# Check if Python 3 is available
if command -v python3 &> /dev/null; then
    # Try to install yt-dlp via pip if not available
    if ! python3 -m yt_dlp --version &> /dev/null; then
        echo "Installing yt-dlp via pip..."
        python3 -m pip install --target "$RESOURCES_DIR/yt_dlp_package" yt-dlp --quiet
    fi
    
    # Create wrapper script
    cat > "$RESOURCES_DIR/yt-dlp" << 'EOF'
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -d "$SCRIPT_DIR/yt_dlp_package" ]; then
    export PYTHONPATH="$SCRIPT_DIR/yt_dlp_package:$PYTHONPATH"
    python3 -m yt_dlp "$@"
else
    python3 -m yt_dlp "$@"
fi
EOF
    chmod +x "$RESOURCES_DIR/yt-dlp"
    echo "Created yt-dlp wrapper script"
    exit 0
fi

echo "ERROR: Could not download or create yt-dlp"
exit 1





