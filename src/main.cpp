#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>  // Added for ShellExecuteW and shell functions
#include <string>
#include <vector>
#include <filesystem>
#include <format>

// Link with required libraries
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")  // Added for shell functions
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

// Window class name
constexpr wchar_t WINDOW_CLASS_NAME[] = L"SimpleFileExplorer";

// Control IDs
constexpr int ID_FILE_LIST = 100;
constexpr int ID_UP_BUTTON = 101;
constexpr int ID_ADDRESS_BAR = 102;

// Global variables
HWND g_hwndMain = NULL;
HWND g_hwndListView = NULL;
HWND g_hwndAddressBar = NULL;
HWND g_hwndUpButton = NULL;
fs::path g_currentPath;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
bool CreateListView(HWND hwndParent);
void PopulateListView(const fs::path& path);
void NavigateTo(const fs::path& path);
void NavigateUp();
std::vector<fs::path> EnumerateDrives();  // Renamed to avoid conflict with WinAPI
std::wstring GetFileTypeDescription(const fs::path& path);
std::wstring FormatFileSize(uintmax_t size);  // Added function declaration

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

    // Create address bar
    g_hwndAddressBar = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        50, 10, 700, 25,
        g_hwndMain,
        (HMENU)(INT_PTR)ID_ADDRESS_BAR,  // Fixed cast
        hInstance,
        NULL
    );

    // Create Up button
    g_hwndUpButton = CreateWindowExW(
        0,
        L"BUTTON",
        L"↑",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 10, 30, 25,
        g_hwndMain,
        (HMENU)(INT_PTR)ID_UP_BUTTON,  // Fixed cast
        hInstance,
        NULL
    );

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

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            return 0;

        case WM_SIZE:
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            // Resize address bar and up button
            SetWindowPos(g_hwndAddressBar, NULL, 50, 10, width - 60, 25, SWP_NOZORDER);

            // Resize list view
            SetWindowPos(g_hwndListView, NULL, 0, 45, width, height - 45, SWP_NOZORDER);
            return 0;
        }

        case WM_COMMAND:
        {
            int ctrlId = LOWORD(wParam);
            int notifyCode = HIWORD(wParam);

            if (ctrlId == ID_UP_BUTTON) {
                NavigateUp();
                return 0;
            }
            else if (ctrlId == ID_ADDRESS_BAR && notifyCode == EN_CHANGE) {
                // Implement address bar navigation (optional)
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
        (HMENU)(INT_PTR)ID_FILE_LIST,  // Fixed cast
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
    HIMAGELIST hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
    ListView_SetImageList(g_hwndListView, hImageList, LVSIL_SMALL);

    return true;
}

// Populate the list view with files and folders
void PopulateListView(const fs::path& path) {
    // Clear list view
    ListView_DeleteAllItems(g_hwndListView);

    try {
        if (path.empty()) {
            // Special case: show drives
            auto drives = EnumerateDrives();  // Changed function name
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
        }
        else {
            // Update address bar
            SetWindowTextW(g_hwndAddressBar, path.wstring().c_str());

            // Actual directory contents
            int index = 0;

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
    }
    catch (const std::exception& e) {
        MessageBoxA(g_hwndMain, e.what(), "Error", MB_ICONERROR);
    }
}

// Navigate to a path
void NavigateTo(const fs::path& path) {
    try {
        if (path.empty()) {
            // Special case for drives
            g_currentPath = L"";
            PopulateListView(g_currentPath);
        }
        else if (fs::is_directory(path)) {
            g_currentPath = path;
            PopulateListView(g_currentPath);
        }
        else if (fs::exists(path)) {
            // If it's a file, open it
            ShellExecuteW(g_hwndMain, L"open", path.wstring().c_str(), NULL, NULL, SW_SHOW);
        }
    }
    catch (const std::exception& e) {
        MessageBoxA(g_hwndMain, e.what(), "Error", MB_ICONERROR);
    }
}

// Navigate up one level
void NavigateUp() {
    if (g_currentPath.empty()) {
        // Already at drives view, do nothing
        return;
    }

    fs::path parent = g_currentPath.parent_path();
    if (parent == g_currentPath) {
        // At root, go to drives view
        NavigateTo(L"");
    }
    else {
        NavigateTo(parent);
    }
}

// Get a list of available drives (renamed to avoid conflict with WinAPI)
std::vector<fs::path> EnumerateDrives() {
    std::vector<fs::path> drives;

    DWORD driveMask = ::GetLogicalDrives();  // Use namespace resolution operator
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