#include "file_explorer.hpp"
#include <algorithm>
#include <codecvt>
#include <locale>
#include <format>
#include <shlobj_core.h>

FileExplorer::FileExplorer() : m_currentPath(fs::current_path()) {
    m_dirWatcher = std::make_unique<DirectoryWatcher>();
    m_dirWatcher->setChangedCallback([this]() { onDirectoryChanged(); });
    
    // Initialize quick access locations
    WCHAR path[MAX_PATH];
    
    // Documents
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, path))) {
        m_quickAccessLocations.push_back(path);
    }
    
    // Desktop
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, 0, path))) {
        m_quickAccessLocations.push_back(path);
    }
    
    // Downloads
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path))) {
        m_quickAccessLocations.push_back(std::wstring(path) + L"\\Downloads");
    }
    
    // Pictures
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYPICTURES, NULL, 0, path))) {
        m_quickAccessLocations.push_back(path);
    }
    
    // Load files initially
    navigateTo(m_currentPath);
}

FileExplorer::~FileExplorer() {
    if (m_loadingThread.joinable()) {
        m_isLoading = false;
        m_loadingThread.join();
    }
    m_dirWatcher->stopWatching();
}

void FileExplorer::navigateTo(const fs::path& path) {
    try {
        // Check if path exists and is a directory
        if (!fs::exists(path) || !fs::is_directory(path)) {
            if (m_onError) {
                m_onError(std::format("Path '{}' is not a valid directory", path.string()));
            }
            return;
        }
        
        // Stop watching the current directory
        m_dirWatcher->stopWatching();
        
        // Update current path
        m_currentPath = fs::canonical(path);
        
        // Start watching the new directory
        m_dirWatcher->startWatching(m_currentPath);
        
        // Load files
        loadFiles();
    }
    catch (const fs::filesystem_error& e) {
        if (m_onError) {
            m_onError(std::format("Error navigating to path: {}", e.what()));
        }
    }
}

void FileExplorer::navigateUp() {
    fs::path parent = m_currentPath.parent_path();
    if (parent != m_currentPath) {
        navigateTo(parent);
    }
}

fs::path FileExplorer::getCurrentPath() const {
    return m_currentPath;
}

void FileExplorer::refresh() {
    loadFiles();
}

void FileExplorer::search(const std::string& query) {
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    std::vector<FileItem> results;
    std::string lowercaseQuery = query;
    std::transform(lowercaseQuery.begin(), lowercaseQuery.end(), lowercaseQuery.begin(), ::tolower);
    
    for (const auto& item : m_files) {
        std::string lowercaseName = item.name;
        std::transform(lowercaseName.begin(), lowercaseName.end(), lowercaseName.begin(), ::tolower);
        
        if (lowercaseName.find(lowercaseQuery) != std::string::npos) {
            results.push_back(item);
        }
    }
    
    if (m_onFilesLoaded) {
        m_onFilesLoaded(results);
    }
}

void FileExplorer::filterByExtension(const std::vector<std::string>& extensions) {
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    if (extensions.empty()) {
        // No filter
        if (m_onFilesLoaded) {
            m_onFilesLoaded(m_files);
        }
        return;
    }
    
    std::vector<FileItem> filtered;
    for (const auto& item : m_files) {
        std::string ext = item.path.extension().string();
        if (!ext.empty()) {
            // Remove the dot
            ext = ext.substr(1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                filtered.push_back(item);
            }
        }
    }
    
    if (m_onFilesLoaded) {
        m_onFilesLoaded(filtered);
    }
}

void FileExplorer::sortFiles(SortCriteria criteria, SortOrder order) {
    std::lock_guard<std::mutex> lock(m_filesMutex);
    
    auto sortFunction = [criteria, order](const FileItem& a, const FileItem& b) {
        bool result = false;
        
        // First sort by directory/file
        if (fs::is_directory(a.path) && !fs::is_directory(b.path)) {
            return true;
        }
        if (!fs::is_directory(a.path) && fs::is_directory(b.path)) {
            return false;
        }
        
        // Then sort by the specified criteria
        switch (criteria) {
            case SortCriteria::Name:
                result = a.name < b.name;
                break;
            case SortCriteria::Size:
                result = a.size < b.size;
                break;
            case SortCriteria::Type:
                result = a.path.extension().string() < b.path.extension().string();
                break;
            case SortCriteria::Date:
                result = a.last_write_time < b.last_write_time;
                break;
        }
        
        // Apply sort order
        return (order == SortOrder::Ascending) ? result : !result;
    };
    
    std::sort(m_files.begin(), m_files.end(), sortFunction);
    
    if (m_onFilesLoaded) {
        m_onFilesLoaded(m_files);
    }
}

void FileExplorer::setOnFilesLoadedCallback(FileItemCallback callback) {
    m_onFilesLoaded = std::move(callback);
}

void FileExplorer::setOnErrorCallback(ErrorCallback callback) {
    m_onError = std::move(callback);
}

std::vector<fs::path> FileExplorer::getQuickAccessLocations() const {
    return m_quickAccessLocations;
}

void FileExplorer::addQuickAccessLocation(const fs::path& path) {
    if (std::find(m_quickAccessLocations.begin(), m_quickAccessLocations.end(), path) == m_quickAccessLocations.end()) {
        m_quickAccessLocations.push_back(path);
    }
}

void FileExplorer::removeQuickAccessLocation(const fs::path& path) {
    auto it = std::find(m_quickAccessLocations.begin(), m_quickAccessLocations.end(), path);
    if (it != m_quickAccessLocations.end()) {
        m_quickAccessLocations.erase(it);
    }
}

void FileExplorer::loadFiles() {
    // Cancel any ongoing loading operation
    if (m_loadingThread.joinable()) {
        m_isLoading = false;
        m_loadingThread.join();
    }
    
    // Start new loading thread
    m_isLoading = true;
    m_loadingThread = std::thread([this]() {
        std::vector<FileItem> newFiles;
        
        try {
            // Use Windows API for faster directory enumeration
            WIN32_FIND_DATAW findData;
            std::wstring searchPath = m_currentPath.wstring() + L"\\*";
            
            HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    // Check if the operation was canceled
                    if (!m_isLoading) {
                        FindClose(hFind);
                        return;
                    }
                    
                    std::wstring filename = findData.cFileName;
                    if (filename != L"." && filename != L"..") {
                        FileItem item;
                        
                        // Convert wide string to utf8
                        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
                        item.name = converter.to_bytes(filename);
                        
                        // Set file path
                        item.path = m_currentPath / filename;
                        
                        // Get file type
                        item.type = fs::is_directory(item.path) ? fs::file_type::directory : fs::file_type::regular;
                        
                        // Get file size
                        if (item.type == fs::file_type::directory) {
                            item.size = 0; // Don't calculate directory size for performance
                        } else {
                            LARGE_INTEGER fileSize;
                            fileSize.LowPart = findData.nFileSizeLow;
                            fileSize.HighPart = findData.nFileSizeHigh;
                            item.size = fileSize.QuadPart;
                        }
                        
                        // Get last write time
                        FILETIME localFileTime;
                        FileTimeToLocalFileTime(&findData.ftLastWriteTime, &localFileTime);
                        
                        SYSTEMTIME systemTime;
                        FileTimeToSystemTime(&localFileTime, &systemTime);
                        
                        std::tm tm = {};
                        tm.tm_year = systemTime.wYear - 1900;
                        tm.tm_mon = systemTime.wMonth - 1;
                        tm.tm_mday = systemTime.wDay;
                        tm.tm_hour = systemTime.wHour;
                        tm.tm_min = systemTime.wMinute;
                        tm.tm_sec = systemTime.wSecond;
                        
                        item.last_write_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));
                        
                        // Add the file to our list
                        newFiles.push_back(item);
                    }
                } while (FindNextFileW(hFind, &findData) != 0 && m_isLoading);
                
                FindClose(hFind);
            }
            
            // Sort files (directories first, then by name)
            std::sort(newFiles.begin(), newFiles.end(), [](const FileItem& a, const FileItem& b) {
                bool aIsDir = fs::is_directory(a.path);
                bool bIsDir = fs::is_directory(b.path);
                
                if (aIsDir && !bIsDir) return true;
                if (!aIsDir && bIsDir) return false;
                
                return a.name < b.name;
            });
            
            // Update the file list and notify
            {
                std::lock_guard<std::mutex> lock(m_filesMutex);
                m_files = std::move(newFiles);
            }
            
            if (m_onFilesLoaded && m_isLoading) {
                m_onFilesLoaded(m_files);
            }
        }
        catch (const std::exception& e) {
            if (m_onError && m_isLoading) {
                m_onError(std::format("Error loading files: {}", e.what()));
            }
        }
        
        m_isLoading = false;
    });
    
    // Detach the thread to allow it to run in the background
    m_loadingThread.detach();
}

void FileExplorer::processFiles() {
    // This method can be used to apply filters and sorting
    if (m_onFilesLoaded) {
        std::lock_guard<std::mutex> lock(m_filesMutex);
        m_onFilesLoaded(m_files);
    }
}

void FileExplorer::onDirectoryChanged() {
    // Automatically refresh when directory changes
    refresh();
}
                