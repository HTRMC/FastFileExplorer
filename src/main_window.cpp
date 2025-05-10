#include "main_window.hpp"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <format>
#include <string>

// Window class name
constexpr wchar_t WINDOW_CLASS_NAME[] = L"FastFileExplorerMainWindow";

// Window styles
constexpr DWORD WINDOW_STYLE = WS_OVERLAPPEDWINDOW;
constexpr DWORD WINDOW_STYLE_EX = WS_EX_ACCEPTFILES;

// Window dimensions
constexpr int WINDOW_WIDTH = 1024;
constexpr int WINDOW_HEIGHT = 768;

// Control IDs
constexpr int ID_TOOLBAR = 100;
constexpr int ID_ADDRESS_BAR = 101;
constexpr int ID_SEARCH_BOX = 102;
constexpr int ID_FILE_LIST = 103;
constexpr int ID_STATUS_BAR = 104;
constexpr int ID_SIDE_BAR = 105;

// Menu IDs
constexpr int ID_MENU_BACK = 1001;
constexpr int ID_MENU_FORWARD = 1002;
constexpr int ID_MENU_UP = 1003;
constexpr int ID_MENU_REFRESH = 1004;
constexpr int ID_MENU_NAVIGATE = 1005;
constexpr int ID_MENU_FILTER = 1006;
constexpr int ID_MENU_SORT = 1007;
constexpr int ID_MENU_VIEW = 1008;

// Register the window class
bool MainWindow::registerWindowClass() {
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = MainWindow::windowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = WINDOW_CLASS_NAME;
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    return RegisterClassExW(&wcex) != 0;
}

MainWindow::MainWindow()
    : m_hwnd(NULL)
    , m_addressBar(NULL)
    , m_searchBox(NULL)
    , m_statusBar(NULL)
    , m_sideBar(NULL)
{
    m_fileExplorer = std::make_unique<FileExplorer>();
}

MainWindow::~MainWindow() {
    // Resources will be automatically cleaned up
}

bool MainWindow::create() {
    // Register window class if not already registered
    static bool registered = registerWindowClass();
    if (!registered) {
        return false;
    }
    
    // Create the window
    m_hwnd = CreateWindowExW(
        WINDOW_STYLE_EX,
        WINDOW_CLASS_NAME,
        L"Fast File Explorer",
        WINDOW_STYLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        this
    );
    
    if (!m_hwnd) {
        return false;
    }
    
    // Set window pointer
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    
    // Show the window
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    
    return true;
}

void MainWindow::processMessages() {
    MSG msg = {};
    
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

HWND MainWindow::getHandle() const {
    return m_hwnd;
}

LRESULT CALLBACK MainWindow::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Get the window pointer
    MainWindow* window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    
    switch (msg) {
        case WM_CREATE:
        {
            // Store the window pointer
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            window = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            
            // Initialize the window
            window->onCreate();
            return 0;
        }
        
        case WM_SIZE:
        {
            if (window) {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                window->onSize(width, height);
            }
            return 0;
        }
        
        case WM_COMMAND:
        {
            if (window) {
                int id = LOWORD(wParam);
                int notifyCode = HIWORD(wParam);
                HWND control = reinterpret_cast<HWND>(lParam);
                window->onCommand(id, notifyCode, control);
            }
            return 0;
        }
        
        case WM_NOTIFY:
        {
            if (window) {
                NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
                window->onNotify(nmhdr);
            }
            return 0;
        }
        
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
        
        case WM_DROPFILES:
        {
            if (window) {
                HDROP hDrop = reinterpret_cast<HDROP>(wParam);
                
                WCHAR szFilePath[MAX_PATH];
                UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
                
                if (fileCount > 0) {
                    // We'll just handle the first file
                    DragQueryFileW(hDrop, 0, szFilePath, MAX_PATH);
                    
                    fs::path path(szFilePath);
                    if (fs::is_directory(path)) {
                        window->m_fileExplorer->navigateTo(path);
                    }
                    else if (fs::exists(path)) {
                        // If it's a file, navigate to its parent
                        window->m_fileExplorer->navigateTo(path.parent_path());
                    }
                }
                
                DragFinish(hDrop);
            }
            return 0;
        }
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void MainWindow::onCreate() {
    // Set file explorer callbacks
    m_fileExplorer->setOnFilesLoadedCallback([this](const std::vector<FileExplorer::FileItem>& files) {
        onFilesLoaded(files);
    });
    
    m_fileExplorer->setOnErrorCallback([this](const std::string& errorMessage) {
        onError(errorMessage);
    });
    
    // Create UI components
    createToolbar();
    createAddressBar();
    createSearchBox();
    createFileListView();
    createStatusBar();
    createSideBar();
    
    // Update UI
    updateTitle();
    updateAddressBar();
    updateStatusBar();
    
    // Navigate to initial directory
    m_fileExplorer->navigateTo(m_fileExplorer->getCurrentPath());
}

void MainWindow::onSize(int width, int height) {
    // Layout constants
    const int TOOLBAR_HEIGHT = 40;
    const int ADDRESS_BAR_HEIGHT = 30;
    const int STATUS_BAR_HEIGHT = 22;
    const int SIDE_BAR_WIDTH = 200;
    const int PADDING = 5;
    
    // Resize toolbar (spans the entire width)
    if (m_addressBar) {
        SetWindowPos(m_addressBar, NULL, 
            SIDE_BAR_WIDTH + PADDING, PADDING, 
            width - SIDE_BAR_WIDTH - PADDING * 2, ADDRESS_BAR_HEIGHT, 
            SWP_NOZORDER);
    }
    
    // Resize search box (right side of address bar)
    if (m_searchBox) {
        SetWindowPos(m_searchBox, NULL, 
            width - 250 - PADDING, PADDING + ADDRESS_BAR_HEIGHT + PADDING, 
            250, ADDRESS_BAR_HEIGHT, 
            SWP_NOZORDER);
    }
    
    // Resize status bar (spans the entire width)
    if (m_statusBar) {
        SendMessage(m_statusBar, WM_SIZE, 0, 0);
    }
    
    // Resize sidebar (left side, full height except toolbar and status bar)
    if (m_sideBar) {
        SetWindowPos(m_sideBar, NULL, 
            PADDING, PADDING, 
            SIDE_BAR_WIDTH - PADDING * 2, height - TOOLBAR_HEIGHT - STATUS_BAR_HEIGHT - PADDING * 2, 
            SWP_NOZORDER);
    }
    
    // Resize file list view (center area)
    if (m_fileListView) {
        int fileListTop = TOOLBAR_HEIGHT + PADDING;
        int fileListHeight = height - TOOLBAR_HEIGHT - STATUS_BAR_HEIGHT - PADDING * 2;
        
        m_fileListView->resize(width - SIDE_BAR_WIDTH - PADDING * 2, fileListHeight);
        
        HWND fileListHwnd = m_fileListView->getHandle();
        SetWindowPos(fileListHwnd, NULL, 
            SIDE_BAR_WIDTH + PADDING, fileListTop, 
            width - SIDE_BAR_WIDTH - PADDING * 2, fileListHeight, 
            SWP_NOZORDER);
    }
}

void MainWindow::onCommand(int id, int notifyCode, HWND control) {
    switch (id) {
        case ID_MENU_BACK:
            // Handle back button
            break;
            
        case ID_MENU_FORWARD:
            // Handle forward button
            break;
            
        case ID_MENU_UP:
            m_fileExplorer->navigateUp();
            break;
            
        case ID_MENU_REFRESH:
            m_fileExplorer->refresh();
            break;
            
        case ID_MENU_NAVIGATE:
            onNavigateButtonClicked();
            break;
            
        case ID_MENU_FILTER:
            onFilterButtonClicked();
            break;
            
        case ID_MENU_SORT:
            onSortButtonClicked();
            break;
            
        case ID_MENU_VIEW:
            onViewButtonClicked();
            break;
            
        case ID_ADDRESS_BAR:
            if (notifyCode == EN_CHANGE) {
                onAddressBarTextChanged();
            }
            break;
            
        case ID_SEARCH_BOX:
            if (notifyCode == EN_CHANGE) {
                onSearchBoxTextChanged();
            }
            break;
    }
}

void MainWindow::onNotify(NMHDR* nmhdr) {
    // Handle notifications from controls
    if (m_fileListView && nmhdr->hwndFrom == m_fileListView->getHandle()) {
        switch (nmhdr->code) {
            case LVN_ITEMACTIVATE:
                // Handle item activation (double-click)
                {
                    NMITEMACTIVATE* pnmia = reinterpret_cast<NMITEMACTIVATE*>(nmhdr);
                    int itemIndex = pnmia->iItem;
                    
                    // Get selected items
                    std::vector<FileExplorer::FileItem> selectedItems = m_fileListView->getSelectedItems();
                    if (!selectedItems.empty()) {
                        const auto& item = selectedItems[0];
                        
                        if (fs::is_directory(item.path)) {
                            // Navigate to directory
                            m_fileExplorer->navigateTo(item.path);
                        }
                        else {
                            // Open file using shell execute
                            ShellExecuteW(m_hwnd, L"open", item.path.wstring().c_str(), NULL, NULL, SW_SHOW);
                        }
                    }
                }
                break;
                
            case LVN_COLUMNCLICK:
                // Handle column click for sorting
                {
                    NMLISTVIEW* pnmlv = reinterpret_cast<NMLISTVIEW*>(nmhdr);
                    int columnIndex = pnmlv->iSubItem;
                    
                    // Map column index to sort criteria
                    FileExplorer::SortCriteria criteria;
                    switch (columnIndex) {
                        case 0: criteria = FileExplorer::SortCriteria::Name; break;
                        case 1: criteria = FileExplorer::SortCriteria::Size; break;
                        case 2: criteria = FileExplorer::SortCriteria::Type; break;
                        case 3: criteria = FileExplorer::SortCriteria::Date; break;
                        default: criteria = FileExplorer::SortCriteria::Name; break;
                    }
                    
                    // Toggle sort order
                    static bool ascending = true;
                    ascending = !ascending;
                    
                    m_fileExplorer->sortFiles(criteria, 
                        ascending ? FileExplorer::SortOrder::Ascending : FileExplorer::SortOrder::Descending);
                }
                break;
        }
    }
}

void MainWindow::onAddressBarTextChanged() {
    // This method can be used to implement auto-complete
}

void MainWindow::onSearchBoxTextChanged() {
    // Get search text
    WCHAR buffer[256];
    GetWindowTextW(m_searchBox, buffer, 256);
    
    // Convert to UTF-8
    std::wstring wstr(buffer);
    std::string query(wstr.begin(), wstr.end());
    
    // Perform search
    if (!query.empty()) {
        m_fileExplorer->search(query);
    }
    else {
        // If search box is empty, show all files
        m_fileExplorer->refresh();
    }
}

void MainWindow::onNavigateButtonClicked() {
    // Get address bar text
    WCHAR buffer[MAX_PATH];
    GetWindowTextW(m_addressBar, buffer, MAX_PATH);
    
    // Navigate to the path
    try {
        fs::path path(buffer);
        m_fileExplorer->navigateTo(path);
    }
    catch (const std::exception& e) {
        MessageBoxA(m_hwnd, e.what(), "Error", MB_ICONERROR | MB_OK);
    }
}

void MainWindow::onRefreshButtonClicked() {
    m_fileExplorer->refresh();
}

void MainWindow::onFilterButtonClicked() {
    // TODO: Implement filter dialog
}

void MainWindow::onSortButtonClicked() {
    // TODO: Implement sort menu
}

void MainWindow::onViewButtonClicked() {
    // TODO: Implement view menu
    if (m_fileListView) {
        // Toggle view mode
        FileListView::ViewMode currentMode = m_fileListView->getViewMode();
        FileListView::ViewMode newMode;
        
        switch (currentMode) {
            case FileListView::ViewMode::Details:
                newMode = FileListView::ViewMode::List;
                break;
            case FileListView::ViewMode::List:
                newMode = FileListView::ViewMode::Icons;
                break;
            case FileListView::ViewMode::Icons:
                newMode = FileListView::ViewMode::Tiles;
                break;
            case FileListView::ViewMode::Tiles:
                newMode = FileListView::ViewMode::Details;
                break;
            default:
                newMode = FileListView::ViewMode::Details;
                break;
        }
        
        m_fileListView->setViewMode(newMode);
    }
}

void MainWindow::updateTitle() {
    // Set window title to current path
    std::wstring title = L"Fast File Explorer - " + m_fileExplorer->getCurrentPath().wstring();
    SetWindowTextW(m_hwnd, title.c_str());
}

void MainWindow::updateAddressBar() {
    // Update address bar with current path
    SetWindowTextW(m_addressBar, m_fileExplorer->getCurrentPath().wstring().c_str());
}

void MainWindow::updateStatusBar() {
    // Update status bar with item count and selected info
    if (m_statusBar && m_fileListView) {
        std::vector<FileExplorer::FileItem> selectedItems = m_fileListView->getSelectedItems();
        int selectedCount = static_cast<int>(selectedItems.size());
        
        std::wstring statusText;
        if (selectedCount == 0) {
            // Show item count
            statusText = std::format(L"{} items", m_fileListView->getSelectedItems().size());
        }
        else if (selectedCount == 1) {
            // Show selected item info
            const auto& item = selectedItems[0];
            statusText = std::format(L"1 item selected - {} ({})", 
                item.path.filename().wstring(),
                FileSystemUtils::formatFileSize(item.size));
        }
        else {
            // Show selected count
            statusText = std::format(L"{} items selected", selectedCount);
        }
        
        SendMessageW(m_statusBar, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(statusText.c_str()));
    }
}

void MainWindow::onFilesLoaded(const std::vector<FileExplorer::FileItem>& files) {
    // Update the file list view
    if (m_fileListView) {
        m_fileListView->loadFiles(files);
    }
    
    // Update UI
    updateTitle();
    updateAddressBar();
    updateStatusBar();
    
    // Reset loading flag
    m_isLoading = false;
}

void MainWindow::onError(const std::string& errorMessage) {
    // Show error message
    MessageBoxA(m_hwnd, errorMessage.c_str(), "Error", MB_ICONERROR | MB_OK);
    
    // Reset loading flag
    m_isLoading = false;
}

void MainWindow::createToolbar() {
    // TODO: Implement toolbar
}

void MainWindow::createAddressBar() {
    // Create address bar edit control
    m_addressBar = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0,  // Will be positioned in onSize
        m_hwnd,
        reinterpret_cast<HMENU>(ID_ADDRESS_BAR),
        GetModuleHandle(NULL),
        NULL
    );
    
    // Set default font
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessage(m_addressBar, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), MAKELPARAM(TRUE, 0));
}

void MainWindow::createSearchBox() {
    // Create search box edit control
    m_searchBox = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0,  // Will be positioned in onSize
        m_hwnd,
        reinterpret_cast<HMENU>(ID_SEARCH_BOX),
        GetModuleHandle(NULL),
        NULL
    );
    
    // Set placeholder text using modern API
    SendMessageW(m_searchBox, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search..."));
    
    // Set default font
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessage(m_searchBox, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), MAKELPARAM(TRUE, 0));
}

void MainWindow::createFileListView() {
    // Create file list view
    m_fileListView = std::make_unique<FileListView>(m_hwnd);
    m_fileListView->create();
    
    // Set callbacks
    m_fileListView->setItemActivatedCallback([this](const FileExplorer::FileItem& item) {
        if (fs::is_directory(item.path)) {
            m_fileExplorer->navigateTo(item.path);
        }
        else {
            ShellExecuteW(m_hwnd, L"open", item.path.wstring().c_str(), NULL, NULL, SW_SHOW);
        }
    });
    
    m_fileListView->setSelectionChangedCallback([this](const std::vector<FileExplorer::FileItem>& items) {
        updateStatusBar();
    });
    
    // Set initial view mode
    m_fileListView->setViewMode(FileListView::ViewMode::Details);
}

void MainWindow::createStatusBar() {
    // Create status bar
    m_statusBar = CreateWindowExW(
        0,
        STATUSCLASSNAMEW,
        L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0,  // Will be positioned by Windows
        m_hwnd,
        reinterpret_cast<HMENU>(ID_STATUS_BAR),
        GetModuleHandle(NULL),
        NULL
    );
    
    // Set default font
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessage(m_statusBar, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), MAKELPARAM(TRUE, 0));
}

void MainWindow::createSideBar() {
    // Create side bar (tree view)
    m_sideBar = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_TREEVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        0, 0, 0, 0,  // Will be positioned in onSize
        m_hwnd,
        reinterpret_cast<HMENU>(ID_SIDE_BAR),
        GetModuleHandle(NULL),
        NULL
    );
    
    // Set default font
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SendMessage(m_sideBar, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), MAKELPARAM(TRUE, 0));
    
    // Populate tree view with drives and special folders
    HTREEITEM hRoot = TreeView_InsertItem(m_sideBar, &TVINSERTSTRUCTW{
        .hParent = TVI_ROOT,
        .hInsertAfter = TVI_LAST,
        .item = {
            .mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE,
            .pszText = const_cast<LPWSTR>(L"This PC"),
            .iImage = 0,
            .iSelectedImage = 0,
            .lParam = 0
        }
    });
    
    // Add drives
    for (const auto& drive : FileSystemUtils::getLogicalDrives()) {
        std::wstring driveName = drive.wstring();
        
        TVINSERTSTRUCTW tvis = {};
        tvis.hParent = hRoot;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
        tvis.item.pszText = const_cast<LPWSTR>(driveName.c_str());
        tvis.item.lParam = reinterpret_cast<LPARAM>(new fs::path(drive));
        
        TreeView_InsertItem(m_sideBar, &tvis);
    }
    
    // Add quick access locations
    for (const auto& location : m_fileExplorer->getQuickAccessLocations()) {
        std::wstring locationName = location.filename().wstring();
        
        TVINSERTSTRUCTW tvis = {};
        tvis.hParent = TVI_ROOT;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_PARAM;
        tvis.item.pszText = const_cast<LPWSTR>(locationName.c_str());
        tvis.item.lParam = reinterpret_cast<LPARAM>(new fs::path(location));
        
        TreeView_InsertItem(m_sideBar, &tvis);
    }
    
    // Expand root
    TreeView_Expand(m_sideBar, hRoot, TVE_EXPAND);
}