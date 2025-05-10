#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <format>
#include <Uxtheme.h>

// Link with required libraries
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Constants
constexpr int BUTTON_SIZE = 40;
constexpr int ICON_SIZE = 24;
constexpr int BUTTONS_PER_ROW = 16;
constexpr int PADDING = 5;
constexpr int MAX_STOCK_ICON_ID = 365; // We'll display icon IDs from 0 to this value

// Window class name
constexpr wchar_t WINDOW_CLASS_NAME[] = L"IconBrowserWindow";

// Message when user clicks a button
constexpr int WM_ICON_BUTTON_CLICKED = WM_USER + 100;

// Button data
struct IconButtonData {
    int iconId;
    HWND hwnd;
};

// Global variables
std::vector<IconButtonData> g_buttons;
HWND g_hwndMain = NULL;
HWND g_hwndStatusBar = NULL;

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Subclass procedure for buttons
WNDPROC g_originalButtonProc = nullptr;

// Helper function to get icon from system
HICON GetSystemIconById(int iconId) {
    HICON hIcon = NULL;
    
    // Try to extract icon from shell32.dll first
    hIcon = ExtractIcon(GetModuleHandle(NULL), L"shell32.dll", iconId);
    
    // If that fails, try imageres.dll (Windows 7+)
    if (hIcon == NULL || hIcon == (HICON)1) {
        hIcon = ExtractIcon(GetModuleHandle(NULL), L"imageres.dll", iconId);
    }
    
    return (hIcon == (HICON)1) ? NULL : hIcon;
}

// Helper function to copy text to clipboard
void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(NULL))
        return;
    
    EmptyClipboard();
    
    // Allocate global memory for the text
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.length() + 1) * sizeof(wchar_t));
    if (hMem) {
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        if (pMem) {
            wcscpy_s(pMem, text.length() + 1, text.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    
    CloseClipboard();
}

// Create all icon buttons
void CreateIconButtons(HWND hwnd) {
    // Get window client area
    RECT rect;
    GetClientRect(hwnd, &rect);
    
    // Calculate window width without padding
    int usableWidth = rect.right - (2 * PADDING);
    
    // Calculate how many buttons we can fit per row
    int buttonsPerRow = usableWidth / BUTTON_SIZE;
    if (buttonsPerRow < 1) buttonsPerRow = 1;
    
    // Clear any existing buttons
    for (const auto& button : g_buttons) {
        DestroyWindow(button.hwnd);
    }
    g_buttons.clear();
    
    // Create buttons for each icon ID
    for (int i = 0; i <= MAX_STOCK_ICON_ID; i++) {
        // Calculate button position
        int row = i / buttonsPerRow;
        int col = i % buttonsPerRow;
        int x = PADDING + (col * BUTTON_SIZE);
        int y = PADDING + (row * BUTTON_SIZE);
        
        // Create the button
        HWND hwndButton = CreateWindow(
            L"BUTTON", 
            std::to_wstring(i).c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_BITMAP,
            x, y, BUTTON_SIZE, BUTTON_SIZE,
            hwnd, (HMENU)(INT_PTR)i, GetModuleHandle(NULL), NULL
        );
        
        if (hwndButton) {
            // Subclass the button
            SetWindowLongPtr(hwndButton, GWLP_USERDATA, i); // Store icon ID
            g_originalButtonProc = (WNDPROC)SetWindowLongPtr(hwndButton, GWLP_WNDPROC, (LONG_PTR)ButtonProc);
            
            // Load icon
            HICON hIcon = GetSystemIconById(i);
            if (hIcon) {
                // Set icon to the button
                SendMessage(hwndButton, BM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);
                
                // Store button data
                g_buttons.push_back({i, hwndButton});
            } else {
                // No icon found, use text instead
                SetWindowText(hwndButton, std::to_wstring(i).c_str());
            }
        }
    }
    
    // Calculate required window height
    int rowCount = (MAX_STOCK_ICON_ID + buttonsPerRow) / buttonsPerRow;
    int requiredHeight = (rowCount * BUTTON_SIZE) + (2 * PADDING);
    
    // Update status bar
    SetWindowText(g_hwndStatusBar, L"Click an icon to copy its ID to clipboard");
}

// Initialize the window
BOOL InitWindow(HINSTANCE hInstance) {
    // Register the window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = WINDOW_CLASS_NAME;
    
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Window Registration Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return FALSE;
    }
    
    // Initialize common controls
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    
    // Create the window
    g_hwndMain = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        WINDOW_CLASS_NAME,
        L"Icon Browser - Click to Copy Icon ID",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT, 
        800, 600,
        NULL, NULL, hInstance, NULL
    );
    
    if (!g_hwndMain) {
        MessageBox(NULL, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return FALSE;
    }
    
    // Create status bar
    g_hwndStatusBar = CreateWindowEx(
        0, STATUSCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        g_hwndMain, NULL, hInstance, NULL
    );
    
    // Show the window
    ShowWindow(g_hwndMain, SW_SHOW);
    UpdateWindow(g_hwndMain);
    
    return TRUE;
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            // Create icon buttons
            CreateIconButtons(hwnd);
            return 0;
            
        case WM_SIZE:
            // Recreate buttons when window is resized
            CreateIconButtons(hwnd);
            
            // Resize status bar
            SendMessage(g_hwndStatusBar, WM_SIZE, 0, 0);
            return 0;
            
        case WM_COMMAND:
            // Handle button clicks
            {
                int buttonId = LOWORD(wParam);
                std::wstring idText = std::format(L"{}", buttonId);
                CopyToClipboard(idText);
                
                // Show feedback in status bar
                std::wstring statusText = std::format(L"Copied icon ID: {}", buttonId);
                SetWindowText(g_hwndStatusBar, statusText.c_str());
            }
            return 0;
            
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Button subclass procedure
LRESULT CALLBACK ButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_LBUTTONUP:
            {
                // Get icon ID stored in button's user data
                int iconId = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);
                
                // Copy to clipboard
                std::wstring idText = std::format(L"{}", iconId);
                CopyToClipboard(idText);
                
                // Show feedback in status bar
                std::wstring statusText = std::format(L"Copied icon ID: {}", iconId);
                SetWindowText(g_hwndStatusBar, statusText.c_str());
                
                // Let the button also process this message
                return CallWindowProc(g_originalButtonProc, hwnd, uMsg, wParam, lParam);
            }
    }
    
    return CallWindowProc(g_originalButtonProc, hwnd, uMsg, wParam, lParam);
}

// Main entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Initialize window
    if (!InitWindow(hInstance)) {
        return 0;
    }
    
    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}