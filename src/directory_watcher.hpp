#pragma once

#include <filesystem>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <windows.h>

namespace fs = std::filesystem;

class DirectoryWatcher {
public:
    // Callback types for directory changes
    using ChangedCallback = std::function<void()>;
    
    DirectoryWatcher();
    ~DirectoryWatcher();
    
    // Start watching a directory
    bool startWatching(const fs::path& path);
    
    // Stop watching the current directory
    void stopWatching();
    
    // Set the callback for when directory changes
    void setChangedCallback(ChangedCallback callback);
    
    // Check if currently watching
    bool isWatching() const;
    
    // Get the path being watched
    fs::path getWatchPath() const;

private:
    fs::path m_watchPath;
    HANDLE m_directoryHandle;
    HANDLE m_stopEvent;
    OVERLAPPED m_overlapped;
    
    std::thread m_watcherThread;
    std::atomic<bool> m_isWatching;
    ChangedCallback m_changedCallback;
    
    // Buffer for change notifications
    static constexpr DWORD BUFFER_SIZE = 32 * 1024; // 32 KB buffer
    BYTE m_buffer[BUFFER_SIZE];
    
    // Worker thread function
    void watcherThreadFunc();
    
    // Process directory change notification
    void processNotification(DWORD bytesTransferred);
};
