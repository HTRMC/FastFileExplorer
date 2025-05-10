#include "file_system_utils.hpp"
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <comdef.h>
#include <format>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <array>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

namespace FileSystemUtils {

namespace {
    // Thread-safe cache for icons
    std::unordered_map<std::string, HICON> g_iconCache;
    std::mutex g_iconCacheMutex;
    
    // Thread-safe cache for file attributes
    std::unordered_map<std::string, FileAttributes> g_attributesCache;
    std::mutex g_attributesCacheMutex;
    
    // Thread-safe cache for file type descriptions
    std::unordered_map<std::string, std::string> g_fileTypeCache;
    std::mutex g_fileTypeCacheMutex;
    
    // Convert wide string to UTF-8
    std::string wideToUtf8(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }
    
    // Convert UTF-8 to wide string
    std::wstring utf8ToWide(const std::string& str) {
        if (str.empty()) return std::wstring();
        
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &wstrTo[0], size_needed);
        return wstrTo;
    }
}

std::vector<fs::path> getLogicalDrives() {
    std::vector<fs::path> drives;
    
    // Get bitmask of available drives
    DWORD driveMask = GetLogicalDrives();
    
    if (driveMask == 0) {
        return drives;
    }
    
    // Process drive mask
    for (char drive = 'A'; drive <= 'Z'; drive++) {
        if ((driveMask & 1) == 1) {
            std::string drivePath = std::format("{}:\\", drive);
            drives.push_back(drivePath);
        }
        driveMask >>= 1;
    }
    
    return drives;
}

HICON getFileIcon(const fs::path& path, bool largeIcon) {
    std::string cacheKey = path.string() + (largeIcon ? "_large" : "_small");
    
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(g_iconCacheMutex);
        auto it = g_iconCache.find(cacheKey);
        if (it != g_iconCache.end()) {
            return it->second;
        }
    }
    
    // Get icon from shell
    SHFILEINFOW shFileInfo = {};
    UINT flags = SHGFI_ICON | (largeIcon ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    
    if (fs::is_directory(path)) {
        flags |= SHGFI_USEFILEATTRIBUTES;
    }
    
    if (SHGetFileInfoW(path.wstring().c_str(), 0, &shFileInfo, sizeof(shFileInfo), flags)) {
        // Add to cache
        std::lock_guard<std::mutex> lock(g_iconCacheMutex);
        g_iconCache[cacheKey] = shFileInfo.hIcon;
        return shFileInfo.hIcon;
    }
    
    return NULL;
}

FileAttributes getFileAttributes(const fs::path& path) {
    std::string pathStr = path.string();
    
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(g_attributesCacheMutex);
        auto it = g_attributesCache.find(pathStr);
        if (it != g_attributesCache.end()) {
            return it->second;
        }
    }
    
    DWORD attributes = GetFileAttributesW(path.wstring().c_str());
    
    FileAttributes result = {
        (attributes & FILE_ATTRIBUTE_HIDDEN) != 0,
        (attributes & FILE_ATTRIBUTE_SYSTEM) != 0,
        (attributes & FILE_ATTRIBUTE_ARCHIVE) != 0,
        (attributes & FILE_ATTRIBUTE_READONLY) != 0,
        (attributes & FILE_ATTRIBUTE_COMPRESSED) != 0,
        (attributes & FILE_ATTRIBUTE_ENCRYPTED) != 0
    };
    
    // Add to cache
    std::lock_guard<std::mutex> lock(g_attributesCacheMutex);
    g_attributesCache[pathStr] = result;
    
    return result;
}

uintmax_t getFileSize(const fs::path& path) {
    // Use fast Win32 API
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &fileInfo)) {
        LARGE_INTEGER size;
        size.LowPart = fileInfo.nFileSizeLow;
        size.HighPart = fileInfo.nFileSizeHigh;
        return size.QuadPart;
    }
    
    // Fallback to std::filesystem (slower)
    try {
        return fs::file_size(path);
    }
    catch (const fs::filesystem_error&) {
        return 0;
    }
}

std::chrono::system_clock::time_point fileTimeToSystemTime(const FILETIME& fileTime) {
    SYSTEMTIME systemTime;
    FILETIME localFileTime;
    
    FileTimeToLocalFileTime(&fileTime, &localFileTime);
    FileTimeToSystemTime(&localFileTime, &systemTime);
    
    std::tm tm = {};
    tm.tm_year = systemTime.wYear - 1900;
    tm.tm_mon = systemTime.wMonth - 1;
    tm.tm_mday = systemTime.wDay;
    tm.tm_hour = systemTime.wHour;
    tm.tm_min = systemTime.wMinute;
    tm.tm_sec = systemTime.wSecond;
    
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string formatFileSize(uintmax_t size) {
    constexpr std::array<const char*, 5> suffixes = { "B", "KB", "MB", "GB", "TB" };
    
    double dblSize = static_cast<double>(size);
    int suffixIndex = 0;
    
    while (dblSize >= 1024.0 && suffixIndex < suffixes.size() - 1) {
        dblSize /= 1024.0;
        suffixIndex++;
    }
    
    // Format with appropriate precision
    if (suffixIndex == 0) {
        return std::format("{} {}", size, suffixes[suffixIndex]);
    }
    else if (dblSize < 10) {
        return std::format("{:.2f} {}", dblSize, suffixes[suffixIndex]);
    }
    else if (dblSize < 100) {
        return std::format("{:.1f} {}", dblSize, suffixes[suffixIndex]);
    }
    else {
        return std::format("{:.0f} {}", dblSize, suffixes[suffixIndex]);
    }
}

std::string getFileTypeDescription(const fs::path& path) {
    std::string ext = path.extension().string();
    if (ext.empty()) {
        if (fs::is_directory(path)) {
            return "File folder";
        }
        return "File";
    }
    
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(g_fileTypeCacheMutex);
        auto it = g_fileTypeCache.find(ext);
        if (it != g_fileTypeCache.end()) {
            return it->second;
        }
    }
    
    // Get description from system
    SHFILEINFOW shFileInfo = {};
    if (SHGetFileInfoW(path.wstring().c_str(), 0, &shFileInfo, sizeof(shFileInfo), SHGFI_TYPENAME)) {
        std::string description = wideToUtf8(shFileInfo.szTypeName);
        
        // Add to cache
        std::lock_guard<std::mutex> lock(g_fileTypeCacheMutex);
        g_fileTypeCache[ext] = description;
        
        return description;
    }
    
    // Fallback
    return std::format("{} File", ext.substr(1));
}

std::vector<WIN32_FIND_DATAW> enumerateDirectory(const fs::path& path) {
    std::vector<WIN32_FIND_DATAW> results;
    
    WIN32_FIND_DATAW findData;
    std::wstring searchPath = path.wstring() + L"\\*";
    
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::wstring filename = findData.cFileName;
            if (filename != L"." && filename != L"..") {
                results.push_back(findData);
            }
        } while (FindNextFileW(hFind, &findData) != 0);
        
        FindClose(hFind);
    }
    
    return results;
}

std::future<size_t> enumerateDirectoryAsync(const fs::path& path, DirectoryEnumCallback callback) {
    return std::async(std::launch::async, [path, callback]() {
        size_t count = 0;
        
        WIN32_FIND_DATAW findData;
        std::wstring searchPath = path.wstring() + L"\\*";
        
        HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                std::wstring filename = findData.cFileName;
                if (filename != L"." && filename != L"..") {
                    callback(findData);
                    count++;
                }
            } while (FindNextFileW(hFind, &findData) != 0);
            
            FindClose(hFind);
        }
        
        return count;
    });
}

FileMetadata getFileMetadata(const fs::path& path) {
    FileMetadata metadata;
    metadata.name = path.filename().string();
    metadata.extension = path.extension().string();
    
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &fileInfo)) {
        // File size
        LARGE_INTEGER size;
        size.LowPart = fileInfo.nFileSizeLow;
        size.HighPart = fileInfo.nFileSizeHigh;
        metadata.size = size.QuadPart;
        
        // Times
        metadata.creationTime = fileTimeToSystemTime(fileInfo.ftCreationTime);
        metadata.lastAccessTime = fileTimeToSystemTime(fileInfo.ftLastAccessTime);
        metadata.lastWriteTime = fileTimeToSystemTime(fileInfo.ftLastWriteTime);
        
        // Attributes
        metadata.attributes.isHidden = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        metadata.attributes.isSystem = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        metadata.attributes.isArchive = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0;
        metadata.attributes.isReadOnly = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        metadata.attributes.isCompressed = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0;
        metadata.attributes.isEncrypted = (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0;
    }
    
    return metadata;
}

bool exists(const fs::path& path) {
    // Faster than fs::exists
    return GetFileAttributesW(path.wstring().c_str()) != INVALID_FILE_ATTRIBUTES;
}

MemoryMappedFile::MemoryMappedFile(const fs::path& path)
    : m_fileHandle(INVALID_HANDLE_VALUE)
    , m_mappingHandle(NULL)
    , m_mappedData(NULL)
    , m_size(0)
{
    // Open file
    m_fileHandle = CreateFileW(
        path.wstring().c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (m_fileHandle == INVALID_HANDLE_VALUE) {
        return;
    }
    
    // Get file size
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(m_fileHandle, &fileSize)) {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        return;
    }
    
    m_size = fileSize.QuadPart;
    
    // Create file mapping
    m_mappingHandle = CreateFileMappingW(
        m_fileHandle,
        NULL,
        PAGE_READONLY,
        0,
        0,
        NULL
    );
    
    if (m_mappingHandle == NULL) {
        CloseHandle(m_fileHandle);
        m_fileHandle = INVALID_HANDLE_VALUE;
        return;
    }
    
    // Map view of file
    m_mappedData = MapViewOfFile(
        m_mappingHandle,
        FILE_MAP_READ,
        0,
        0,
        0
    );
    
    if (m_mappedData == NULL) {
        CloseHandle(m_mappingHandle);
        CloseHandle(m_fileHandle);
        m_mappingHandle = NULL;
        m_fileHandle = INVALID_HANDLE_VALUE;
    }
}

MemoryMappedFile::~MemoryMappedFile() {
    if (m_mappedData != NULL) {
        UnmapViewOfFile(m_mappedData);
    }
    
    if (m_mappingHandle != NULL) {
        CloseHandle(m_mappingHandle);
    }
    
    if (m_fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_fileHandle);
    }
}

bool MemoryMappedFile::isOpen() const {
    return m_mappedData != NULL;
}

size_t MemoryMappedFile::size() const {
    return m_size;
}

const void* MemoryMappedFile::data() const {
    return m_mappedData;
}

} // namespace FileSystemUtils
