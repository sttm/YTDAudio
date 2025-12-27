#ifndef BASE64_H
#define BASE64_H

#include <string>
#include <vector>
#include <cstdint>

namespace Base64 {

// Base64 encoding table
static const char BASE64_CHARS[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Encode binary data to base64 string
 * @param data Pointer to binary data
 * @param size Size of data in bytes
 * @return Base64 encoded string
 */
inline std::string encode(const unsigned char* data, size_t size) {
    std::string result;
    result.reserve(((size + 2) / 3) * 4);
    
    size_t i = 0;
    while (i < size) {
        uint32_t value = 0;
        int bits = 0;
        
        // Read up to 3 bytes
        for (int j = 0; j < 3 && i < size; j++, i++) {
            value = (value << 8) | data[i];
            bits += 8;
        }
        
        // Pad to 24 bits
        value <<= (24 - bits);
        
        // Encode to base64 (4 characters)
        for (int j = 0; j < 4; j++) {
            if (bits > 0) {
                result += BASE64_CHARS[(value >> 18) & 0x3F];
                value <<= 6;
                bits -= 6;
            } else {
                result += '=';
            }
        }
    }
    
    return result;
}

/**
 * Encode binary data from vector to base64 string
 * @param data Vector of unsigned chars
 * @return Base64 encoded string
 */
inline std::string encode(const std::vector<unsigned char>& data) {
    if (data.empty()) return "";
    return encode(data.data(), data.size());
}

} // namespace Base64

#endif // BASE64_H

