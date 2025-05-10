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

// Debug logging helper function
void log(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);

    wchar_t buffer[1024];
    vswprintf_s(buffer, format, args);

    OutputDebugStringW(buffer);
    va_end(args);
}

// Windows entry point for GUI applications
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    try {
        // Enable debug output
        log(L"Fast File Explorer starting...\n");

        // Initialize common controls with error handling
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES |
                     ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS;

        if (!InitCommonControlsEx(&icex)) {
            DWORD error = GetLastError();
            log(L"InitCommonControlsEx failed with error %d\n", error);
            MessageBoxW(NULL, L"Failed to initialize common controls!", L"Error", MB_ICONERROR | MB_OK);
            return 1;
        }

        log(L"Common controls initialized\n");

        // Create main window
        auto mainWindow = std::make_unique<MainWindow>();
        if (!mainWindow->create()) {
            log(L"Failed to create main window\n");
            MessageBoxW(NULL, L"Failed to create main window!", L"Error", MB_ICONERROR | MB_OK);
            return 1;
        }

        log(L"Main window created\n");

        // Make sure the window is visible and properly sized
        ShowWindow(mainWindow->getHandle(), nCmdShow);
        UpdateWindow(mainWindow->getHandle());

        log(L"Window shown, processing messages...\n");

        // Process window messages
        mainWindow->processMessages();

        log(L"Application terminating normally\n");

        return 0;
    }
    catch (const std::exception& e) {
        std::string errorMessage = fmt::format("Fatal error: {}", e.what());
        log(L"Exception caught: %hs\n", errorMessage.c_str());
        MessageBoxA(NULL, errorMessage.c_str(), "Error", MB_ICONERROR | MB_OK);
        return 1;
    }
    catch (...) {
        log(L"Unknown exception caught\n");
        MessageBoxW(NULL, L"Unknown fatal error occurred!", L"Error", MB_ICONERROR | MB_OK);
        return 1;
    }
}