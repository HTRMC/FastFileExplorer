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
#include <Uxtheme.h>  // For button theming

// Link with required libraries
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "UxTheme.lib") // For button theming
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

// UI constants
constexpr int ICON_SIZE = 16; // Standard small icon size in Windows 11
constexpr int BUTTON_WIDTH = 32; // Slightly wider for better touch targets
constexpr int BUTTON_HEIGHT = 32; // Square buttons look more modern
constexpr int UI_PADDING = 10; // Standard padding between elements

// Colors
constexpr COLORREF DARK_GRAY = RGB(64, 64, 64); // Dark gray color for button backgrounds
constexpr COLORREF BUTTON_TEXT_COLOR = RGB(255, 255, 255); // White text for buttons

// Special paths
constexpr wchar_t THIS_PC_NAME[] = L"This PC";

HICON g_hBackIcon = NULL;
HICON g_hForwardIcon = NULL;
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
HFONT g_hFont = NULL;
fs::path g_currentPath;

// Original window procedure for the address bar
WNDPROC g_oldAddressBarProc = NULL;
WNDPROC g_oldBackButtonProc = NULL;
WNDPROC g_oldForwardButtonProc = NULL;

// Navigation history
std::deque<fs::path> g_backHistory;
std::deque<fs::path> g_forwardHistory;
bool g_navigatingHistory = false;

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
            SetWindowPos(g_hwndAddressBar, NULL, addressBarX, UI_PADDING + 5, width - addressBarX - 45, 25,
                         SWP_NOZORDER);
            SetWindowPos(g_hwndGoButton, NULL, width - 40, UI_PADDING + 5, 30, 25, SWP_NOZORDER);

            // Resize list view
            SetWindowPos(g_hwndListView, NULL, 0, BUTTON_HEIGHT + 20, width, height - (BUTTON_HEIGHT + 20),
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
    }

    // Initialize back and forward icons
    g_hBackIcon = NULL;
    g_hForwardIcon = NULL;

    // Load standard system arrow icons - direct approach
    HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
    if (hShell32)
    {
        // Try several common icon indices
        const int backIconIds[] = {3, 77, 132, 134, 136, 308};
        const int forwardIconIds[] = {4, 78, 133, 135, 137, 309};

        // Try each icon index until we find ones that work
        for (int i = 0; i < _countof(backIconIds); i++)
        {
            if (!g_hBackIcon)
            {
                g_hBackIcon = (HICON)LoadImageW(
                    hShell32,
                    MAKEINTRESOURCEW(backIconIds[i]),
                    IMAGE_ICON,
                    ICON_SIZE, ICON_SIZE,
                    LR_DEFAULTCOLOR
                );
            }

            if (!g_hForwardIcon)
            {
                g_hForwardIcon = (HICON)LoadImageW(
                    hShell32,
                    MAKEINTRESOURCEW(forwardIconIds[i]),
                    IMAGE_ICON,
                    ICON_SIZE, ICON_SIZE,
                    LR_DEFAULTCOLOR
                );
            }

            // Break if we loaded both icons
            if (g_hBackIcon && g_hForwardIcon)
            {
                break;
            }
        }

        FreeLibrary(hShell32);
    }

    // Fallback to system arrows if necessary
    if (!g_hBackIcon)
        g_hBackIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!g_hForwardIcon)
        g_hForwardIcon = LoadIcon(NULL, IDI_APPLICATION);

    // Create STANDARD buttons (not custom) which better handle icons
    g_hwndBackButton = CreateWindowW(
        L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_ICON,
        UI_PADDING, UI_PADDING, BUTTON_WIDTH, BUTTON_HEIGHT,
        g_hwndMain, (HMENU)(INT_PTR)ID_BACK_BUTTON, hInstance, NULL
    );

    g_hwndForwardButton = CreateWindowW(
        L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_ICON,
        UI_PADDING + BUTTON_WIDTH + 5, UI_PADDING, BUTTON_WIDTH, BUTTON_HEIGHT,
        g_hwndMain, (HMENU)(INT_PTR)ID_FORWARD_BUTTON, hInstance, NULL
    );

    // Set button icons using a more reliable method
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