#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>

#include "directory_watcher.hpp"
#include "file_system_utils.hpp"

namespace fs = std::filesystem;

class FileExplorer {
public:
    struct FileItem {
        std::string name;
        fs::path path;
        fs::file_type type;
        uintmax_t size;
        std::chrono::system_clock::time_point last_write_time;
        
        bool operator==(const FileItem& other) const {
            return path == other.path;
        }
    };

    using FileItemCallback = std::function<void(const std::vector<FileItem>&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    FileExplorer();
    ~FileExplorer();

    // Navigate to a specific path
    void navigateTo(const fs::path& path);
    
    // Go up one directory level
    void navigateUp();
    
    // Get current path
    fs::path getCurrentPath() const;
    
    // Refresh current directory
    void refresh();
    
    // Search for files in current directory
    void search(const std::string& query);
    
    // Filter files by extension
    void filterByExtension(const std::vector<std::string>& extensions);
    
    // Sort files by various criteria
    enum class SortCriteria { Name, Size, Type, Date };
    enum class SortOrder { Ascending, Descending };
    void sortFiles(SortCriteria criteria, SortOrder order);
    
    // Set callbacks
    void setOnFilesLoadedCallback(FileItemCallback callback);
    void setOnErrorCallback(ErrorCallback callback);
    
    // Quick access locations
    std::vector<fs::path> getQuickAccessLocations() const;
    void addQuickAccessLocation(const fs::path& path);
    void removeQuickAccessLocation(const fs::path& path);

private:
    fs::path m_currentPath;
    std::vector<FileItem> m_files;
    std::unique_ptr<DirectoryWatcher> m_dirWatcher;
    FileItemCallback m_onFilesLoaded;
    ErrorCallback m_onError;
    std::vector<fs::path> m_quickAccessLocations;
    
    std::mutex m_filesMutex;
    std::atomic<bool> m_isLoading{false};
    std::thread m_loadingThread;
    
    // Load files from current directory
    void loadFiles();
    
    // Apply filters and sorting
    void processFiles();
    
    // Handle directory change notifications
    void onDirectoryChanged();
};
