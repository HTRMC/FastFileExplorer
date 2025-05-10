#include <windows.h>
#include <commctrl.h>
#include <cwctype>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <filesystem>
#include <format>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <Uxtheme.h>
#include <algorithm>
#include <atomic>

// Link with required libraries
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;
using namespace std::literals::chrono_literals;

// Window class name
constexpr wchar_t WINDOW_CLASS_NAME[] = L"SimpleFileExplorer";

// Control IDs
constexpr int ID_FILE_LIST = 100;
constexpr int ID_BACK_BUTTON = 101;
constexpr int ID_FORWARD_BUTTON = 102;
constexpr int ID_ADDRESS_BAR = 103;
constexpr int ID_GO_BUTTON = 104;
constexpr int ID_SEARCH_BOX = 105;
constexpr int ID_SEARCH_BUTTON = 106;
constexpr int ID_STOP_SEARCH_BUTTON = 107;

// UI constants
constexpr int ICON_SIZE = 16; // Standard small icon size in Windows 11
constexpr int BUTTON_WIDTH = 32; // Slightly wider for better touch targets
constexpr int BUTTON_HEIGHT = 32; // Square buttons look more modern
constexpr int UI_PADDING = 10; // Standard padding between elements

// Search status
constexpr int WM_SEARCH_RESULT = WM_USER + 1;
constexpr int WM_SEARCH_COMPLETE = WM_USER + 2;
constexpr int WM_SEARCH_PROGRESS = WM_USER + 3;

// Colors
constexpr COLORREF DARK_GRAY = RGB(64, 64, 64); // Dark gray color for button backgrounds
constexpr COLORREF BUTTON_TEXT_COLOR = RGB(255, 255, 255); // White text for buttons

// Special paths
constexpr wchar_t THIS_PC_NAME[] = L"This PC";

// Search thread pool size
constexpr int MAX_SEARCH_THREADS = 8;

HICON g_hBackIcon = NULL;
HICON g_hForwardIcon = NULL;
HICON g_hSearchIcon = NULL;
HBRUSH g_hButtonBrush = NULL; // Brush for button background

// Custom button class name
constexpr wchar_t CUSTOM_BUTTON_CLASS[] = L"FastFileExplorerButton";

// Global variables
HWND g_hwndMain = NULL;
HWND g_hwndListView = NULL;
HWND g_hwndAddressBar = NULL;
HWND g_hwndBackButton = NULL;
HWND g_hwndForwardButton = NULL;
HWND g_hwndGoButton = NULL;
HWND g_hwndSearchBox = NULL;
HWND g_hwndSearchButton = NULL;
HWND g_hwndStatusBar = NULL;
HWND g_hwndStopSearchButton = NULL;
HFONT g_hFont = NULL;
fs::path g_currentPath;

// Original window procedure for the address bar
WNDPROC g_oldAddressBarProc = NULL;
WNDPROC g_oldBackButtonProc = NULL;
WNDPROC g_oldForwardButtonProc = NULL;
WNDPROC g_oldSearchBoxProc = NULL;

// Navigation history
std::deque<fs::path> g_backHistory;
std::deque<fs::path> g_forwardHistory;
bool g_navigatingHistory = false;

// Search related variables
std::atomic<bool> g_isSearching = false;
std::atomic<int> g_filesSearched = 0;
std::atomic<int> g_filesFound = 0;
std::atomic<int> g_directoriesSearched = 0;
std::vector<std::jthread> g_searchThreads;
std::mutex g_resultsMutex;
std::vector<fs::path> g_searchResults;
std::string g_searchTerm;
fs::path g_searchRootPath;
std::condition_variable g_stopSearchCV;
std::mutex g_stopSearchMutex;

class OptimizedThreadPool {
public:
    OptimizedThreadPool(size_t threads) : stop(false) {
        workers.reserve(threads);
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this, i] {
                // Each worker gets its own task queue to reduce lock contention
                size_t workerIndex = i;

                while (true) {
                    std::function<void()> task;
                    bool hasTask = false;

                    {
                        // First try to get a task from this worker's preferred queue
                        std::unique_lock<std::mutex> lock(this->queue_mutex);

                        // Check if we should stop
                        if (this->stop && this->global_tasks.empty()) {
                            return;
                        }

                        // Try to get task from global queue
                        if (!this->global_tasks.empty()) {
                            task = std::move(this->global_tasks.front());
                            this->global_tasks.pop();
                            hasTask = true;
                        }
                    }

                    if (hasTask) {
                        try {
                            task();
                        }
                        catch (const std::exception& e) {
                            // Log but don't crash
                            OutputDebugStringA(("ThreadPool exception: " + std::string(e.what())).c_str());
                        }
                    } else {
                        // If no tasks, sleep briefly to reduce CPU usage
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            });
        }
    }

    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) return;
            global_tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~OptimizedThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> global_tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// ThreadPool class for search operations
class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false) {
        for (size_t i = 0; i < threads; ++i)
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });

                        if (this->stop && this->tasks.empty())
                            return;

                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
    }

    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers)
            worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK AddressBarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK CustomButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
HFONT CreateSegoeUIFont(int size, bool bold = false);
bool CreateListView(HWND hwndParent);
void PopulateListView(const fs::path& path);
void NavigateTo(const fs::path& path, bool addToHistory = true);
void NavigateBack();
void NavigateForward();
std::vector<fs::path> EnumerateDrives();
std::wstring GetFileTypeDescription(const fs::path& path);
std::wstring FormatFileSize(uintmax_t size);
void UpdateNavigationButtons();
void ApplyFontToAllControls();
void EnableWindowTheme(HWND hwnd, LPCWSTR classList, LPCWSTR subApp);
HWND CreateCustomButton(HWND hwndParent, int x, int y, int width, int height, int id, HINSTANCE hInstance);
void SearchFiles(const fs::path& rootPath, const std::wstring& searchTerm);
void DisplaySearchResults();
void ClearSearchResults();
LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void SearchDirectoryRecursive(const fs::path& dirPath, const std::wstring& searchTerm,
                             ThreadPool& pool, std::atomic<bool>& isSearching);

// Create a custom button with dark gray background
HWND CreateCustomButton(HWND hwndParent, int x, int y, int width, int height, int id, HINSTANCE hInstance)
{
    return CreateWindowExW(
        0,
        CUSTOM_BUTTON_CLASS,
        NULL,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, width, height,
        hwndParent,
        (HMENU)(INT_PTR)id,
        hInstance,
        NULL
    );
}

// Create Segoe UI font with specified size
HFONT CreateSegoeUIFont(int size, bool bold)
{
    return CreateFontW(
        size, // Height
        0, // Width
        0, // Escapement
        0, // Orientation
        bold ? FW_BOLD : FW_NORMAL, // Weight
        FALSE, // Italic
        FALSE, // Underline
        FALSE, // StrikeOut
        DEFAULT_CHARSET, // CharSet
        OUT_DEFAULT_PRECIS, // OutPrecision
        CLIP_DEFAULT_PRECIS, // ClipPrecision
        CLEARTYPE_QUALITY, // Quality (using ClearType for better appearance)
        DEFAULT_PITCH | FF_DONTCARE, // Pitch And Family
        L"Segoe UI" // Font Name
    );
}

// Convert string to lowercase for case-insensitive comparison
std::wstring ToLowerCase(const std::wstring& str) {
    std::wstring lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return lowerStr;
}

// Replace all occurrences of a substring (case insensitive)
void CaseInsensitiveReplace(std::wstring& str, const std::wstring& from, const std::wstring& to) {
    std::wstring lowerStr = ToLowerCase(str);
    std::wstring lowerFrom = ToLowerCase(from);

    size_t pos = 0;
    while ((pos = lowerStr.find(lowerFrom, pos)) != std::wstring::npos) {
        str.replace(pos, from.length(), to);
        lowerStr.replace(pos, lowerFrom.length(), to);
        pos += to.length();
    }
}

// Check if a filename matches the search term
bool MatchesSearchTerm(const std::wstring& filename, const std::wstring& searchTerm) {
    if (searchTerm.empty()) {
        return true;
    }

    std::wstring lowerFilename = ToLowerCase(filename);
    std::wstring lowerSearchTerm = ToLowerCase(searchTerm);

    // Check if the search term is found in the filename
    return lowerFilename.find(lowerSearchTerm) != std::wstring::npos;
}

// Apply Segoe UI font to all controls
void ApplyFontToAllControls()
{
    if (g_hFont)
    {
        // Apply font to all controls
        SendMessageW(g_hwndMain, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndBackButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndForwardButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndAddressBar, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndGoButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndListView, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndSearchBox, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndSearchButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndStatusBar, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(g_hwndStopSearchButton, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    }
}

// Get a list of available drives
std::vector<fs::path> EnumerateDrives()
{
    std::vector<fs::path> drives;

    DWORD driveMask = ::GetLogicalDrives();
    if (driveMask == 0)
    {
        return drives;
    }

    for (char drive = 'A'; drive <= 'Z'; drive++)
    {
        if ((driveMask & 1) == 1)
        {
            std::wstring drivePath = std::format(L"{}:\\", drive);
            drives.push_back(drivePath);
        }
        driveMask >>= 1;
    }

    return drives;
}

// Get file type description
std::wstring GetFileTypeDescription(const fs::path& path)
{
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(path.wstring().c_str(), 0, &sfi, sizeof(sfi), SHGFI_TYPENAME))
    {
        return sfi.szTypeName;
    }

    return L"File";
}

// Format file size
std::wstring FormatFileSize(uintmax_t size)
{
    constexpr const wchar_t* SUFFIXES[] = {L"B", L"KB", L"MB", L"GB", L"TB"};

    double dblSize = static_cast<double>(size);
    int suffixIndex = 0;

    while (dblSize >= 1024.0 && suffixIndex < 4)
    {
        dblSize /= 1024.0;
        suffixIndex++;
    }

    if (suffixIndex == 0)
    {
        return std::format(L"{} {}", size, SUFFIXES[suffixIndex]);
    }
    else if (dblSize < 10)
    {
        return std::format(L"{:.2f} {}", dblSize, SUFFIXES[suffixIndex]);
    }
    else if (dblSize < 100)
    {
        return std::format(L"{:.1f} {}", dblSize, SUFFIXES[suffixIndex]);
    }
    else
    {
        return std::format(L"{:.0f} {}", dblSize, SUFFIXES[suffixIndex]);
    }
}

// Stop the search operation
void StopSearch() {
    if (!g_isSearching) {
        return;
    }

    // Set the flag to stop searching
    g_isSearching = false;

    // Notify threads to stop
    {
        std::lock_guard<std::mutex> lock(g_stopSearchMutex);
        g_stopSearchCV.notify_all();
    }

    // Join all search threads
    for (auto& thread : g_searchThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // Clear the thread vector
    g_searchThreads.clear();

    // Hide stop search button
    ShowWindow(g_hwndStopSearchButton, SW_HIDE);

    // Update UI with final results
    DisplaySearchResults();

    // Update status bar
    std::wstring status = std::format(L"Search complete. Found {} files in {} directories. Searched {} files.",
                                     g_filesFound.load(), g_directoriesSearched.load(), g_filesSearched.load());
    SendMessageW(g_hwndStatusBar, SB_SETTEXT, 0, (LPARAM)status.c_str());

    // Post message to notify search complete
    PostMessageW(g_hwndMain, WM_SEARCH_COMPLETE, 0, 0);
}

void InitializeSearch() {
    // Reset counters
    g_filesSearched = 0;
    g_filesFound = 0;
    g_directoriesSearched = 0;

    // Clear results
    {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        g_searchResults.clear();
    }

    // Show stop search button
    ShowWindow(g_hwndStopSearchButton, SW_SHOW);

    // Update status bar
    SendMessageW(g_hwndStatusBar, SB_SETTEXT, 0, (LPARAM)L"Starting search...");
}

// Start a file search operation
void StartFileSearch() {
    if (g_isSearching) {
        StopSearch();
    }

    // Get search term
    wchar_t searchText[MAX_PATH] = {};
    GetWindowTextW(g_hwndSearchBox, searchText, MAX_PATH);

    // Trim whitespace
    std::wstring searchTerm = searchText;
    searchTerm.erase(0, searchTerm.find_first_not_of(L' '));
    searchTerm.erase(searchTerm.find_last_not_of(L' ') + 1);

    if (searchTerm.empty()) {
        MessageBoxW(g_hwndMain, L"Please enter a search term.", L"Search", MB_ICONINFORMATION);
        return;
    }

    // Determine the search root path
    fs::path rootPath;
    if (g_currentPath.empty()) {
        MessageBoxW(g_hwndMain, L"Please navigate to a drive or folder to search.", L"Search", MB_ICONINFORMATION);
        return;
    } else {
        rootPath = g_currentPath;
    }

    // Check if the directory is accessible
    std::error_code ec;
    if (!fs::exists(rootPath, ec) || !fs::is_directory(rootPath, ec)) {
        std::wstring errorMsg = L"Cannot access directory: " + rootPath.wstring();
        MessageBoxW(g_hwndMain, errorMsg.c_str(), L"Search Error", MB_ICONERROR);
        return;
    }

    // Initialize search state
    InitializeSearch();

    // Start search
    SearchFiles(rootPath, searchTerm);

    // Start a timeout thread
    std::thread timeoutThread([rootPath]() {
        // Set timeout based on drive type (longer for network drives)
        UINT driveType = GetDriveTypeW(rootPath.root_name().c_str());
        int timeoutSeconds = (driveType == DRIVE_REMOTE) ? 300 : 120; // 5 min for network, 2 min for local

        // Wait for timeout or completion
        for (int i = 0; i < timeoutSeconds && g_isSearching; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // If still searching after timeout, ask user if they want to continue
        if (g_isSearching) {
            // Post message to UI thread to show dialog
            PostMessageW(g_hwndMain, WM_APP + 100, 0, 0);
        }
    });

    // Detach timeout thread
    timeoutThread.detach();
}

// Clear search results
void ClearSearchResults() {
    std::lock_guard<std::mutex> lock(g_resultsMutex);
    g_searchResults.clear();
}

// Display search results in the list view
void DisplaySearchResults() {
    // Clear list view and free previous items
    int itemCount = ListView_GetItemCount(g_hwndListView);
    for (int i = 0; i < itemCount; i++) {
        LVITEMW lvItem = {};
        lvItem.mask = LVIF_PARAM;
        lvItem.iItem = i;

        if (ListView_GetItem(g_hwndListView, &lvItem) && lvItem.lParam) {
            delete reinterpret_cast<fs::path*>(lvItem.lParam);
        }
    }
    ListView_DeleteAllItems(g_hwndListView);

    // Copy search results to prevent locking during UI update
    std::vector<fs::path> results;
    {
        std::lock_guard<std::mutex> lock(g_resultsMutex);
        results = g_searchResults;
    }

    // Sort results alphabetically
    std::sort(results.begin(), results.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename().wstring() < b.filename().wstring();
    });

    // Populate list view with search results
    int index = 0;
    for (const auto& path : results) {
        LVITEMW lvItem = {};
        lvItem.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvItem.iItem = index++;
        lvItem.iSubItem = 0;

        // Store the path
        fs::path* pathCopy = new fs::path(path);
        lvItem.lParam = (LPARAM)pathCopy;

        // Get file/folder name
        std::wstring name = path.filename().wstring();
        lvItem.pszText = const_cast<LPWSTR>(name.c_str());

        // Add icon
        SHFILEINFOW sfi = {};
        SHGetFileInfoW(path.wstring().c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON);
        lvItem.iImage = ImageList_AddIcon(ListView_GetImageList(g_hwndListView, LVSIL_SMALL), sfi.hIcon);
        DestroyIcon(sfi.hIcon);

        // Insert item
        int itemIndex = ListView_InsertItem(g_hwndListView, &lvItem);

        // Set location (parent path)
        std::wstring location = path.parent_path().wstring();
        ListView_SetItemText(g_hwndListView, itemIndex, 1, const_cast<LPWSTR>(location.c_str()));

        // Set type and size
        if (fs::is_directory(path)) {
            ListView_SetItemText(g_hwndListView, itemIndex, 2, const_cast<LPWSTR>(L"Folder"));
            ListView_SetItemText(g_hwndListView, itemIndex, 3, const_cast<LPWSTR>(L""));
        } else {
            // Get file type
            std::wstring typeDesc = GetFileTypeDescription(path);
            ListView_SetItemText(g_hwndListView, itemIndex, 2, const_cast<LPWSTR>(typeDesc.c_str()));

            // Get file size
            uintmax_t size = 0;
            try {
                size = fs::file_size(path);
            } catch (...) {
                // Ignore errors
            }

            std::wstring sizeStr = FormatFileSize(size);
            ListView_SetItemText(g_hwndListView, itemIndex, 3, const_cast<LPWSTR>(sizeStr.c_str()));
        }
    }

    // Update window title
    std::wstring windowTitle = std::format(L"Fast File Explorer - Search Results ({} items)", results.size());
    SetWindowTextW(g_hwndMain, windowTitle.c_str());

    // Set search box text as address bar text
    wchar_t searchText[MAX_PATH] = {};
    GetWindowTextW(g_hwndSearchBox, searchText, MAX_PATH);
    std::wstring addressText = std::format(L"Search Results: \"{}\" in {}", searchText, g_currentPath.wstring());
    SetWindowTextW(g_hwndAddressBar, addressText.c_str());
}

// Update search progress
void UpdateSearchProgress() {
    // Get current counts
    int filesSearched = g_filesSearched.load();
    int filesFound = g_filesFound.load();
    int directoriesSearched = g_directoriesSearched.load();

    // Update status bar
    std::wstring status = std::format(L"Searching... Found {} files in {} directories. Searched {} files.",
                                     filesFound, directoriesSearched, filesSearched);
    SendMessageW(g_hwndStatusBar, SB_SETTEXT, 0, (LPARAM)status.c_str());
}

// Recursive file search function
void SearchDirectoryRecursive(const fs::path& dirPath, const std::wstring& searchTerm,
                             ThreadPool& pool, std::atomic<bool>& isSearching) {
    if (!isSearching) {
        return;
    }

    // Convert search term to lowercase once at the beginning of each thread
    std::wstring lowerSearchTerm = ToLowerCase(searchTerm);

    try {
        // Increment directories searched counter
        g_directoriesSearched++;

        // Check stopping condition only occasionally to avoid overhead
        std::atomic<int> entryCounter{0};

        // Use error_code to avoid exceptions for common file system errors
        std::error_code ec;
        auto dirOptions = fs::directory_options::skip_permission_denied;

        // Use a try-block for the directory iteration to handle access errors
        try {
            for (const auto& entry : fs::directory_iterator(dirPath, dirOptions, ec)) {
                // Check stopping condition periodically
                if ((++entryCounter % 100) == 0 && !isSearching) {
                    return;
                }

                try {
                    if (fs::is_regular_file(entry, ec)) {
                        // Get filename once
                        const std::wstring filename = entry.path().filename().wstring();

                        // Increment files searched counter
                        g_filesSearched++;

                        // Fast case-insensitive check
                        std::wstring lowerFilename = ToLowerCase(filename);
                        if (lowerFilename.find(lowerSearchTerm) != std::wstring::npos) {
                            // Increment files found counter
                            g_filesFound++;

                            // Add to results
                            {
                                std::lock_guard<std::mutex> lock(g_resultsMutex);
                                g_searchResults.push_back(entry.path());
                            }

                            // Only update UI periodically to reduce overhead
                            if (g_filesFound % 20 == 0) {
                                PostMessageW(g_hwndMain, WM_SEARCH_RESULT, 0, 0);
                            }
                        }

                        // Update progress less frequently
                        if (g_filesSearched % 500 == 0) {
                            PostMessageW(g_hwndMain, WM_SEARCH_PROGRESS, 0, 0);
                        }
                    }
                    else if (fs::is_directory(entry, ec) && !fs::is_symlink(entry, ec)) {
                        // Use thread-local counter to limit directory recursion
                        thread_local int recursionDepth = 0;

                        // Add directory to pool queue if we're not too deep in recursion
                        if (recursionDepth < 50) {  // Limit recursion depth
                            recursionDepth++;
                            pool.enqueue([entry, searchTerm, &pool, &isSearching]() {
                                SearchDirectoryRecursive(entry.path(), searchTerm, pool, isSearching);
                            });
                            recursionDepth--;
                        } else {
                            // Process directory directly for deep paths
                            SearchDirectoryRecursive(entry.path(), searchTerm, pool, isSearching);
                        }
                    }
                }
                catch (const std::exception&) {
                    // Skip files/directories that can't be accessed
                    continue;
                }
            }
        }
        catch (const std::exception&) {
            // Skip directories that can't be accessed
        }
    }
    catch (const std::exception&) {
        // Skip directories that can't be accessed
    }
}

bool FastMatchesSearchTerm(const std::wstring& filename, const std::wstring& searchTerm) {
    if (searchTerm.empty()) {
        return true;
    }

    // Convert to lowercase only once per call
    std::wstring lowerFilename = ToLowerCase(filename);
    std::wstring lowerSearchTerm = ToLowerCase(searchTerm);

    // Simple substring check
    return lowerFilename.find(lowerSearchTerm) != std::wstring::npos;
}

// Search files function
void SearchFiles(const fs::path& rootPath, const std::wstring& searchTerm) {
    // Set searching flag
    g_isSearching = true;

    // Update UI
    SendMessageW(g_hwndStatusBar, SB_SETTEXT, 0, (LPARAM)L"Starting search...");

    // Clear old search threads
    g_searchThreads.clear();

    // Start search thread with a more efficient approach
    std::jthread searchThread([rootPath, searchTerm]() {
        try {
            // Limit number of search threads based on CPU cores
            int numCores = std::thread::hardware_concurrency();
            int threadCount = std::max(2, std::min(MAX_SEARCH_THREADS, numCores));

            // Create thread pool with optimal size for search operations
            ThreadPool pool(threadCount);

            // Add a timer to update UI periodically regardless of search progress
            std::thread updateTimer([&]() {
                while (g_isSearching) {
                    // Update UI every half second
                    PostMessageW(g_hwndMain, WM_SEARCH_PROGRESS, 0, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            });

            // Start the recursive search
            SearchDirectoryRecursive(rootPath, searchTerm, pool, g_isSearching);

            // Set searching to false to stop the update timer
            g_isSearching = false;

            // Wait for update timer to complete
            if (updateTimer.joinable()) {
                updateTimer.join();
            }

            // Post message to update UI with final results
            PostMessageW(g_hwndMain, WM_SEARCH_COMPLETE, 0, 0);
        }
        catch (const std::exception& e) {
            // Log error or display in status bar
            std::string errorMsg = "Search error: ";
            errorMsg += e.what();
            g_isSearching = false;

            // Convert to wstring for Windows API
            std::wstring wErrorMsg(errorMsg.begin(), errorMsg.end());
            SendMessageW(g_hwndStatusBar, SB_SETTEXT, 0, (LPARAM)wErrorMsg.c_str());

            // Notify UI that search is complete (with error)
            PostMessageW(g_hwndMain, WM_SEARCH_COMPLETE, 0, 0);
        }
    });

    // Store the thread for proper management
    g_searchThreads.push_back(std::move(searchThread));
}

// Navigate to a path
void NavigateTo(const fs::path& path, bool addToHistory)
{
    try
    {
        fs::path newPath;

        if (path.empty())
        {
            // Special case for drives
            newPath = L"";
        }
        else if (fs::is_directory(path))
        {
            newPath = path;
        }
        else if (fs::exists(path))
        {
            // If it's a file, open it
            ShellExecuteW(g_hwndMain, L"open", path.wstring().c_str(), NULL, NULL, SW_SHOW);
            return;
        }
        else
        {
            MessageBoxW(g_hwndMain, L"The specified path does not exist.", L"Error", MB_ICONERROR);
            return;
        }

        // Add current path to back history if it's a new navigation
        if (addToHistory && !g_navigatingHistory && !g_currentPath.empty())
        {
            g_backHistory.push_front(g_currentPath);

            // Clear forward history when navigating to a new path
            while (!g_forwardHistory.empty())
            {
                g_forwardHistory.pop_front();
            }
        }

        // Stop any ongoing search
        if (g_isSearching) {
            StopSearch();
        }

        // Update current path and refresh view
        g_currentPath = newPath;
        PopulateListView(g_currentPath);
    }
    catch (const std::exception& e)
    {
        MessageBoxA(g_hwndMain, e.what(), "Error", MB_ICONERROR);
    }
}

// Navigate back
void NavigateBack()
{
    if (!g_backHistory.empty())
    {
        // Add current location to forward history
        g_forwardHistory.push_front(g_currentPath);

        // Get the back location
        fs::path backPath = g_backHistory.front();
        g_backHistory.pop_front();

        // Navigate without adding to history
        g_navigatingHistory = true;
        NavigateTo(backPath, false);
        g_navigatingHistory = false;
    }
}

// Navigate forward
void NavigateForward()
{
    if (!g_forwardHistory.empty())
    {
        // Add current location to back history
        g_backHistory.push_front(g_currentPath);

        // Get the forward location
        fs::path forwardPath = g_forwardHistory.front();
        g_forwardHistory.pop_front();

        // Navigate without adding to history
        g_navigatingHistory = true;
        NavigateTo(forwardPath, false);
        g_navigatingHistory = false;
    }
}

// Update the state of navigation buttons
void UpdateNavigationButtons()
{
    EnableWindow(g_hwndBackButton, !g_backHistory.empty());
    EnableWindow(g_hwndForwardButton, !g_forwardHistory.empty());
}

// Populate the list view with files and folders
void PopulateListView(const fs::path& path)
{
    // Clear list view and free previous items
    int itemCount = ListView_GetItemCount(g_hwndListView);
    for (int i = 0; i < itemCount; i++)
    {
        LVITEMW lvItem = {};
        lvItem.mask = LVIF_PARAM;
        lvItem.iItem = i;

        if (ListView_GetItem(g_hwndListView, &lvItem) && lvItem.lParam)
        {
            delete reinterpret_cast<fs::path*>(lvItem.lParam);
        }
    }
    ListView_DeleteAllItems(g_hwndListView);

    try
    {
        if (path.empty())
        {
            // Special case: show drives
            auto drives = EnumerateDrives();
            int index = 0;

            for (const auto& drive : drives)
            {
                LVITEMW lvItem = {};
                lvItem.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                lvItem.iItem = index++;
                lvItem.iSubItem = 0;

                // Store the path
                fs::path* drivePath = new fs::path(drive);
                lvItem.lParam = (LPARAM)drivePath;

                // Get drive label
                std::wstring driveLabel = drive.wstring();
                lvItem.pszText = const_cast<LPWSTR>(driveLabel.c_str());

                // Add drive icon
                SHFILEINFOW sfi = {};
                SHGetFileInfoW(drive.wstring().c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON);
                lvItem.iImage = ImageList_AddIcon(ListView_GetImageList(g_hwndListView, LVSIL_SMALL), sfi.hIcon);
                DestroyIcon(sfi.hIcon);

                // Insert item
                int itemIndex = ListView_InsertItem(g_hwndListView, &lvItem);

                // Set type
                ListView_SetItemText(g_hwndListView, itemIndex, 1, const_cast<LPWSTR>(L"Drive"));

                // Set size (not applicable for drives)
                ListView_SetItemText(g_hwndListView, itemIndex, 2, const_cast<LPWSTR>(L""));

                // Set location (empty for drives)
                ListView_SetItemText(g_hwndListView, itemIndex, 3, const_cast<LPWSTR>(L""));
            }

            // Update address bar
            SetWindowTextW(g_hwndAddressBar, THIS_PC_NAME);

            // Set window title
            std::wstring windowTitle = L"Fast File Explorer - This PC";
            SetWindowTextW(g_hwndMain, windowTitle.c_str());
        }
        else
        {
            // Update address bar
            SetWindowTextW(g_hwndAddressBar, path.wstring().c_str());

            // Set window title
            std::wstring windowTitle = L"Fast File Explorer - " + path.wstring();
            SetWindowTextW(g_hwndMain, windowTitle.c_str());

            // Actual directory contents
            int index = 0;

            try
            {
                for (const auto& entry : fs::directory_iterator(path))
                {
                    LVITEMW lvItem = {};
                    lvItem.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
                    lvItem.iItem = index++;
                    lvItem.iSubItem = 0;

                    // Store the path
                    fs::path* entryPath = new fs::path(entry.path());
                    lvItem.lParam = (LPARAM)entryPath;

                    // Get file/folder name
                    std::wstring name = entry.path().filename().wstring();
                    lvItem.pszText = const_cast<LPWSTR>(name.c_str());

                    // Add icon
                    SHFILEINFOW sfi = {};
                    SHGetFileInfoW(entry.path().wstring().c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON);
                    lvItem.iImage = ImageList_AddIcon(ListView_GetImageList(g_hwndListView, LVSIL_SMALL), sfi.hIcon);
                    DestroyIcon(sfi.hIcon);

                    // Insert item
                    int itemIndex = ListView_InsertItem(g_hwndListView, &lvItem);

                    // Set type
                    if (fs::is_directory(entry.path()))
                    {
                        ListView_SetItemText(g_hwndListView, itemIndex, 1, const_cast<LPWSTR>(L"Folder"));
                        ListView_SetItemText(g_hwndListView, itemIndex, 2, const_cast<LPWSTR>(L""));
                    }
                    else
                    {
                        // Get file type
                        std::wstring typeDesc = GetFileTypeDescription(entry.path());
                        ListView_SetItemText(g_hwndListView, itemIndex, 1, const_cast<LPWSTR>(typeDesc.c_str()));

                        // Get file size
                        uintmax_t size = 0;
                        try
                        {
                            size = fs::file_size(entry.path());
                        }
                        catch (...)
                        {
                            // Ignore errors
                        }

                        std::wstring sizeStr = FormatFileSize(size);
                        ListView_SetItemText(g_hwndListView, itemIndex, 2, const_cast<LPWSTR>(sizeStr.c_str()));
                    }

                    // Set location (parent path - empty for current directory)
                    ListView_SetItemText(g_hwndListView, itemIndex, 3, const_cast<LPWSTR>(L""));
                }
            }
            catch (const fs::filesystem_error& e)
            {
                MessageBoxA(g_hwndMain, e.what(), "Directory Error", MB_ICONERROR);
            }
        }
    }
    catch (const std::exception& e)
    {
        MessageBoxA(g_hwndMain, e.what(), "Error", MB_ICONERROR);
    }

    // Update navigation buttons
    UpdateNavigationButtons();
}

// Create the list view control
bool CreateListView(HWND hwndParent)
{
    // Create list view control
    g_hwndListView = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHAREIMAGELISTS | LVS_SINGLESEL,
        0, BUTTON_HEIGHT + 20, 0, 0, // Will be resized in WM_SIZE
        hwndParent,
        (HMENU)(INT_PTR)ID_FILE_LIST,
        GetModuleHandle(NULL),
        NULL
    );

    if (!g_hwndListView)
    {
        return false;
    }

    // Set extended list view styles
    ListView_SetExtendedListViewStyle(g_hwndListView, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

    // Add columns
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;

    // Name column
    lvc.iSubItem = 0;
    lvc.pszText = const_cast<LPWSTR>(L"Name");
    lvc.cx = 300;
    lvc.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(g_hwndListView, 0, &lvc);

    // Type column
    lvc.iSubItem = 1;
    lvc.pszText = const_cast<LPWSTR>(L"Type");
    lvc.cx = 150;
    lvc.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(g_hwndListView, 1, &lvc);

    // Size column
    lvc.iSubItem = 2;
    lvc.pszText = const_cast<LPWSTR>(L"Size");
    lvc.cx = 100;
    lvc.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(g_hwndListView, 2, &lvc);

    // Location column (for search results)
    lvc.iSubItem = 3;
    lvc.pszText = const_cast<LPWSTR>(L"Location");
    lvc.cx = 300;
    lvc.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(g_hwndListView, 3, &lvc);

    // Create and set image list
    HIMAGELIST hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 32, 32);
    ListView_SetImageList(g_hwndListView, hImageList, LVSIL_SMALL);

    // Apply font to list view
    if (g_hFont)
    {
        SendMessageW(g_hwndListView, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    }

    return true;
}

// Custom window procedure for address bar to handle Enter key
LRESULT CALLBACK AddressBarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        // Handle Enter key
        wchar_t path[MAX_PATH] = {};
        GetWindowTextW(hwnd, path, MAX_PATH);
        if (path[0] != '\0')
        {
            // Check if user typed "This PC" (case-insensitive)
            if (_wcsicmp(path, THIS_PC_NAME) == 0)
            {
                NavigateTo(L""); // Empty path means "This PC" in our app
            }
            else
            {
                NavigateTo(path);
            }
        }
        return 0;
    }

    // Call the original window procedure for unhandled messages
    return CallWindowProc(g_oldAddressBarProc, hwnd, uMsg, wParam, lParam);
}

// Custom window procedure for buttons with dark gray background
LRESULT CALLBACK CustomButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static bool isTracking = false;
    static bool isPressed = false;
    static HBRUSH hHoverBrush = CreateSolidBrush(RGB(80, 80, 80)); // Slightly lighter gray for hover
    static HBRUSH hPressedBrush = CreateSolidBrush(RGB(40, 40, 40)); // Slightly darker gray for pressed

    switch (uMsg)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rect;
            GetClientRect(hwnd, &rect);

            // Select the appropriate brush based on button state
            HBRUSH hBrush = g_hButtonBrush; // Default
            if (isPressed)
                hBrush = hPressedBrush;
            else if (isTracking)
                hBrush = hHoverBrush;

            // Fill the background
            FillRect(hdc, &rect, hBrush);

            // Get the icon directly from the window's user data
            HICON hIcon = (HICON)GetWindowLongPtr(hwnd, GWLP_USERDATA);

            // Set up for drawing text
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, BUTTON_TEXT_COLOR);

            if (hIcon)
            {
                // Draw icon centered - use a different approach
                int iconX = (rect.right - rect.left - ICON_SIZE) / 2;
                int iconY = (rect.bottom - rect.top - ICON_SIZE) / 2;

                // Try to force icon drawing with multiple methods
                DrawIcon(hdc, iconX, iconY, hIcon);
            }
            else
            {
                // No icon, draw text instead
                TCHAR text[256];
                GetWindowText(hwnd, text, 256);
                if (text[0] != '\0')
                {
                    // Draw text as a clear fallback
                    DrawText(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }

            // Draw focus rectangle if button has focus
            if (GetFocus() == hwnd)
            {
                RECT focusRect = rect;
                InflateRect(&focusRect, -3, -3);
                DrawFocusRect(hdc, &focusRect);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

    case WM_MOUSEMOVE:
        if (!isTracking)
        {
            // Start tracking mouse events to detect when mouse leaves
            TRACKMOUSEEVENT tme = {0};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            if (TrackMouseEvent(&tme))
            {
                isTracking = true;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        isTracking = false;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        isPressed = true;
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (isPressed)
        {
            isPressed = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);

            // Send click notification to parent
            RECT rect;
            GetClientRect(hwnd, &rect);
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            if (PtInRect(&rect, pt))
            {
                SendMessageW(GetParent(hwnd), WM_COMMAND,
                             MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
            }
        }
        return 0;

    case WM_SETTEXT:
        {
            LRESULT result = DefWindowProcW(hwnd, uMsg, wParam, lParam);
            InvalidateRect(hwnd, NULL, FALSE);
            return result;
        }

    case WM_GETDLGCODE:
        return DLGC_BUTTON | DLGC_WANTARROWS;

    case WM_KEYDOWN:
        if (wParam == VK_SPACE || wParam == VK_RETURN)
        {
            isPressed = true;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_KEYUP:
        if ((wParam == VK_SPACE || wParam == VK_RETURN) && isPressed)
        {
            isPressed = false;
            InvalidateRect(hwnd, NULL, FALSE);
            SendMessageW(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), (LPARAM)hwnd);
        }
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_DESTROY:
        DeleteObject(hHoverBrush);
        DeleteObject(hPressedBrush);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_APP + 100: // Search timeout message
        {
            // Only show dialog if still searching
            if (g_isSearching) {
                int result = MessageBoxW(hwnd,
                    L"The search is taking a long time. Do you want to continue searching?",
                    L"Search Taking Too Long",
                    MB_YESNO | MB_ICONQUESTION);

                if (result == IDNO) {
                    // User wants to stop the search
                    StopSearch();
                }
            }
            return 0;
        }
    case WM_CREATE:
        return 0;

    case WM_SIZE:
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            // Resize controls with the new BUTTON_WIDTH and BUTTON_HEIGHT
            SetWindowPos(g_hwndBackButton, NULL, UI_PADDING, UI_PADDING, BUTTON_WIDTH, BUTTON_HEIGHT, SWP_NOZORDER);
            SetWindowPos(g_hwndForwardButton, NULL, UI_PADDING + BUTTON_WIDTH + 5, UI_PADDING, BUTTON_WIDTH,
                         BUTTON_HEIGHT, SWP_NOZORDER);

            // Adjust address bar position to account for new button sizes
            int addressBarX = UI_PADDING + (BUTTON_WIDTH + 5) * 2;
            int addressBarWidth = (width * 2) / 3 - addressBarX - 45;  // 2/3 of width for address bar
            SetWindowPos(g_hwndAddressBar, NULL, addressBarX, UI_PADDING + 5, addressBarWidth, 25, SWP_NOZORDER);
            SetWindowPos(g_hwndGoButton, NULL, addressBarX + addressBarWidth + 5, UI_PADDING + 5, 30, 25, SWP_NOZORDER);

            // Search box position (after Go button)
            int searchBoxX = addressBarX + addressBarWidth + 40;
            int searchWidth = width - searchBoxX - 80;  // Reserve space for search button
            SetWindowPos(g_hwndSearchBox, NULL, searchBoxX, UI_PADDING + 5, searchWidth, 25, SWP_NOZORDER);
            SetWindowPos(g_hwndSearchButton, NULL, searchBoxX + searchWidth + 5, UI_PADDING + 5, 35, 25, SWP_NOZORDER);
            SetWindowPos(g_hwndStopSearchButton, NULL, searchBoxX + searchWidth + 45, UI_PADDING + 5, 35, 25, SWP_NOZORDER);

            // Add status bar
            int statusBarHeight = 25;

            // Resize list view (account for status bar height)
            SetWindowPos(g_hwndListView, NULL, 0, BUTTON_HEIGHT + 20, width,
                        height - (BUTTON_HEIGHT + 20) - statusBarHeight, SWP_NOZORDER);

            // Resize status bar
            SetWindowPos(g_hwndStatusBar, NULL, 0, height - statusBarHeight, width, statusBarHeight,
                        SWP_NOZORDER);
            return 0;
        }

    case WM_COMMAND:
        {
            int ctrlId = LOWORD(wParam);
            int notifyCode = HIWORD(wParam);

            if (ctrlId == ID_BACK_BUTTON)
            {
                NavigateBack();
                return 0;
            }
            else if (ctrlId == ID_FORWARD_BUTTON)
            {
                NavigateForward();
                return 0;
            }
            else if (ctrlId == ID_GO_BUTTON)
            {
                // Get path from address bar and navigate to it
                wchar_t path[MAX_PATH] = {};
                GetWindowTextW(g_hwndAddressBar, path, MAX_PATH);
                if (path[0] != '\0')
                {
                    // Check if user typed "This PC" (case-insensitive)
                    if (_wcsicmp(path, THIS_PC_NAME) == 0)
                    {
                        NavigateTo(L""); // Empty path means "This PC" in our app
                    }
                    else
                    {
                        NavigateTo(path);
                    }
                }
                return 0;
            }
            else if (ctrlId == ID_SEARCH_BUTTON)
            {
                // Start file search
                StartFileSearch();
                return 0;
            }
            else if (ctrlId == ID_STOP_SEARCH_BUTTON)
            {
                // Stop file search
                StopSearch();
                return 0;
            }
            break;
        }

    case WM_NOTIFY:
        {
            NMHDR* nmhdr = (NMHDR*)lParam;

            if (nmhdr->hwndFrom == g_hwndListView)
            {
                switch (nmhdr->code)
                {
                case NM_DBLCLK:
                    {
                        NMITEMACTIVATE* nmia = (NMITEMACTIVATE*)lParam;
                        int itemIndex = nmia->iItem;

                        if (itemIndex >= 0)
                        {
                            LVITEMW lvItem = {};
                            wchar_t buffer[MAX_PATH] = {};

                            lvItem.mask = LVIF_TEXT | LVIF_PARAM;
                            lvItem.iItem = itemIndex;
                            lvItem.iSubItem = 0;
                            lvItem.pszText = buffer;
                            lvItem.cchTextMax = MAX_PATH;

                            ListView_GetItem(g_hwndListView, &lvItem);

                            fs::path* itemPath = (fs::path*)lvItem.lParam;
                            if (itemPath)
                            {
                                // Create a copy of the path to use in NavigateTo
                                fs::path pathCopy = *itemPath;
                                NavigateTo(pathCopy);
                            }
                        }
                        return 0;
                    }
                }
            }
            break;
        }

    case WM_SEARCH_RESULT:
        // Update UI with search results
        DisplaySearchResults();
        return 0;

    case WM_SEARCH_COMPLETE:
        // Search completed
        StopSearch();
        return 0;

    case WM_SEARCH_PROGRESS:
        // Update search progress
        UpdateSearchProgress();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Main entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    // Initialize common controls
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    // Create brush for dark gray background
    g_hButtonBrush = CreateSolidBrush(DARK_GRAY);

    // Register custom button class
    WNDCLASSEX wcButton = {};
    wcButton.cbSize = sizeof(WNDCLASSEX);
    wcButton.style = CS_HREDRAW | CS_VREDRAW | CS_GLOBALCLASS;
    wcButton.lpfnWndProc = CustomButtonProc;
    wcButton.hInstance = hInstance;
    wcButton.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcButton.hbrBackground = g_hButtonBrush;
    wcButton.lpszClassName = CUSTOM_BUTTON_CLASS;

    if (!RegisterClassEx(&wcButton))
    {
        MessageBoxW(NULL, L"Failed to register custom button class!", L"Error", MB_ICONERROR);
        return 1;
    }

    // Register window class
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WindowProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = WINDOW_CLASS_NAME;
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex))
    {
        MessageBoxW(NULL, L"Failed to register window class!", L"Error", MB_ICONERROR);
        return 1;
    }

    // Create the main window
    g_hwndMain = CreateWindowExW(
        WS_EX_OVERLAPPEDWINDOW,
        WINDOW_CLASS_NAME,
        L"Fast File Explorer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwndMain)
    {
        MessageBoxW(NULL, L"Failed to create main window!", L"Error", MB_ICONERROR);
        return 1;
    }

    // Create Segoe UI font
    g_hFont = CreateSegoeUIFont(16);
    if (!g_hFont)
    {
        MessageBoxW(NULL, L"Failed to create Segoe UI font!", L"Warning", MB_ICONWARNING);
    }    // Initialize back and forward icons using a specific icon from shell32.dll
    HINSTANCE hShell32 = LoadLibraryW(L"shell32.dll");
    if (hShell32)
    {
        // Calculate icon indices based on the offset from user's system (191) to reference system (305)
        // Use one icon index for back button and one for forward button
        int iconOffset = 305 - 191; // The difference between reference and user's system
        int backIconIndex = 135 - iconOffset; // For left arrow
        int forwardIconIndex = 136 - iconOffset; // For right arrow
        
        g_hBackIcon = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(backIconIndex), IMAGE_ICON, ICON_SIZE, ICON_SIZE, 0);
        g_hForwardIcon = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(forwardIconIndex), IMAGE_ICON, ICON_SIZE, ICON_SIZE, 0);
        FreeLibrary(hShell32);
    }
    else
    {
        // Fall back to text arrows if we can't load the icons
        g_hBackIcon = NULL;
        g_hForwardIcon = NULL;
    }// Create STANDARD buttons with text arrows
    g_hwndBackButton = CreateWindowW(
        L"BUTTON", L"←",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        UI_PADDING, UI_PADDING, BUTTON_WIDTH, BUTTON_HEIGHT,
        g_hwndMain, (HMENU)(INT_PTR)ID_BACK_BUTTON, hInstance, NULL
    );

    g_hwndForwardButton = CreateWindowW(
        L"BUTTON", L"→",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        UI_PADDING + BUTTON_WIDTH + 5, UI_PADDING, BUTTON_WIDTH, BUTTON_HEIGHT,
        g_hwndMain, (HMENU)(INT_PTR)ID_FORWARD_BUTTON, hInstance, NULL
    );// Set button icons using a more reliable method
    if (g_hBackIcon)
    {
        SendMessageW(g_hwndBackButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_hBackIcon);
    }
    else
    {
        SetWindowTextW(g_hwndBackButton, L"←");
    }

    if (g_hForwardIcon)
    {
        SendMessageW(g_hwndForwardButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM)g_hForwardIcon);
    }
    else
    {
        SetWindowTextW(g_hwndForwardButton, L"→");
    }

    // Apply visual styles to buttons for a modern look
    EnableWindowTheme(g_hwndBackButton, L"Navigation", L"Back");
    EnableWindowTheme(g_hwndForwardButton, L"Navigation", L"Forward");

    // Calculate address bar position
    int addressBarX = UI_PADDING + (BUTTON_WIDTH + 5) * 2;
    int addressBarWidth = 800 - addressBarX - UI_PADDING - 40;

    // Create address bar
    g_hwndAddressBar = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        addressBarX, UI_PADDING + 5, addressBarWidth, 25,
        g_hwndMain,
        (HMENU)(INT_PTR)ID_ADDRESS_BAR,
        hInstance,
        NULL
    );

    // Calculate search box position
    int searchBoxX = addressBarX + addressBarWidth + 40;
    int searchWidth = 800 - searchBoxX - 80;  // Reserve space for search button

    // Create search box
    g_hwndSearchBox = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        searchBoxX, UI_PADDING + 5, searchWidth, 25,
        g_hwndMain,
        (HMENU)(INT_PTR)ID_SEARCH_BOX,
        hInstance,
        NULL
    );

    // Set placeholder text for search box
    SetWindowTextW(g_hwndSearchBox, L"Search");

    // Subclass the search box to handle keyboard input
    g_oldSearchBoxProc = (WNDPROC)SetWindowLongPtr(g_hwndSearchBox, GWLP_WNDPROC, (LONG_PTR)SearchBoxProc);

    // Create search button
    g_hwndSearchButton = CreateWindowW(
        L"BUTTON", L"🔍",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        searchBoxX + searchWidth + 5, UI_PADDING + 5, 35, 25,
        g_hwndMain, (HMENU)(INT_PTR)ID_SEARCH_BUTTON, hInstance, NULL
    );

    // Create stop search button (initially hidden)
    g_hwndStopSearchButton = CreateWindowW(
        L"BUTTON", L"✖",
        WS_CHILD | BS_PUSHBUTTON, // Initially hidden
        searchBoxX + searchWidth + 45, UI_PADDING + 5, 35, 25,
        g_hwndMain, (HMENU)(INT_PTR)ID_STOP_SEARCH_BUTTON, hInstance, NULL
    );

    // Create status bar
    g_hwndStatusBar = CreateWindowExW(
        0, STATUSCLASSNAMEW, NULL,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, // Will be sized by parent
        g_hwndMain, NULL, hInstance, NULL
    );

    // Initialize status bar text
    SendMessageW(g_hwndStatusBar, SB_SETTEXT, 0, (LPARAM)L"Ready");

    // Subclass the address bar to handle keyboard input
    g_oldAddressBarProc = (WNDPROC)SetWindowLongPtr(g_hwndAddressBar, GWLP_WNDPROC, (LONG_PTR)AddressBarProc);

    // Create Go button with the custom class
    g_hwndGoButton = CreateCustomButton(g_hwndMain, 755, UI_PADDING + 5, 30, 25, ID_GO_BUTTON, hInstance);
    SetWindowTextW(g_hwndGoButton, L"Go");

    // Apply Segoe UI font to all controls
    ApplyFontToAllControls();

    // Create list view
    if (!CreateListView(g_hwndMain))
    {
        MessageBoxW(NULL, L"Failed to create list view!", L"Error", MB_ICONERROR);
        return 1;
    }

    // Set initial path to drives list
    g_currentPath = L"";
    PopulateListView(g_currentPath);

    // Show the window
    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);

    // Main message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Clean up
    if (g_hFont)
    {
        DeleteObject(g_hFont);
    }
    if (g_hButtonBrush)
    {
        DeleteObject(g_hButtonBrush);
    }
    if (g_hBackIcon)
    {
        DestroyIcon(g_hBackIcon);
    }
    if (g_hForwardIcon)
    {
        DestroyIcon(g_hForwardIcon);
    }

    return (int)msg.wParam;
}

void EnableWindowTheme(HWND hwnd, LPCWSTR classList, LPCWSTR subApp)
{
    SetWindowTheme(hwnd, subApp, NULL);
}

// Custom window procedure for search box to handle Enter key
LRESULT CALLBACK SearchBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        // Handle Enter key - start search
        StartFileSearch();
        return 0;
    }

    // Call the original window procedure for unhandled messages
    return CallWindowProc(g_oldSearchBoxProc, hwnd, uMsg, wParam, lParam);
}