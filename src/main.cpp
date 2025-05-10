#include <windows.h>
#include <commctrl.h>
#include <fmt/core.h>
#include <filesystem>
#include <exception>
#include <iostream>

#include "main_window.hpp"

// Link with Comctl32.lib
#pragma comment(lib, "comctl32.lib")

// Enable visual styles for modern UI
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

// Windows entry point for GUI applications
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    try {
        // Initialize common controls
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES |
                     ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS;
        InitCommonControlsEx(&icex);

        // Create main window
        auto mainWindow = std::make_unique<MainWindow>();
        if (!mainWindow->create()) {
            MessageBoxW(NULL, L"Failed to create main window!", L"Error", MB_ICONERROR | MB_OK);
            return 1;
        }

        // Process window messages
        mainWindow->processMessages();

        return 0;
    }
    catch (const std::exception& e) {
        std::string errorMessage = fmt::format("Fatal error: {}", e.what());
        MessageBoxA(NULL, errorMessage.c_str(), "Error", MB_ICONERROR | MB_OK);
        return 1;
    }
    catch (...) {
        MessageBoxW(NULL, L"Unknown fatal error occurred!", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }
}