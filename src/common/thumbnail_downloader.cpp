#include "thumbnail_downloader.h"
#include "base64.h"
#include <iostream>
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <urlmon.h>
#include <shlobj.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "urlmon.lib")
#else
#include <unistd.h>
#include <sys/wait.h>
#include <cstdio>
#endif

namespace ThumbnailDownloader {

#ifdef _WIN32
std::string downloadThumbnailAsBase64(const std::string& thumbnail_url, bool use_proxy) {
    if (thumbnail_url.empty()) {
        std::cout << "[DEBUG] ThumbnailDownloader: Empty URL" << std::endl;
        return "";
    }
    
    std::cout << "[DEBUG] ThumbnailDownloader: Downloading from: " << thumbnail_url << std::endl;
    
    // METHOD 1: Try simple URLDownloadToFile first (works for direct file links)
    // This is simpler and faster for most CDN links like SoundCloud
    std::string temp_file_path;
    {
        // Get temp directory
        char temp_path[MAX_PATH];
        if (GetTempPathA(MAX_PATH, temp_path) > 0) {
            // Create unique temp filename
            char temp_file[MAX_PATH];
            if (GetTempFileNameA(temp_path, "ytd", 0, temp_file) > 0) {
                temp_file_path = temp_file;
                
                // Convert URL to wide string for URLDownloadToFileW
                int url_size = MultiByteToWideChar(CP_UTF8, 0, thumbnail_url.c_str(), -1, NULL, 0);
                if (url_size > 0) {
                    std::vector<wchar_t> url_wide(url_size);
                    MultiByteToWideChar(CP_UTF8, 0, thumbnail_url.c_str(), -1, url_wide.data(), url_size);
                    
                    // Convert temp file path to wide string
                    int file_size = MultiByteToWideChar(CP_UTF8, 0, temp_file_path.c_str(), -1, NULL, 0);
                    if (file_size > 0) {
                        std::vector<wchar_t> file_wide(file_size);
                        MultiByteToWideChar(CP_UTF8, 0, temp_file_path.c_str(), -1, file_wide.data(), file_size);
                        
                        std::cout << "[DEBUG] ThumbnailDownloader: Trying simple download method (URLDownloadToFile)..." << std::endl;
                        
                        // Try to download using URLDownloadToFile (simple, direct method)
                        HRESULT hr = URLDownloadToFileW(NULL, url_wide.data(), file_wide.data(), 0, NULL);
                        
                        if (SUCCEEDED(hr)) {
                            // Success! Read file and convert to base64
                            std::cout << "[DEBUG] ThumbnailDownloader: Simple download method succeeded!" << std::endl;
                            
                            // Read file
                            FILE* file = fopen(temp_file_path.c_str(), "rb");
                            if (file) {
                                fseek(file, 0, SEEK_END);
                                long file_size_bytes = ftell(file);
                                fseek(file, 0, SEEK_SET);
                                
                                if (file_size_bytes > 0) {
                                    std::vector<unsigned char> data(file_size_bytes);
                                    size_t read = fread(data.data(), 1, file_size_bytes, file);
                                    fclose(file);
                                    
                                    // Delete temp file
                                    DeleteFileA(temp_file_path.c_str());
                                    
                                    if (read == file_size_bytes) {
                                        std::cout << "[DEBUG] ThumbnailDownloader: Downloaded " << data.size() << " bytes using simple method" << std::endl;
                                        return Base64::encode(data.data(), data.size());
                                    }
                                } else {
                                    fclose(file);
                                }
                            }
                            
                            // Clean up temp file if read failed
                            DeleteFileA(temp_file_path.c_str());
                        } else {
                            std::cout << "[DEBUG] ThumbnailDownloader: Simple download method failed (HRESULT: " << hr 
                                      << "), falling back to WinHttp method..." << std::endl;
                        }
                    }
                }
            }
        }
    }
    
    // METHOD 2: Fallback to WinHttp (more control, works with complex URLs)
    std::cout << "[DEBUG] ThumbnailDownloader: Using WinHttp method..." << std::endl;
    
    // Parse URL - convert UTF-8 to wide string properly
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, thumbnail_url.c_str(), -1, NULL, 0);
    if (size_needed <= 0) {
        std::cout << "[DEBUG] ThumbnailDownloader: Failed to convert URL to wide string" << std::endl;
        return "";
    }
    std::vector<wchar_t> url_wide_buffer(size_needed);
    MultiByteToWideChar(CP_UTF8, 0, thumbnail_url.c_str(), -1, url_wide_buffer.data(), size_needed);
    std::wstring url_wide(url_wide_buffer.data());
    URL_COMPONENTSW url_components = {0};
    url_components.dwStructSize = sizeof(url_components);
    url_components.dwSchemeLength = (DWORD)-1;
    url_components.dwHostNameLength = (DWORD)-1;
    url_components.dwUrlPathLength = (DWORD)-1;
    url_components.dwExtraInfoLength = (DWORD)-1;
    
    wchar_t scheme[32] = {0};
    wchar_t hostname[256] = {0};
    wchar_t url_path[2048] = {0};
    wchar_t extra_info[256] = {0};
    
    url_components.lpszScheme = scheme;
    url_components.lpszHostName = hostname;
    url_components.lpszUrlPath = url_path;
    url_components.lpszExtraInfo = extra_info;
    
    if (!WinHttpCrackUrl(url_wide.c_str(), url_wide.length(), 0, &url_components)) {
        DWORD error = GetLastError();
        std::cout << "[DEBUG] ThumbnailDownloader: Failed to parse URL, error: " << error << std::endl;
        return "";
    }
    
    // Log parsed URL components
    std::wcout << L"[DEBUG] ThumbnailDownloader: Parsed URL - scheme: " << scheme 
               << L", hostname: " << hostname 
               << L", port: " << url_components.nPort 
               << L", path: " << url_path << std::endl;
    
    // Determine port
    INTERNET_PORT port = url_components.nPort;
    if (port == 0) {
        port = (url_components.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    }
    std::cout << "[DEBUG] ThumbnailDownloader: Using port: " << port << std::endl;
    
    // Open session - use proxy only if use_proxy is true
    HINTERNET session = NULL;
    if (use_proxy) {
        // Try with auto-detect proxy first
        session = WinHttpOpen(L"YTDAudio/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, 
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            // Fallback to default proxy if auto-detect fails
            session = WinHttpOpen(L"YTDAudio/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session) {
                DWORD error = GetLastError();
                std::cout << "[DEBUG] ThumbnailDownloader: Failed to open WinHTTP session with proxy, error: " << error << std::endl;
                return "";
            }
            std::cout << "[DEBUG] ThumbnailDownloader: WinHTTP session opened with DEFAULT_PROXY" << std::endl;
        } else {
            std::cout << "[DEBUG] ThumbnailDownloader: WinHTTP session opened with AUTOMATIC_PROXY" << std::endl;
        }
    } else {
        // Direct connection without proxy
        session = WinHttpOpen(L"YTDAudio/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, 
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            DWORD error = GetLastError();
            std::cout << "[DEBUG] ThumbnailDownloader: Failed to open WinHTTP session without proxy, error: " << error << std::endl;
            return "";
        }
        std::cout << "[DEBUG] ThumbnailDownloader: WinHTTP session opened with NO_PROXY (direct connection)" << std::endl;
    }
    
    // Set SSL protocols on session (must be done before connecting)
    // Enable TLS 1.2 (most compatible and secure)
    if (url_components.nScheme == INTERNET_SCHEME_HTTPS) {
        DWORD secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &secure_protocols, sizeof(secure_protocols))) {
            std::cout << "[DEBUG] ThumbnailDownloader: SSL protocols configured on session (TLS 1.2)" << std::endl;
        } else {
            DWORD error = GetLastError();
            std::cout << "[DEBUG] ThumbnailDownloader: Failed to set SSL protocols on session, error: " << error << " (will use default)" << std::endl;
        }
    }
    
    // Set timeouts on session (affects all operations)
    // Use longer timeouts for thumbnail downloads to handle slow connections and CDN delays
    // Note: WinHttp default timeouts are often too short for CDN responses
    DWORD resolve_timeout = 30000;  // 30 seconds for DNS resolution
    DWORD connect_timeout = 30000;  // 30 seconds for connection
    DWORD send_timeout = 30000;     // 30 seconds for sending (CDN may be slow to accept requests)
    DWORD receive_timeout = 30000;   // 30 seconds for receiving (CDN may be slow)
    WinHttpSetTimeouts(session, resolve_timeout, connect_timeout, send_timeout, receive_timeout);
    std::cout << "[DEBUG] ThumbnailDownloader: Session timeouts set (resolve/connect/send/receive): " 
              << resolve_timeout << "/" << connect_timeout << "/" << send_timeout << "/" << receive_timeout << " ms" << std::endl;
    
    // Connect to host
    HINTERNET connect = WinHttpConnect(session, hostname, port, 0);
    if (!connect) {
        DWORD error = GetLastError();
        WinHttpCloseHandle(session);
        const char* error_msg = "Unknown error";
        switch (error) {
            case ERROR_WINHTTP_CANNOT_CONNECT: error_msg = "Cannot connect"; break;
            case ERROR_WINHTTP_NAME_NOT_RESOLVED: error_msg = "Name not resolved"; break;
            case ERROR_WINHTTP_TIMEOUT: error_msg = "Timeout"; break;
        }
        std::cout << "[DEBUG] ThumbnailDownloader: Failed to connect to host " << hostname << ":" << port << ", error: " << error << " (" << error_msg << ")" << std::endl;
        return "";
    }
    std::cout << "[DEBUG] ThumbnailDownloader: Connected to host successfully" << std::endl;
    
    // Build request path
    std::wstring request_path = std::wstring(url_path);
    if (url_components.dwExtraInfoLength > 0) {
        request_path += std::wstring(extra_info);
    }
    std::wcout << L"[DEBUG] ThumbnailDownloader: Request path: " << request_path << std::endl;
    
    // Open request
    DWORD request_flags = 0;
    if (url_components.nScheme == INTERNET_SCHEME_HTTPS) {
        request_flags = WINHTTP_FLAG_SECURE;
    }
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", request_path.c_str(), 
                                           NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           request_flags);
    if (!request) {
        DWORD error = GetLastError();
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        std::cout << "[DEBUG] ThumbnailDownloader: Failed to open request, error: " << error << std::endl;
        return "";
    }
    std::cout << "[DEBUG] ThumbnailDownloader: Request opened successfully" << std::endl;
    
    // Set timeout on request FIRST (before adding headers)
    WinHttpSetTimeouts(request, resolve_timeout, connect_timeout, send_timeout, receive_timeout);
    std::cout << "[DEBUG] ThumbnailDownloader: Request timeouts set (resolve/connect/send/receive): " 
              << resolve_timeout << "/" << connect_timeout << "/" << send_timeout << "/" << receive_timeout << " ms" << std::endl;
    
    // Build headers string (more efficient than multiple calls)
    std::wstring headers = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n";
    headers += L"Accept: image/webp,image/apng,image/*,*/*;q=0.8\r\n";
    headers += L"Referer: https://" + std::wstring(hostname) + L"/\r\n";
    headers += L"Connection: keep-alive\r\n";
    
    // Add all headers at once
    WinHttpAddRequestHeaders(request, headers.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    
    // Send request with retry logic for timeout errors
    bool send_success = false;
    int retry_count = 0;
    const int max_retries = 2;
    
    while (!send_success && retry_count <= max_retries) {
        if (retry_count > 0) {
            std::cout << "[DEBUG] ThumbnailDownloader: Retrying send request (attempt " << (retry_count + 1) << "/" << (max_retries + 1) << ")..." << std::endl;
            // Small delay before retry
            Sleep(500);
        }
        
        send_success = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        
        if (!send_success) {
            DWORD error = GetLastError();
            const char* error_msg = "Unknown error";
            switch (error) {
                case ERROR_WINHTTP_TIMEOUT: error_msg = "Timeout"; break;
                case ERROR_WINHTTP_CANNOT_CONNECT: error_msg = "Cannot connect"; break;
                case ERROR_WINHTTP_NAME_NOT_RESOLVED: error_msg = "Name not resolved"; break;
                case ERROR_WINHTTP_CONNECTION_ERROR: error_msg = "Connection error"; break;
                case ERROR_WINHTTP_SECURE_FAILURE: error_msg = "SSL/TLS failure"; break;
            }
            
            if (error == ERROR_WINHTTP_TIMEOUT && retry_count < max_retries) {
                // Retry on timeout
                retry_count++;
                continue;
            } else {
                // Other errors or max retries reached
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                std::cout << "[DEBUG] ThumbnailDownloader: Failed to send request, error: " << error << " (" << error_msg << ")" << std::endl;
                return "";
            }
        }
    }
    std::cout << "[DEBUG] ThumbnailDownloader: Request sent successfully" << std::endl;
    
    // Receive response
    if (!WinHttpReceiveResponse(request, NULL)) {
        DWORD error = GetLastError();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        std::cout << "[DEBUG] ThumbnailDownloader: Failed to receive response, error: " << error << std::endl;
        return "";
    }
    std::cout << "[DEBUG] ThumbnailDownloader: Response received successfully" << std::endl;
    
    // Check status code
    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                            NULL, &status_code, &status_code_size, NULL)) {
        std::cout << "[DEBUG] ThumbnailDownloader: HTTP status code: " << status_code << std::endl;
        if (status_code != 200) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            std::cout << "[DEBUG] ThumbnailDownloader: Non-200 status code, aborting" << std::endl;
            return "";
        }
    }
    
    // Read data
    std::vector<unsigned char> data;
    DWORD bytes_read = 0;
    char buffer[4096];
    
    do {
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &bytes_read)) {
            break;
        }
        if (bytes_read > 0) {
            data.insert(data.end(), buffer, buffer + bytes_read);
        }
    } while (bytes_read > 0);
    
    // Cleanup
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    if (data.empty()) {
        std::cout << "[DEBUG] ThumbnailDownloader: No data received" << std::endl;
        return "";
    }
    
    std::cout << "[DEBUG] ThumbnailDownloader: Downloaded " << data.size() << " bytes" << std::endl;
    return Base64::encode(data.data(), data.size());
}

#elif defined(__APPLE__)
// macOS implementation is in thumbnail_downloader_macos.mm
// This file should not be compiled on macOS
#error "thumbnail_downloader.cpp should not be compiled on macOS. Use thumbnail_downloader_macos.mm instead."

#else
// Linux and other Unix-like systems - use curl
std::string downloadThumbnailAsBase64(const std::string& thumbnail_url, bool use_proxy) {
    if (thumbnail_url.empty()) {
        std::cout << "[DEBUG] ThumbnailDownloader: Empty URL" << std::endl;
        return "";
    }
    
    std::cout << "[DEBUG] ThumbnailDownloader: Downloading from: " << thumbnail_url << std::endl;
    
    // Escape URL for shell command
    std::string escaped_url;
    for (char c : thumbnail_url) {
        if (c == '"' || c == '\\' || c == '$' || c == '`' || c == '&' || c == '|' || c == ';') {
            escaped_url += '\\';
        }
        escaped_url += c;
    }
    
    // Use curl to download thumbnail
    std::string command = "curl -s -L --max-time 10 --connect-timeout 5 \"";
    command += escaped_url;
    command += "\"";
    
    std::cout << "[DEBUG] ThumbnailDownloader: curl command: " << command << std::endl;
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cout << "[DEBUG] ThumbnailDownloader: Failed to open pipe for curl" << std::endl;
        return "";
    }
    
    std::vector<unsigned char> data;
    char buffer[4096];
    size_t total_read = 0;
    
    while (true) {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), pipe);
        if (bytes_read == 0) {
            break;
        }
        data.insert(data.end(), buffer, buffer + bytes_read);
        total_read += bytes_read;
    }
    
    int status = pclose(pipe);
    std::cout << "[DEBUG] ThumbnailDownloader: curl exit status: " << status 
              << ", bytes read: " << total_read << std::endl;
    
    if (status != 0 || data.empty()) {
        if (status != 0) {
            std::cout << "[DEBUG] ThumbnailDownloader: curl failed with status " << status << std::endl;
        }
        if (data.empty()) {
            std::cout << "[DEBUG] ThumbnailDownloader: No data received from curl" << std::endl;
        }
        return "";
    }
    
    return Base64::encode(data.data(), data.size());
}
#endif

} // namespace ThumbnailDownloader

