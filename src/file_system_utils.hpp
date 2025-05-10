#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <optional>
#include <future>
#include <windows.h>

namespace fs = std::filesystem;

namespace FileSystemUtils {
    // Get drive letters available on the system
    std::vector<fs::path> getLogicalDrives();
    
    // Get file icon for a given path (returns HICON)
    HICON getFileIcon(const fs::path& path, bool largeIcon = false);
    
    // Get file attributes quickly (cached when possible)
    struct FileAttributes {
        bool isHidden;
        bool isSystem;
        bool isArchive;
        bool isReadOnly;
        bool isCompressed;
        bool isEncrypted;
    };
    FileAttributes getFileAttributes(const fs::path& path);
    
    // Fast file size calculation (uses native API for performance)
    uintmax_t getFileSize(const fs::path& path);
    
    // Convert file time to system time
    std::chrono::system_clock::time_point fileTimeToSystemTime(const FILETIME& fileTime);
    
    // Format file size in human-readable format
    std::string formatFileSize(uintmax_t size);
    
    // Get file type description
    std::string getFileTypeDescription(const fs::path& path);
    
    // Fast directory enumeration (uses native Windows API)
    std::vector<WIN32_FIND_DATAW> enumerateDirectory(const fs::path& path);
    
    // Async directory enumeration
    using DirectoryEnumCallback = std::function<void(const WIN32_FIND_DATAW&)>;
    std::future<size_t> enumerateDirectoryAsync(const fs::path& path, DirectoryEnumCallback callback);
    
    // Extract file metadata quickly
    struct FileMetadata {
        std::string name;
        std::string extension;
        uintmax_t size;
        std::chrono::system_clock::time_point creationTime;
        std::chrono::system_clock::time_point lastAccessTime;
        std::chrono::system_clock::time_point lastWriteTime;
        FileAttributes attributes;
    };
    FileMetadata getFileMetadata(const fs::path& path);
    
    // Fast file existence check
    bool exists(const fs::path& path);
    
    // Memory-mapped file reading (for very fast access to file contents)
    class MemoryMappedFile {
    public:
        MemoryMappedFile(const fs::path& path);
        ~MemoryMappedFile();
        
        bool isOpen() const;
        size_t size() const;
        const void* data() const;
        
    private:
        HANDLE m_fileHandle;
        HANDLE m_mappingHandle;
        void* m_mappedData;
        size_t m_size;
    };
}
