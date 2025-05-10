#include "directory_watcher.hpp"
#include <format>
#include <stdexcept>

DirectoryWatcher::DirectoryWatcher()
    : m_directoryHandle(INVALID_HANDLE_VALUE)
    , m_stopEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
    , m_isWatching(false)
{
    ZeroMemory(&m_overlapped, sizeof(OVERLAPPED));
    m_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

DirectoryWatcher::~DirectoryWatcher() {
    stopWatching();
    
    if (m_overlapped.hEvent != NULL) {
        CloseHandle(m_overlapped.hEvent);
    }
    
    if (m_stopEvent != NULL) {
        CloseHandle(m_stopEvent);
    }
}

bool DirectoryWatcher::startWatching(const fs::path& path) {
    // Stop any current watching operation
    stopWatching();
    
    try {
        // Check if path is valid
        if (!fs::exists(path) || !fs::is_directory(path)) {
            return false;
        }
        
        // Store the path
        m_watchPath = path;
        
        // Open directory handle
        m_directoryHandle = CreateFileW(
            path.wstring().c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL
        );
        
        if (m_directoryHandle == INVALID_HANDLE_VALUE) {
            throw std::runtime_error(std::format("Failed to open directory: {}", path.string()));
        }
        
        // Reset stop event
        ResetEvent(m_stopEvent);
        
        // Start watcher thread
        m_isWatching = true;
        m_watcherThread = std::thread(&DirectoryWatcher::watcherThreadFunc, this);
        
        return true;
    }
    catch (const std::exception&) {
        stopWatching();
        return false;
    }
}

void DirectoryWatcher::stopWatching() {
    if (m_isWatching) {
        // Signal the thread to stop
        m_isWatching = false;
        SetEvent(m_stopEvent);
        
        // Wait for the thread to finish
        if (m_watcherThread.joinable()) {
            m_watcherThread.join();
        }
    }
    
    // Close directory handle
    if (m_directoryHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_directoryHandle);
        m_directoryHandle = INVALID_HANDLE_VALUE;
    }
}

void DirectoryWatcher::setChangedCallback(ChangedCallback callback) {
    m_changedCallback = std::move(callback);
}

bool DirectoryWatcher::isWatching() const {
    return m_isWatching;
}

fs::path DirectoryWatcher::getWatchPath() const {
    return m_watchPath;
}

void DirectoryWatcher::watcherThreadFunc() {
    DWORD bytesReturned = 0;
    
    while (m_isWatching) {
        // Start the read directory changes operation
        BOOL success = ReadDirectoryChangesW(
            m_directoryHandle,
            m_buffer,
            BUFFER_SIZE,
            TRUE,  // Watch subdirectories
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
            &bytesReturned,
            &m_overlapped,
            NULL
        );
        
        if (!success) {
            // Failed to start monitoring
            m_isWatching = false;
            break;
        }
        
        // Wait for changes or stop event
        HANDLE handles[] = { m_overlapped.hEvent, m_stopEvent };
        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        
        if (waitResult == WAIT_OBJECT_0) {
            // Directory change notification received
            if (GetOverlappedResult(m_directoryHandle, &m_overlapped, &bytesReturned, FALSE)) {
                if (bytesReturned > 0) {
                    // Process the notification
                    processNotification(bytesReturned);
                }
            }
        }
        else if (waitResult == WAIT_OBJECT_0 + 1) {
            // Stop event signaled
            break;
        }
        else {
            // Error or timeout
            m_isWatching = false;
            break;
        }
    }
}

void DirectoryWatcher::processNotification(DWORD bytesTransferred) {
    if (!m_isWatching || !m_changedCallback) {
        return;
    }
    
    BYTE* ptr = m_buffer;
    
    while (true) {
        FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(ptr);
        
        // Skip if it's a buffer overflow
        if (info->Action != FILE_ACTION_ADDED && 
            info->Action != FILE_ACTION_REMOVED && 
            info->Action != FILE_ACTION_MODIFIED && 
            info->Action != FILE_ACTION_RENAMED_OLD_NAME &&
            info->Action != FILE_ACTION_RENAMED_NEW_NAME) {
            
            // No more entries or invalid entry
            if (info->NextEntryOffset == 0) {
                break;
            }
            
            ptr += info->NextEntryOffset;
            continue;
        }
        
        // Call the notification callback
        m_changedCallback();
        
        // We only need to know that something changed, not the details
        // So we can stop processing here
        break;
    }
}
