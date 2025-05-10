#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>  // Added for SHGetFolderPath
#include <string>
#include <vector>
#include <filesystem>
#include <format>
#include <deque>

// Link with required libraries
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

// Window class name
constexpr wchar_t WINDOW_CLASS_NAME[] = L"SimpleFileExplorer";

// Control IDs
constexpr int ID_FILE_LIST = 100;
constexpr int ID_BACK_BUTTON = 101;
constexpr int ID_FORWARD_BUTTON = 102;
constexpr int ID_ADDRESS_BAR = 103;
constexpr int ID_GO_BUTTON = 104;

// Global variables
HWND g_hwndMain = NULL;
HWND g_hwndListView = NULL;
HWND g_hwndAddressBar = NULL;
HWND g_hwndBackButton = NULL;
HWND g_hwndForwardButton = NULL;
HWND g_hwndGoButton = NULL;
fs::path g_currentPath;

// Original window procedure for the address bar
WNDPROC g_oldAddressBarProc = NULL;

// Navigation history
std::deque<fs::path> g_backHistory;
std::deque<fs::path> g_forwardHistory;
bool g_navigatingHistory = false;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK AddressBarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
bool CreateListView(HWND hwndParent);
void PopulateListView(const fs::path& path);
void NavigateTo(const fs::path& path, bool addToHistory = true);
void NavigateBack();
void NavigateForward();
std::vector<fs::path> EnumerateDrives();
std::wstring GetFileTypeDescription(const fs::path& path);
std::wstring FormatFileSize(uintmax_t size);
void UpdateNavigationButtons();

// Main entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

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

    if (!RegisterClassExW(&wcex)) {
        MessageBoxW(NULL, L"Failed to register window class!", L"Error", MB_ICONERROR);
        return 1;
    }

    // Create the main window
    g_hwndMain = CreateWindowExW(
        WS_EX_OVERLAPPEDWINDOW,
        WINDOW_CLASS_NAME,
        L"Simple File Explorer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwndMain) {
        MessageBoxW(NULL, L"Failed to create main window!", L"Error", MB_ICONERROR);
        return 1;
    }

    // Create Back button (left arrow)
    g_hwndBackButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"←",  // Left arrow for back
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 10, 30, 25,
        g_hwndMain,
        (HMENU)(INT_PTR)ID_BACK_BUTTON,
        hInstance,
        NULL
    );

    // Create Forward button (right arrow)
    g_hwndForwardButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"→",  // Right arrow for forward
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        45, 10, 30, 25,
        g_hwndMain,
        (HMENU)(INT_PTR)ID_FORWARD_BUTTON,
        hInstance,
        NULL
    );

    // Create address bar
    g_hwndAddressBar = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        85, 10, 665, 25,
        g_hwndMain,
        (HMENU)(INT_PTR)ID_ADDRESS_BAR,
        hInstance,
        NULL
    );

    // Subclass the address bar to handle keyboard input
    g_oldAddressBarProc = (WNDPROC)SetWindowLongPtr(g_hwndAddressBar, GWLP_WNDPROC, (LONG_PTR)AddressBarProc);

    // Create Go button
    g_hwndGoButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"Go",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        755, 10, 30, 25,
        g_hwndMain,
        (HMENU)(INT_PTR)ID_GO_BUTTON,
        hInstance,
        NULL
    );

    // Set fonts for the buttons to ensure proper rendering
    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    if (hFont) {
        SendMessageW(g_hwndBackButton, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hwndForwardButton, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hwndAddressBar, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hwndGoButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    // Create list view
    if (!CreateListView(g_hwndMain)) {
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
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

// Custom window procedure for address bar to handle Enter key
LRESULT CALLBACK AddressBarProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        // Handle Enter key
        wchar_t path[MAX_PATH] = {};
        GetWindowTextW(hwnd, path, MAX_PATH);
        if (path[0] != '\0') {
            NavigateTo(path);
        }
        return 0;
    }

    // Call the original window procedure for unhandled messages
    return CallWindowProc(g_oldAddressBarProc, hwnd, uMsg, wParam, lParam);
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            return 0;

        case WM_SIZE:
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            // Resize controls
            SetWindowPos(g_hwndBackButton, NULL, 10, 10, 30, 25, SWP_NOZORDER);
            SetWindowPos(g_hwndForwardButton, NULL, 45, 10, 30, 25, SWP_NOZORDER);
            SetWindowPos(g_hwndAddressBar, NULL, 85, 10, width - 125, 25, SWP_NOZORDER);
            SetWindowPos(g_hwndGoButton, NULL, width - 40, 10, 30, 25, SWP_NOZORDER);

            // Resize list view
            SetWindowPos(g_hwndListView, NULL, 0, 45, width, height - 45, SWP_NOZORDER);
            return 0;
        }

        case WM_COMMAND:
        {
            int ctrlId = LOWORD(wParam);
            int notifyCode = HIWORD(wParam);

            if (ctrlId == ID_BACK_BUTTON) {
                NavigateBack();
                return 0;
            }
            else if (ctrlId == ID_FORWARD_BUTTON) {
                NavigateForward();
                return 0;
            }
            else if (ctrlId == ID_GO_BUTTON) {
                // Get path from address bar and navigate to it
                wchar_t path[MAX_PATH] = {};
                GetWindowTextW(g_hwndAddressBar, path, MAX_PATH);
                if (path[0] != '\0') {
                    NavigateTo(path);
                }
                return 0;
            }
            break;
        }

        case WM_NOTIFY:
        {
            NMHDR* nmhdr = (NMHDR*)lParam;

            if (nmhdr->hwndFrom == g_hwndListView) {
                switch (nmhdr->code) {
                    case NM_DBLCLK:
                    {
                        NMITEMACTIVATE* nmia = (NMITEMACTIVATE*)lParam;
                        int itemIndex = nmia->iItem;

                        if (itemIndex >= 0) {
                            LVITEMW lvItem = {};
                            wchar_t buffer[MAX_PATH] = {};

                            lvItem.mask = LVIF_TEXT | LVIF_PARAM;
                            lvItem.iItem = itemIndex;
                            lvItem.iSubItem = 0;
                            lvItem.pszText = buffer;
                            lvItem.cchTextMax = MAX_PATH;

                            ListView_GetItem(g_hwndListView, &lvItem);

                            fs::path* itemPath = (fs::path*)lvItem.lParam;
                            if (itemPath) {
                                NavigateTo(*itemPath);
                                delete itemPath; // Free the allocated path
                            }
                        }
                        return 0;
                    }
                }
            }
            break;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Create the list view control
bool CreateListView(HWND hwndParent) {
    // Create list view control
    g_hwndListView = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHAREIMAGELISTS | LVS_SINGLESEL,
        0, 45, 0, 0, // Will be resized in WM_SIZE
        hwndParent,
        (HMENU)(INT_PTR)ID_FILE_LIST,
        GetModuleHandle(NULL),
        NULL
    );

    if (!g_hwndListView) {
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

    // Create and set image list
    HIMAGELIST hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 32, 32);
    ListView_SetImageList(g_hwndListView, hImageList, LVSIL_SMALL);

    return true;
}

// Populate the list view with files and folders
void PopulateListView(const fs::path& path) {
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

    try {
        if (path.empty()) {
            // Special case: show drives
            auto drives = EnumerateDrives();
            int index = 0;

            for (const auto& drive : drives) {
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
            }

            // Update address bar
            SetWindowTextW(g_hwndAddressBar, L"This PC");

            // Set window title
            SetWindowTextW(g_hwndMain, L"Simple File Explorer - This PC");
        }
        else {
            // Update address bar
            SetWindowTextW(g_hwndAddressBar, path.wstring().c_str());

            // Set window title
            std::wstring windowTitle = L"Simple File Explorer - " + path.wstring();
            SetWindowTextW(g_hwndMain, windowTitle.c_str());

            // Actual directory contents
            int index = 0;

            try {
                for (const auto& entry : fs::directory_iterator(path)) {
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
                    if (fs::is_directory(entry.path())) {
                        ListView_SetItemText(g_hwndListView, itemIndex, 1, const_cast<LPWSTR>(L"Folder"));
                        ListView_SetItemText(g_hwndListView, itemIndex, 2, const_cast<LPWSTR>(L""));
                    }
                    else {
                        // Get file type
                        std::wstring typeDesc = GetFileTypeDescription(entry.path());
                        ListView_SetItemText(g_hwndListView, itemIndex, 1, const_cast<LPWSTR>(typeDesc.c_str()));

                        // Get file size
                        uintmax_t size = 0;
                        try {
                            size = fs::file_size(entry.path());
                        }
                        catch (...) {
                            // Ignore errors
                        }

                        std::wstring sizeStr = FormatFileSize(size);
                        ListView_SetItemText(g_hwndListView, itemIndex, 2, const_cast<LPWSTR>(sizeStr.c_str()));
                    }
                }
            }
            catch (const fs::filesystem_error& e) {
                MessageBoxA(g_hwndMain, e.what(), "Directory Error", MB_ICONERROR);
            }
        }
    }
    catch (const std::exception& e) {
        MessageBoxA(g_hwndMain, e.what(), "Error", MB_ICONERROR);
    }

    // Update navigation buttons
    UpdateNavigationButtons();
}

// Navigate to a path
void NavigateTo(const fs::path& path, bool addToHistory) {
    try {
        fs::path newPath;

        if (path.empty()) {
            // Special case for drives
            newPath = L"";
        }
        else if (fs::is_directory(path)) {
            newPath = path;
        }
        else if (fs::exists(path)) {
            // If it's a file, open it
            ShellExecuteW(g_hwndMain, L"open", path.wstring().c_str(), NULL, NULL, SW_SHOW);
            return;
        }
        else {
            MessageBoxW(g_hwndMain, L"The specified path does not exist.", L"Error", MB_ICONERROR);
            return;
        }

        // Add current path to back history if it's a new navigation
        if (addToHistory && !g_navigatingHistory && !g_currentPath.empty()) {
            g_backHistory.push_front(g_currentPath);

            // Clear forward history when navigating to a new path
            while (!g_forwardHistory.empty()) {
                g_forwardHistory.pop_front();
            }
        }

        // Update current path and refresh view
        g_currentPath = newPath;
        PopulateListView(g_currentPath);
    }
    catch (const std::exception& e) {
        MessageBoxA(g_hwndMain, e.what(), "Error", MB_ICONERROR);
    }
}

// Navigate back
void NavigateBack() {
    if (!g_backHistory.empty()) {
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
void NavigateForward() {
    if (!g_forwardHistory.empty()) {
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
void UpdateNavigationButtons() {
    EnableWindow(g_hwndBackButton, !g_backHistory.empty());
    EnableWindow(g_hwndForwardButton, !g_forwardHistory.empty());
}

// Get a list of available drives
std::vector<fs::path> EnumerateDrives() {
    std::vector<fs::path> drives;

    DWORD driveMask = ::GetLogicalDrives();
    if (driveMask == 0) {
        return drives;
    }

    for (char drive = 'A'; drive <= 'Z'; drive++) {
        if ((driveMask & 1) == 1) {
            std::wstring drivePath = std::format(L"{}:\\", drive);
            drives.push_back(drivePath);
        }
        driveMask >>= 1;
    }

    return drives;
}

// Get file type description
std::wstring GetFileTypeDescription(const fs::path& path) {
    SHFILEINFOW sfi = {};
    if (SHGetFileInfoW(path.wstring().c_str(), 0, &sfi, sizeof(sfi), SHGFI_TYPENAME)) {
        return sfi.szTypeName;
    }

    return L"File";
}

// Format file size
std::wstring FormatFileSize(uintmax_t size) {
    constexpr const wchar_t* SUFFIXES[] = { L"B", L"KB", L"MB", L"GB", L"TB" };

    double dblSize = static_cast<double>(size);
    int suffixIndex = 0;

    while (dblSize >= 1024.0 && suffixIndex < 4) {
        dblSize /= 1024.0;
        suffixIndex++;
    }

    if (suffixIndex == 0) {
        return std::format(L"{} {}", size, SUFFIXES[suffixIndex]);
    }
    else if (dblSize < 10) {
        return std::format(L"{:.2f} {}", dblSize, SUFFIXES[suffixIndex]);
    }
    else if (dblSize < 100) {
        return std::format(L"{:.1f} {}", dblSize, SUFFIXES[suffixIndex]);
    }
    else {
        return std::format(L"{:.0f} {}", dblSize, SUFFIXES[suffixIndex]);
    }
}