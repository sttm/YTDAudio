#pragma once

#include <string>

namespace ThumbnailDownloader {
    /**
     * Downloads a thumbnail image from URL and converts it to base64
     * Works on Windows, macOS, and Linux
     * 
     * @param thumbnail_url The URL of the thumbnail image
     * @param use_proxy Whether to use proxy (if false, direct connection is used)
     * @return Base64-encoded string of the image, or empty string on failure
     */
    std::string downloadThumbnailAsBase64(const std::string& thumbnail_url, bool use_proxy = false);
}

