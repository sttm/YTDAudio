#!/bin/bash

# Build script for YTDAudio

set -e

echo "Building YTDAudio..."

# Create build directory
mkdir -p build
cd build

# Configure with CMake (standard build with yt-dlp and ffmpeg)
cmake -DFAST_BUILD=OFF ..

# Build
make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "Build complete!"
echo "Run the application with: ./build/ytdaudio"





