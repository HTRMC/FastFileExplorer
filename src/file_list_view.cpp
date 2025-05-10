#include "file_list_view.hpp"
#include "file_system_utils.hpp"
#include <windowsx.h>
#include <format>
#include <unordered_map>
#include <algorithm>

// Control IDs
constexpr int ID_FILE_LIST = 103;

// Column indices
enum {
    COLUMN_NAME = 0,
    COLUMN_SIZE,
    COLUMN_TYPE,
    COLUMN_DATE,
    COLUMN_COUNT
};

// Column widths
constexpr int COLUMN_WIDTH_NAME = 250;
constexpr int COLUMN_WIDTH_SIZE = 100;
constexpr int COLUMN_WIDTH_TYPE = 150;
constexpr int COLUMN_WIDTH_DATE = 150;

FileListView::FileListView(HWND parentWindow)
    : m_parentHwnd(parentWindow)
    , m_hwnd(NULL)
    , m_viewMode(ViewMode::Details)
    , m_imageList(NULL)
{
}

FileListView::~FileListView() {
    // Free icon cache
    for (auto& entry : m_iconCache) {
        if (entry.second.icon) {
            DestroyIcon(entry.second.icon);
        }
    }
    
    // Free image list
    if (m_imageList) {
        ImageList_Destroy(m_imageList);
    }
}

bool FileListView::create() {
    // Create list view control
    m_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS,
        0, 0, 100, 100,  // Initial size (will be resized)
        m_parentHwnd,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(ID_FILE_LIST)),  // Using ID constant
        GetModuleHandle(NULL),
        NULL
    );
    
    if (!m_hwnd) {
        DWORD error = GetLastError();
        wchar_t buffer[256];
        swprintf_s(buffer, L"Failed to create list view. Error code: %d\n", error);
        OutputDebugStringW(buffer);
        return false;
    }
    
    OutputDebugStringW(L"List view control created\n");

    // Set extended list view styles
    DWORD exStyle = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP;
    ListView_SetExtendedListViewStyle(m_hwnd, exStyle);
    
    // Create image list for icons
    m_imageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
    
    // Set image list
    ListView_SetImageList(m_hwnd, m_imageList, LVSIL_SMALL);
    
    // Initialize columns
    initializeColumns();
    
    // Set subclass procedure for custom handling
    if (!SetWindowSubclass(m_hwnd, FileListView::listViewProc, 0, reinterpret_cast<DWORD_PTR>(this))) {
        OutputDebugStringW(L"Failed to set subclass procedure\n");
    }

    // Ensure visibility
    ShowWindow(m_hwnd, SW_SHOW);
    
    return true;
}

HWND FileListView::getHandle() const {
    return m_hwnd;
}

void FileListView::setViewMode(ViewMode mode) {
    if (m_viewMode == mode) {
        return;
    }
    
    m_viewMode = mode;
    
    // Change list view style
    DWORD style = GetWindowLongW(m_hwnd, GWL_STYLE);
    style &= ~(LVS_TYPEMASK);
    
    switch (mode) {
        case ViewMode::Details:
            style |= LVS_REPORT;
            break;
            
        case ViewMode::List:
            style |= LVS_LIST;
            break;
            
        case ViewMode::Icons:
            style |= LVS_ICON;
            break;
            
        case ViewMode::Tiles:
            style |= LVS_SMALLICON;
            break;
    }
    
    SetWindowLongW(m_hwnd, GWL_STYLE, style);
    
    // Reload files to update the view
    loadFiles(m_files);
}

FileListView::ViewMode FileListView::getViewMode() const {
    return m_viewMode;
}

void FileListView::loadFiles(const std::vector<FileExplorer::FileItem>& files) {
    // Store the files
    m_files = files;
    
    // Clear the list view
    ListView_DeleteAllItems(m_hwnd);
    
    OutputDebugStringW((L"Loading " + std::to_wstring(files.size()) + L" files\n").c_str());

    // Add items to the list view
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];
        
        // Get icon for the file
        int iconIndex = getIconIndex(file);
        
        // Format size
        std::string sizeStr;
        if (fs::is_directory(file.path)) {
            sizeStr = "";
        }
        else {
            sizeStr = FileSystemUtils::formatFileSize(file.size);
        }
        
        // Format date
        auto timePoint = file.last_write_time;
        auto time = std::chrono::system_clock::to_time_t(timePoint);
        std::tm tm = {};
        localtime_s(&tm, &time);
        
        // Format date as "YYYY-MM-DD HH:MM:SS"
        std::string dateStr = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
        
        // Get file type description
        std::string typeStr = FileSystemUtils::getFileTypeDescription(file.path);
        
        // Add the item
        LVITEMW lvItem = {};
        lvItem.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        lvItem.iItem = static_cast<int>(i);
        lvItem.iSubItem = 0;
        lvItem.iImage = iconIndex;
        lvItem.lParam = static_cast<LPARAM>(i);  // Store index for lookup
        
        // Convert filename to wide string
        std::wstring nameW;
        if (!file.name.empty()) {
            size_t convertedChars = 0;
            const size_t newsize = file.name.length() + 1;
            wchar_t* wcstring = new wchar_t[newsize];
            mbstowcs_s(&convertedChars, wcstring, newsize, file.name.c_str(), _TRUNCATE);
            nameW = wcstring;
            delete[] wcstring;
            lvItem.pszText = const_cast<LPWSTR>(nameW.c_str());
        } else {
            lvItem.pszText = const_cast<LPWSTR>(L"");
        }
        
        int itemIndex = ListView_InsertItem(m_hwnd, &lvItem);
        
        if (itemIndex == -1) {
            OutputDebugStringW(L"Failed to insert item\n");
            continue;
        }
        
        // Set subitems
        // Convert size to wide string
        std::wstring sizeW;
        if (!sizeStr.empty()) {
            size_t convertedChars = 0;
            const size_t newsize = sizeStr.length() + 1;
            wchar_t* wcstring = new wchar_t[newsize];
            mbstowcs_s(&convertedChars, wcstring, newsize, sizeStr.c_str(), _TRUNCATE);
            sizeW = wcstring;
            delete[] wcstring;
        } else {
            sizeW = L"";
        }
        ListView_SetItemText(m_hwnd, itemIndex, COLUMN_SIZE, const_cast<LPWSTR>(sizeW.c_str()));

        // Convert type to wide string
        std::wstring typeW;
        if (!typeStr.empty()) {
            size_t convertedChars = 0;
            const size_t newsize = typeStr.length() + 1;
            wchar_t* wcstring = new wchar_t[newsize];
            mbstowcs_s(&convertedChars, wcstring, newsize, typeStr.c_str(), _TRUNCATE);
            typeW = wcstring;
            delete[] wcstring;
        } else {
            typeW = L"";
        }
        ListView_SetItemText(m_hwnd, itemIndex, COLUMN_TYPE, const_cast<LPWSTR>(typeW.c_str()));

        // Convert date to wide string
        std::wstring dateW;
        if (!dateStr.empty()) {
            size_t convertedChars = 0;
            const size_t newsize = dateStr.length() + 1;
            wchar_t* wcstring = new wchar_t[newsize];
            mbstowcs_s(&convertedChars, wcstring, newsize, dateStr.c_str(), _TRUNCATE);
            dateW = wcstring;
            delete[] wcstring;
        } else {
            dateW = L"";
        }
        ListView_SetItemText(m_hwnd, itemIndex, COLUMN_DATE, const_cast<LPWSTR>(dateW.c_str()));
    }

    // Force redraw
    InvalidateRect(m_hwnd, NULL, TRUE);
}

void FileListView::clear() {
    ListView_DeleteAllItems(m_hwnd);
    m_files.clear();
}

std::vector<size_t> FileListView::getSelectedIndices() const {
    std::vector<size_t> indices;
    
    int itemCount = ListView_GetItemCount(m_hwnd);
    for (int i = 0; i < itemCount; ++i) {
        if (ListView_GetItemState(m_hwnd, i, LVIS_SELECTED) & LVIS_SELECTED) {
            indices.push_back(static_cast<size_t>(i));
        }
    }
    
    return indices;
}

std::vector<FileExplorer::FileItem> FileListView::getSelectedItems() const {
    std::vector<FileExplorer::FileItem> items;
    
    int itemCount = ListView_GetItemCount(m_hwnd);
    for (int i = 0; i < itemCount; ++i) {
        if (ListView_GetItemState(m_hwnd, i, LVIS_SELECTED) & LVIS_SELECTED) {
            LVITEMW lvItem = {};
            lvItem.mask = LVIF_PARAM;
            lvItem.iItem = i;
            ListView_GetItem(m_hwnd, &lvItem);
            
            size_t index = static_cast<size_t>(lvItem.lParam);
            if (index < m_files.size()) {
                items.push_back(m_files[index]);
            }
        }
    }
    
    return items;
}

void FileListView::setSelectedIndex(size_t index) {
    if (index < static_cast<size_t>(ListView_GetItemCount(m_hwnd))) {
        ListView_SetItemState(m_hwnd, static_cast<int>(index), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

void FileListView::setSelectedIndices(const std::vector<size_t>& indices) {
    // Clear current selection
    ListView_SetItemState(m_hwnd, -1, 0, LVIS_SELECTED);
    
    // Set new selection
    for (size_t index : indices) {
        if (index < static_cast<size_t>(ListView_GetItemCount(m_hwnd))) {
            ListView_SetItemState(m_hwnd, static_cast<int>(index), LVIS_SELECTED, LVIS_SELECTED);
        }
    }
    
    // Set focus to the first selected item
    if (!indices.empty()) {
        ListView_SetItemState(m_hwnd, static_cast<int>(indices[0]), LVIS_FOCUSED, LVIS_FOCUSED);
    }
}

void FileListView::enableVirtualMode(bool enable) {
    DWORD style = GetWindowLongW(m_hwnd, GWL_STYLE);
    
    if (enable) {
        style |= LVS_OWNERDATA;
    }
    else {
        style &= ~LVS_OWNERDATA;
    }
    
    SetWindowLongW(m_hwnd, GWL_STYLE, style);
}

void FileListView::resize(int width, int height) {
    // Resize the columns
    if (m_viewMode == ViewMode::Details) {
        // Name column gets extra space
        int nameWidth = width - COLUMN_WIDTH_SIZE - COLUMN_WIDTH_TYPE - COLUMN_WIDTH_DATE;
        if (nameWidth < 100) nameWidth = 100;  // Minimum width
        
        ListView_SetColumnWidth(m_hwnd, COLUMN_NAME, nameWidth);
        ListView_SetColumnWidth(m_hwnd, COLUMN_SIZE, COLUMN_WIDTH_SIZE);
        ListView_SetColumnWidth(m_hwnd, COLUMN_TYPE, COLUMN_WIDTH_TYPE);
        ListView_SetColumnWidth(m_hwnd, COLUMN_DATE, COLUMN_WIDTH_DATE);
    }
}

void FileListView::setItemActivatedCallback(ItemActivatedCallback callback) {
    m_itemActivatedCallback = std::move(callback);
}

void FileListView::setSelectionChangedCallback(SelectionChangedCallback callback) {
    m_selectionChangedCallback = std::move(callback);
}

void FileListView::sortByColumn(int column, bool ascending) {
    // Map column index to sort criteria
    FileExplorer::SortCriteria criteria;
    switch (column) {
        case COLUMN_NAME: criteria = FileExplorer::SortCriteria::Name; break;
        case COLUMN_SIZE: criteria = FileExplorer::SortCriteria::Size; break;
        case COLUMN_TYPE: criteria = FileExplorer::SortCriteria::Type; break;
        case COLUMN_DATE: criteria = FileExplorer::SortCriteria::Date; break;
        default: criteria = FileExplorer::SortCriteria::Name; break;
    }
    
    // Sort the files
    auto sortFunction = [criteria, ascending](const FileExplorer::FileItem& a, const FileExplorer::FileItem& b) {
        bool result = false;
        
        // First sort by directory/file
        bool aIsDir = fs::is_directory(a.path);
        bool bIsDir = fs::is_directory(b.path);
        
        if (aIsDir && !bIsDir) return true;
        if (!aIsDir && bIsDir) return false;
        
        // Then sort by the specified criteria
        switch (criteria) {
            case FileExplorer::SortCriteria::Name:
                result = a.name < b.name;
                break;
            case FileExplorer::SortCriteria::Size:
                result = a.size < b.size;
                break;
            case FileExplorer::SortCriteria::Type:
                result = a.path.extension().string() < b.path.extension().string();
                break;
            case FileExplorer::SortCriteria::Date:
                result = a.last_write_time < b.last_write_time;
                break;
        }
        
        // Apply sort order
        return ascending ? result : !result;
    };
    
    // Sort the files
    std::sort(m_files.begin(), m_files.end(), sortFunction);
    
    // Reload the list view
    loadFiles(m_files);
}

void FileListView::initializeColumns() {
    // Define columns
    LVCOLUMNW lvc = {};
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    
    // Name column
    lvc.iSubItem = COLUMN_NAME;
    lvc.pszText = const_cast<LPWSTR>(L"Name");
    lvc.cx = COLUMN_WIDTH_NAME;
    lvc.fmt = LVCFMT_LEFT;
    if (ListView_InsertColumn(m_hwnd, COLUMN_NAME, &lvc) == -1) {
        OutputDebugStringW(L"Failed to insert Name column\n");
    }
    
    // Size column
    lvc.iSubItem = COLUMN_SIZE;
    lvc.pszText = const_cast<LPWSTR>(L"Size");
    lvc.cx = COLUMN_WIDTH_SIZE;
    lvc.fmt = LVCFMT_RIGHT;
    if (ListView_InsertColumn(m_hwnd, COLUMN_SIZE, &lvc) == -1) {
        OutputDebugStringW(L"Failed to insert Size column\n");
    }
    
    // Type column
    lvc.iSubItem = COLUMN_TYPE;
    lvc.pszText = const_cast<LPWSTR>(L"Type");
    lvc.cx = COLUMN_WIDTH_TYPE;
    lvc.fmt = LVCFMT_LEFT;
    if (ListView_InsertColumn(m_hwnd, COLUMN_TYPE, &lvc) == -1) {
        OutputDebugStringW(L"Failed to insert Type column\n");
    }
    
    // Date column
    lvc.iSubItem = COLUMN_DATE;
    lvc.pszText = const_cast<LPWSTR>(L"Date modified");
    lvc.cx = COLUMN_WIDTH_DATE;
    lvc.fmt = LVCFMT_LEFT;
    if (ListView_InsertColumn(m_hwnd, COLUMN_DATE, &lvc) == -1) {
        OutputDebugStringW(L"Failed to insert Date column\n");
    }

    OutputDebugStringW(L"Columns initialized\n");
}

void FileListView::updateImageList() {
    // Clear old image list
    if (m_imageList) {
        ImageList_Destroy(m_imageList);
    }
    
    // Create new image list
    m_imageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 16, 16);
    
    // Set image list
    ListView_SetImageList(m_hwnd, m_imageList, LVSIL_SMALL);
    
    // Clear icon cache
    for (auto& entry : m_iconCache) {
        if (entry.second.icon) {
            DestroyIcon(entry.second.icon);
        }
    }
    m_iconCache.clear();
}

int FileListView::getIconIndex(const FileExplorer::FileItem& item) {
    std::string ext = item.path.extension().string();
    
    // Use path for directories, extension for files
    std::string cacheKey = fs::is_directory(item.path) ? "dir:" + item.path.string() : ext;
    
    // Check cache
    auto it = m_iconCache.find(cacheKey);
    if (it != m_iconCache.end()) {
        return it->second.imageIndex;
    }
    
    // Get icon from shell
    HICON hIcon = FileSystemUtils::getFileIcon(item.path, false);
    if (!hIcon) {
        return 0;  // Default icon
    }
    
    // Add icon to image list
    int imageIndex = ImageList_AddIcon(m_imageList, hIcon);
    
    // Add to cache
    m_iconCache[cacheKey] = { hIcon, imageIndex };
    
    return imageIndex;
}

void FileListView::onItemActivated(NMITEMACTIVATE* pnmia) {
    int itemIndex = pnmia->iItem;
    if (itemIndex < 0 || itemIndex >= static_cast<int>(m_files.size())) {
        return;
    }
    
    // Call callback
    if (m_itemActivatedCallback) {
        m_itemActivatedCallback(m_files[itemIndex]);
    }
}

void FileListView::onItemChanged(NMLISTVIEW* pnmlv) {
    if ((pnmlv->uChanged & LVIF_STATE) && 
        (pnmlv->uNewState & LVIS_SELECTED || pnmlv->uOldState & LVIS_SELECTED)) {
        
        // Call callback with selected items
        if (m_selectionChangedCallback) {
            m_selectionChangedCallback(getSelectedItems());
        }
    }
}

void FileListView::onColumnClick(NMLISTVIEW* pnmlv) {
    int columnIndex = pnmlv->iSubItem;
    
    // Toggle sort order
    static bool ascending = true;
    static int lastColumn = -1;
    
    if (columnIndex == lastColumn) {
        ascending = !ascending;
    }
    else {
        ascending = true;
        lastColumn = columnIndex;
    }
    
    // Sort by column
    sortByColumn(columnIndex, ascending);
}

LRESULT CALLBACK FileListView::listViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    FileListView* pThis = reinterpret_cast<FileListView*>(dwRefData);
    
    switch (msg) {
        case WM_NOTIFY:
        {
            NMHDR* pnmhdr = reinterpret_cast<NMHDR*>(lParam);
            
            switch (pnmhdr->code) {
                case LVN_ITEMACTIVATE:
                    pThis->onItemActivated(reinterpret_cast<NMITEMACTIVATE*>(pnmhdr));
                    break;
                    
                case LVN_ITEMCHANGED:
                    pThis->onItemChanged(reinterpret_cast<NMLISTVIEW*>(pnmhdr));
                    break;
                    
                case LVN_COLUMNCLICK:
                    pThis->onColumnClick(reinterpret_cast<NMLISTVIEW*>(pnmhdr));
                    break;
            }
            break;
        }
    }
    
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
