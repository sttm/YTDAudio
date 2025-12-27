#include "platform_utils.h"
#include "../common/thumbnail_downloader.h"
#include "../common/windows_utils.h"  // For ::fileExists (cross-platform implementation)
#include <iostream>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <ole2.h>
#include <objidl.h>
#include <vector>
#include <atomic>
#include "../common/platform_macros.h"
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#ifndef CSIDL_MUSIC
#define CSIDL_MUSIC 0x000D
#endif
#ifndef CSIDL_APPDATA
#define CSIDL_APPDATA 0x001A
#endif
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#endif

namespace PlatformUtils {

#ifdef _WIN32
// Windows implementations


bool selectFolderDialog(std::string& path) {
    BROWSEINFOA bi = {0};
    bi.lpszTitle = "Select Folder";
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != nullptr) {
        char selected_path[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, selected_path)) {
            path = selected_path;
            CoTaskMemFree(pidl);
            return true;
        }
        CoTaskMemFree(pidl);
    }
    return false;
}

bool selectFolderDialogWithWindow(SDL_Window* window, std::string& path) {
    (void)window; // Windows doesn't need window handle
    return selectFolderDialog(path);
}

bool selectFileDialog(std::string& path, const std::string& file_types) {
    OPENFILENAMEA ofn = {0};
    char file_path[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "All Files\0*.*\0Text Files\0*.txt\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        path = file_path;
        return true;
    }
    return false;
}

bool selectFileDialogWithWindow(SDL_Window* window, std::string& path, const std::string& file_types) {
    (void)window; // Windows doesn't need window handle
    return selectFileDialog(path, file_types);
}

// Simple IDropSource implementation for drag & drop (Windows only)
#ifdef _WIN32
class SimpleDropSource : public IDropSource {
private:
    ULONG ref_count_;
    
public:
    SimpleDropSource() : ref_count_(1) {}
    
    // IUnknown methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IDropSource || riid == IID_IUnknown) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
    
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }
    
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return count;
    }
    
    // IDropSource methods
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) {
            return DRAGDROP_S_CANCEL;
        }
        if (!(grfKeyState & MK_LBUTTON) && !(grfKeyState & MK_RBUTTON)) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }
    
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD dwEffect) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};

// Simple IDataObject implementation for drag & drop (Windows only)
class SimpleDataObject : public IDataObject {
private:
    ULONG ref_count_;
    HGLOBAL h_global_;
    FORMATETC format_etc_;
    
public:
    SimpleDataObject(HGLOBAL hGlobal) : ref_count_(1), h_global_(hGlobal) {
        format_etc_.cfFormat = CF_HDROP;
        format_etc_.ptd = NULL;
        format_etc_.dwAspect = DVASPECT_CONTENT;
        format_etc_.lindex = -1;
        format_etc_.tymed = TYMED_HGLOBAL;
    }
    
    ~SimpleDataObject() {
        // Don't free h_global_ here - it will be freed by the caller or when drag completes
    }
    
    // IUnknown methods
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IDataObject || riid == IID_IUnknown) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
    
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&ref_count_);
    }
    
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&ref_count_);
        if (count == 0) {
            delete this;
        }
        return count;
    }
    
    // IDataObject methods
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pFormatetc, STGMEDIUM* pmedium) override {
        if (pFormatetc->cfFormat == CF_HDROP && (pFormatetc->tymed & TYMED_HGLOBAL)) {
            // Duplicate the global memory handle
            HGLOBAL hDup = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, GlobalSize(h_global_));
            if (!hDup) {
                return E_OUTOFMEMORY;
            }
            
            void* pSrc = GlobalLock(h_global_);
            void* pDst = GlobalLock(hDup);
            if (pSrc && pDst) {
                memcpy(pDst, pSrc, GlobalSize(h_global_));
            }
            if (pSrc) GlobalUnlock(h_global_);
            if (pDst) GlobalUnlock(hDup);
            
            pmedium->tymed = TYMED_HGLOBAL;
            pmedium->hGlobal = hDup;
            pmedium->pUnkForRelease = NULL;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }
    
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* pFormatetc, STGMEDIUM* pmedium) override {
        return E_NOTIMPL;
    }
    
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pFormatetc) override {
        if (pFormatetc->cfFormat == CF_HDROP && (pFormatetc->tymed & TYMED_HGLOBAL)) {
            return S_OK;
        }
        return DV_E_FORMATETC;
    }
    
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut) override {
        return E_NOTIMPL;
    }
    
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* pFormatetc, STGMEDIUM* pmedium, BOOL fRelease) override {
        return E_NOTIMPL;
    }
    
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnumFormatEtc) override {
        return E_NOTIMPL;
    }
    
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* pFormatetc, DWORD advf, IAdviseSink* pAdvSink, DWORD* pdwConnection) override {
        return E_NOTIMPL;
    }
    
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD dwConnection) override {
        return E_NOTIMPL;
    }
    
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** ppEnumAdvise) override {
        return E_NOTIMPL;
    }
};

// Static flag to prevent multiple simultaneous drag operations
static std::atomic<bool> drag_in_progress(false);
#endif

bool startFileDrag(SDL_Window* window, const std::string& file_path) {
#ifdef _WIN32
    if (file_path.empty()) {
        return false;
    }
    
    // Prevent multiple simultaneous drag operations
    bool expected = false;
    if (!drag_in_progress.compare_exchange_strong(expected, true)) {
        std::cout << "[DEBUG] startFileDrag: Drag operation already in progress" << std::endl;
        return false;
    }
    
    // Check if file exists
    if (!fileExists(file_path)) {
        drag_in_progress = false;
        std::cout << "[DEBUG] startFileDrag: File does not exist: " << file_path << std::endl;
        return false;
    }
    
    // Convert UTF-8 path to wide string
    int path_size_needed = MultiByteToWideChar(CP_UTF8, 0, file_path.c_str(), -1, NULL, 0);
    if (path_size_needed <= 0) {
        drag_in_progress = false;
        std::cout << "[DEBUG] startFileDrag: Failed to convert path to wide string" << std::endl;
        return false;
    }
    
    std::vector<wchar_t> path_wide(path_size_needed);
    MultiByteToWideChar(CP_UTF8, 0, file_path.c_str(), -1, path_wide.data(), path_size_needed);
    
    // Create DROPFILES structure
    size_t path_len = wcslen(path_wide.data());
    size_t buffer_size = sizeof(DROPFILES) + (path_len + 2) * sizeof(wchar_t);
    
    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, buffer_size);
    if (!hGlobal) {
        drag_in_progress = false;
        std::cout << "[DEBUG] startFileDrag: Failed to allocate global memory" << std::endl;
        return false;
    }
    
    void* pGlobal = GlobalLock(hGlobal);
    if (!pGlobal) {
        GlobalFree(hGlobal);
        drag_in_progress = false;
        std::cout << "[DEBUG] startFileDrag: Failed to lock global memory" << std::endl;
        return false;
    }
    
    DROPFILES* df = reinterpret_cast<DROPFILES*>(pGlobal);
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    df->fNC = FALSE;
    df->pt.x = 0;
    df->pt.y = 0;
    
    wchar_t* files = reinterpret_cast<wchar_t*>(reinterpret_cast<char*>(pGlobal) + sizeof(DROPFILES));
    wcscpy_s(files, path_len + 1, path_wide.data());
    files[path_len + 1] = L'\0';
    
    GlobalUnlock(hGlobal);
    
    // Initialize OLE
    HRESULT hr = OleInitialize(NULL);
    bool ole_initialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    
    if (!ole_initialized) {
        GlobalFree(hGlobal);
        drag_in_progress = false;
        std::cout << "[DEBUG] startFileDrag: Failed to initialize OLE" << std::endl;
        return false;
    }
    
    // Create data object and drop source
    SimpleDataObject* data_obj = new SimpleDataObject(hGlobal);
    IDataObject* pDataObject = data_obj;
    
    SimpleDropSource* drop_source = new SimpleDropSource();
    IDropSource* pDropSource = drop_source;
    
    // Perform drag & drop
    DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    HRESULT drag_result = DoDragDrop(pDataObject, pDropSource, dwEffect, &dwEffect);
    
    pDataObject->Release();
    pDropSource->Release();
    
    // Free the global memory after drag completes
    GlobalFree(hGlobal);
    
    if (ole_initialized && hr != RPC_E_CHANGED_MODE) {
        OleUninitialize();
    }
    
    bool success = false;
    if (drag_result == DRAGDROP_S_DROP || SUCCEEDED(drag_result)) {
        std::cout << "[DEBUG] startFileDrag: Drag & drop completed successfully" << std::endl;
        success = true;
    } else if (drag_result == DRAGDROP_S_CANCEL) {
        std::cout << "[DEBUG] startFileDrag: Drag & drop was cancelled" << std::endl;
        success = false;
    } else {
        std::cout << "[DEBUG] startFileDrag: Drag & drop failed, HRESULT: 0x" << std::hex << drag_result << std::dec << std::endl;
        success = false;
    }
    
    drag_in_progress = false;
    return success;
#else
    (void)window;
    (void)file_path;
    // macOS drag & drop is handled by platform_utils_macos.mm
    // Linux/Unix: not implemented
    return false;
#endif
}

void openFileLocation(const std::string& file_path) {
    if (file_path.empty()) return;
    
    // Convert UTF-8 path to UTF-16 for Windows API
    int path_size = MultiByteToWideChar(CP_UTF8, 0, file_path.c_str(), -1, NULL, 0);
    if (path_size <= 0) {
        // Fallback to ANSI if conversion fails
        ShellExecuteA(NULL, "open", "explorer.exe", ("/select,\"" + file_path + "\"").c_str(), NULL, SW_SHOWNORMAL);
        return;
    }
    
    std::wstring wide_path(path_size, 0);
    MultiByteToWideChar(CP_UTF8, 0, file_path.c_str(), -1, &wide_path[0], path_size);
    wide_path.resize(path_size - 1);
    
    // Build command with proper escaping for paths with spaces
    std::wstring command = L"/select,\"" + wide_path + L"\"";
    
    // Use Unicode version of ShellExecute
    ShellExecuteW(NULL, L"open", L"explorer.exe", command.c_str(), NULL, SW_SHOWNORMAL);
}

void openFolder(const std::string& folder_path) {
    if (folder_path.empty()) return;
    
    // Convert UTF-8 path to UTF-16 for Windows API
    int path_size = MultiByteToWideChar(CP_UTF8, 0, folder_path.c_str(), -1, NULL, 0);
    if (path_size <= 0) {
        // Fallback to ANSI if conversion fails
        ShellExecuteA(NULL, "open", folder_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return;
    }
    
    std::wstring wide_path(path_size, 0);
    MultiByteToWideChar(CP_UTF8, 0, folder_path.c_str(), -1, &wide_path[0], path_size);
    wide_path.resize(path_size - 1);
    
    // Use Unicode version of ShellExecute
    ShellExecuteW(NULL, L"open", wide_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

std::string getConfigPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path))) {
        std::string config_dir = std::string(path) + "\\YTDAudio";
        CreateDirectoryA(config_dir.c_str(), NULL);
        return config_dir + "\\config.txt";
    }
    return "config.txt";
}

std::string getHistoryPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path))) {
        std::string config_dir = std::string(path) + "\\YTDAudio";
        CreateDirectoryA(config_dir.c_str(), NULL);
        return config_dir + "\\history.json";
    }
    return "history.json";
}

std::string getDownloadsPath() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_MUSIC, NULL, SHGFP_TYPE_CURRENT, path))) {
        return std::string(path) + "\\YTDAudio";
    }
    return ".";
}

std::string getExecutablePath() {
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::string(path);
    }
    return "";
}

std::string getExecutableDirectory() {
    std::string exe_path = getExecutablePath();
    size_t last_slash = exe_path.find_last_of("\\/");
    if (last_slash != std::string::npos) {
        return exe_path.substr(0, last_slash);
    }
    return "";
}

bool fileExists(const std::string& file_path) {
    // Delegate to cross-platform implementation in windows_utils.h
    return ::fileExists(file_path);
}

bool createDirectory(const std::string& path) {
    if (path.empty()) return false;
    return CreateDirectoryA(path.c_str(), NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

#else
// Linux/Unix implementations (fallback)


bool selectFolderDialog(std::string& path) {
    // Linux file dialogs would require GTK/Qt integration
    // For now, return false
    (void)path;
    return false;
}

bool selectFolderDialogWithWindow(SDL_Window* window, std::string& path) {
    (void)window;
    return selectFolderDialog(path);
}

bool selectFileDialog(std::string& path, const std::string& file_types) {
    (void)path;
    (void)file_types;
    return false;
}

bool selectFileDialogWithWindow(SDL_Window* window, std::string& path, const std::string& file_types) {
    (void)window;
    return selectFileDialog(path, file_types);
}

bool startFileDrag(SDL_Window* window, const std::string& file_path) {
    (void)window;
    (void)file_path;
    return false;
}

void openFileLocation(const std::string& file_path) {
    std::string command = "xdg-open \"" + file_path + "\"";
    system(command.c_str());
}

void openFolder(const std::string& folder_path) {
    std::string command = "xdg-open \"" + folder_path + "\"";
    system(command.c_str());
}

std::string getConfigPath() {
    const char* home = getenv("HOME");
    if (home) {
        std::string config_dir = std::string(home) + "/.config/ytdaudio";
        mkdir(config_dir.c_str(), 0755);
        return config_dir + "/config.txt";
    }
    return "config.txt";
}

std::string getHistoryPath() {
    const char* home = getenv("HOME");
    if (home) {
        std::string config_dir = std::string(home) + "/.config/ytdaudio";
        mkdir(config_dir.c_str(), 0755);
        return config_dir + "/history.json";
    }
    return "history.json";
}

std::string getDownloadsPath() {
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/Music/YTDAudio";
    }
    return ".";
}

std::string getExecutablePath() {
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    if (count != -1) {
        path[count] = '\0';
        return std::string(path);
    }
    return "";
}

std::string getExecutableDirectory() {
    std::string exe_path = getExecutablePath();
    size_t last_slash = exe_path.find_last_of("/");
    if (last_slash != std::string::npos) {
        return exe_path.substr(0, last_slash);
    }
    return "";
}

bool fileExists(const std::string& file_path) {
    // Delegate to cross-platform implementation in windows_utils.h
    return ::fileExists(file_path);
}

bool createDirectory(const std::string& path) {
    if (path.empty()) return false;
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

#endif

std::string downloadThumbnailAsBase64(const std::string& thumbnail_url, bool use_proxy) {
    // Delegate to ThumbnailDownloader namespace which has proper Windows (WinHttp) and macOS implementations
    // This ensures consistent behavior across platforms
    return ThumbnailDownloader::downloadThumbnailAsBase64(thumbnail_url, use_proxy);
}

void openURL(const std::string& url) {
    if (url.empty()) return;
    
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    // Linux/Unix: use xdg-open
    std::string command = "xdg-open \"" + url + "\"";
    system(command.c_str());
#endif
}

} // namespace PlatformUtils

