#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <filesystem>

#include "file_explorer.hpp"

namespace fs = std::filesystem;

class FileListView {
public:
    enum class ViewMode {
        Details,
        List,
        Icons,
        Tiles
    };
    
    FileListView(HWND parentWindow);
    ~FileListView();
    
    // Create the list view control
    bool create();
    
    // Get handle to the list view
    HWND getHandle() const;
    
    // Set the view mode
    void setViewMode(ViewMode mode);
    
    // Get the current view mode
    ViewMode getViewMode() const;
    
    // Load files into the list view
    void loadFiles(const std::vector<FileExplorer::FileItem>& files);
    
    // Clear all items from the list view
    void clear();
    
    // Get selected items
    std::vector<size_t> getSelectedIndices() const;
    std::vector<FileExplorer::FileItem> getSelectedItems() const;
    
    // Set selection
    void setSelectedIndex(size_t index);
    void setSelectedIndices(const std::vector<size_t>& indices);
    
    // Enable/disable VirtualListView for better performance
    void enableVirtualMode(bool enable);
    
    // Resize the control
    void resize(int width, int height);
    
    // Set callback for double-click
    using ItemActivatedCallback = std::function<void(const FileExplorer::FileItem&)>;
    void setItemActivatedCallback(ItemActivatedCallback callback);
    
    // Set callback for selection change
    using SelectionChangedCallback = std::function<void(const std::vector<FileExplorer::FileItem>&)>;
    void setSelectionChangedCallback(SelectionChangedCallback callback);
    
    // Sort by column
    void sortByColumn(int column, bool ascending);

private:
    HWND m_parentHwnd;
    HWND m_hwnd;
    ViewMode m_viewMode;
    
    // Icon cache
    struct IconCacheEntry {
        HICON icon;
        int imageIndex;
    };
    std::unordered_map<std::string, IconCacheEntry> m_iconCache;
    HIMAGELIST m_imageList;
    
    // File data
    std::vector<FileExplorer::FileItem> m_files;
    
    // Callbacks
    ItemActivatedCallback m_itemActivatedCallback;
    SelectionChangedCallback m_selectionChangedCallback;
    
    // Initialize list view columns
    void initializeColumns();
    
    // Update image list
    void updateImageList();
    
    // Get icon for a file
    int getIconIndex(const FileExplorer::FileItem& item);
    
    // Process notifications
    void onItemActivated(NMITEMACTIVATE* pnmia);
    void onItemChanged(NMLISTVIEW* pnmlv);
    void onColumnClick(NMLISTVIEW* pnmlv);
    
    // Callback for virtual list view
    static LRESULT CALLBACK listViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
};
