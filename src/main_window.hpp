#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <atomic>

#include "file_explorer.hpp"
#include "file_list_view.hpp"

namespace fs = std::filesystem;

class MainWindow {
public:
    MainWindow();
    ~MainWindow();
    
    // Create and show the window
    bool create();
    
    // Process Windows messages
    void processMessages();
    
    // Get window handle
    HWND getHandle() const;

private:
    // Window handle
    HWND m_hwnd;
    
    // File explorer instance
    std::unique_ptr<FileExplorer> m_fileExplorer;
    
    // UI components
    HWND m_addressBar;
    HWND m_searchBox;
    std::unique_ptr<FileListView> m_fileListView;
    HWND m_statusBar;
    HWND m_sideBar;
    
    // Status flags
    std::atomic<bool> m_isLoading{false};
    
    // Window procedure
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    
    // Message handlers
    void onCreate();
    void onSize(int width, int height);
    void onCommand(int id, int notifyCode, HWND control);
    void onNotify(NMHDR* nmhdr);
    void onAddressBarTextChanged();
    void onSearchBoxTextChanged();
    void onNavigateButtonClicked();
    void onRefreshButtonClicked();
    void onFilterButtonClicked();
    void onSortButtonClicked();
    void onViewButtonClicked();
    
    // UI update methods
    void updateTitle();
    void updateAddressBar();
    void updateStatusBar();
    
    // File explorer callbacks
    void onFilesLoaded(const std::vector<FileExplorer::FileItem>& files);
    void onError(const std::string& errorMessage);
    
    // Create UI components
    void createToolbar();
    void createAddressBar();
    void createSearchBox();
    void createFileListView();
    void createStatusBar();
    void createSideBar();
    
    // Register window class
    static bool registerWindowClass();
};
