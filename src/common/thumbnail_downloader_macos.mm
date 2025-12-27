#include "thumbnail_downloader.h"
#include "base64.h"
#include <iostream>
#include <vector>
#include <Foundation/Foundation.h>
#include <dispatch/dispatch.h>

namespace ThumbnailDownloader {

std::string downloadThumbnailAsBase64(const std::string& thumbnail_url, bool use_proxy) {
    if (thumbnail_url.empty()) {
        std::cout << "[DEBUG] ThumbnailDownloader: Empty URL" << std::endl;
        return "";
    }
    
    std::cout << "[DEBUG] ThumbnailDownloader: Downloading from: " << thumbnail_url << std::endl;
    
    @autoreleasepool {
        NSString* urlString = [NSString stringWithUTF8String:thumbnail_url.c_str()];
        NSURL* url = [NSURL URLWithString:urlString];
        
        if (!url) {
            std::cout << "[DEBUG] ThumbnailDownloader: Invalid URL" << std::endl;
            return "";
        }
        
        // Use NSURLSession with semaphore for synchronous download
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration defaultSessionConfiguration];
        config.timeoutIntervalForRequest = 10.0;
        config.timeoutIntervalForResource = 10.0;
        
        NSURLSession* session = [NSURLSession sessionWithConfiguration:config];
        
        __block std::vector<unsigned char> downloaded_data;
        __block bool download_success = false;
        __block NSError* download_error = nil;
        
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        
        NSURLSessionDataTask* task = [session dataTaskWithURL:url completionHandler:
            ^(NSData* data, NSURLResponse* response, NSError* error) {
                if (error) {
                    download_error = error;
                    std::cout << "[DEBUG] ThumbnailDownloader: Download error: " 
                              << [[error localizedDescription] UTF8String] << std::endl;
                } else if (data && data.length > 0) {
                    const unsigned char* bytes = (const unsigned char*)[data bytes];
                    downloaded_data.assign(bytes, bytes + data.length);
                    download_success = true;
                    std::cout << "[DEBUG] ThumbnailDownloader: Downloaded " 
                              << downloaded_data.size() << " bytes" << std::endl;
                } else {
                    std::cout << "[DEBUG] ThumbnailDownloader: No data received" << std::endl;
                }
                
                dispatch_semaphore_signal(semaphore);
            }];
        
        [task resume];
        
        // Wait for download to complete (with timeout)
        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC);
        if (dispatch_semaphore_wait(semaphore, timeout) != 0) {
            std::cout << "[DEBUG] ThumbnailDownloader: Download timeout" << std::endl;
            [task cancel];
            return "";
        }
        
        if (!download_success || downloaded_data.empty()) {
            return "";
        }
        
        return Base64::encode(downloaded_data.data(), downloaded_data.size());
    }
}

} // namespace ThumbnailDownloader

