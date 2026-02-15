# Windows Frontend UI Rework Plan

## Overview

This document details the implementation plan for reworking the Windows frontend GUI to match the Qt frontend's tabbed interface with a right-side menu button.

## Current Architecture Comparison

### Qt Frontend Structure

The Qt frontend uses a **scaffolding architecture** where a main window container holds multiple browser tabs:

```
NS_Scaffold (QTabWidget)
├── Tab Bar (with close buttons)
├── "+" Button (corner widget - TopRightCorner)
├── NS_Window (Tab 1)
│   ├── NS_URLBar (QToolBar)
│   │   ├── Back Button
│   │   ├── Local History Button
│   │   ├── Forward Button
│   │   ├── Stop/Reload Button
│   │   ├── URL Input (QLineEdit)
│   │   │   ├── Page Info Icon (LeadingPosition)
│   │   │   └── Bookmark Icon (TrailingPosition)
│   │   ├── Zoom Reset Button
│   │   └── Burger Menu Button ("⋮")
│   ├── NS_Widget (Browser Canvas)
│   └── Scrollbars + Status
├── NS_Window (Tab 2)
└── NS_Window (Tab N)
```

Key Qt source files:
- [`frontends/qt/scaffolding.cls.h`](../frontends/qt/scaffolding.cls.h) - Tab container class
- [`frontends/qt/scaffolding.cpp`](../frontends/qt/scaffolding.cpp) - Tab container implementation
- [`frontends/qt/window.cls.h`](../frontends/qt/window.cls.h) - Individual browser window
- [`frontends/qt/urlbar.cls.h`](../frontends/qt/urlbar.cls.h) - URL bar with burger menu
- [`frontends/qt/actions.cls.h`](../frontends/qt/actions.cls.h) - Centralized action management

### Windows Frontend Current Structure

The Windows frontend currently creates a separate window for each browser context:

```
gui_window (separate window per browser context)
├── Main Window (HWND main)
│   ├── Menu Bar (HMENU mainmenu) - traditional top menu
│   ├── Toolbar (HWND toolbar)
│   │   ├── Back Button
│   │   ├── Forward Button
│   │   ├── Home Button
│   │   ├── Reload Button
│   │   ├── Stop Button
│   │   ├── URL Bar (Edit control)
│   │   └── Throbber (Animation control)
│   ├── Drawing Area (HWND drawingarea)
│   ├── Vertical Scrollbar
│   ├── Horizontal Scrollbar
│   └── Status Bar
└── Local History Window
```

Key Windows source files:
- [`frontends/windows/window.h`](../frontends/windows/window.h) - Window structure definition
- [`frontends/windows/window.c`](../frontends/windows/window.c) - Window creation and management
- [`frontends/windows/drawable.c`](../frontends/windows/drawable.c) - Browser canvas
- [`frontends/windows/resourceid.h`](../frontends/windows/resourceid.h) - Resource IDs

---

## Target Architecture

### New Windows Frontend Structure

```
gui_scaffold (main container window)
├── Tab Control (WC_TABCONTROL)
│   ├── Tab Item 1 (with close button)
│   ├── Tab Item 2
│   └── Tab Item N
├── "+" Button (child window - top right)
├── Toolbar (shared across all tabs)
│   ├── Back Button
│   ├── Forward Button
│   ├── Stop/Reload Button
│   ├── URL Bar (Edit control)
│   │   ├── Page Info Button
│   │   └── Bookmark Button
│   ├── Zoom Reset Button
│   └── Menu Button ("⋮") - right side
├── Drawing Area Container
│   ├── gui_window Tab 1 (drawing area)
│   ├── gui_window Tab 2 (hidden)
│   └── gui_window Tab N (hidden)
├── Vertical Scrollbar
├── Horizontal Scrollbar
└── Status Bar
```

---

## Implementation Details

### 1. New Data Structures

#### 1.1 Scaffold Structure

Create new file: `frontends/windows/scaffold.h`

```c
#ifndef WISP_WINDOWS_SCAFFOLD_H
#define WISP_WINDOWS_SCAFFOLD_H

#include <windows.h>
#include <commctrl.h>

struct gui_window; // forward declaration

/**
 * Scaffold structure - main container for tabbed browser windows
 */
struct gui_scaffold {
    /** Main window handle */
    HWND main;
    
    /** Tab control handle */
    HWND tab_control;
    
    /** Toolbar handle (shared across all tabs) */
    HWND toolbar;
    
    /** URL bar handle */
    HWND urlbar;
    
    /** Throbber animation handle */
    HWND throbber;
    
    /** Status bar handle */
    HWND statusbar;
    
    /** Vertical scrollbar handle */
    HWND vscroll;
    
    /** Horizontal scrollbar handle */
    HWND hscroll;
    
    /** Menu button handle (burger menu) */
    HWND menu_button;
    
    /** New tab button handle */
    HWND newtab_button;
    
    /** Popup menu handle (replaces menu bar) */
    HMENU popup_menu;
    
    /** Context menu for right-click */
    HMENU context_menu;
    
    /** Array of tab gui_window pointers */
    struct gui_window **tabs;
    
    /** Number of tabs */
    int tab_count;
    
    /** Currently active tab index */
    int active_tab;
    
    /** Window width */
    int width;
    
    /** Window height */
    int height;
    
    /** Accelerator table */
    HACCEL accel_table;
    
    /** Linked list pointer for scaffold list */
    struct gui_scaffold *next;
    struct gui_scaffold *prev;
};

/**
 * Get scaffold from window handle
 */
struct gui_scaffold *nsws_get_scaffold(HWND hwnd);

/**
 * Create a new scaffold window
 */
struct gui_scaffold *nsws_scaffold_create(HINSTANCE hInstance);

/**
 * Destroy a scaffold window
 */
void nsws_scaffold_destroy(struct gui_scaffold *scaffold);

/**
 * Add a tab to the scaffold
 */
nserror nsws_scaffold_add_tab(struct gui_scaffold *scaffold, struct gui_window *gw);

/**
 * Remove a tab from the scaffold
 */
nserror nsws_scaffold_remove_tab(struct gui_scaffold *scaffold, int index);

/**
 * Switch to a different tab
 */
void nsws_scaffold_switch_tab(struct gui_scaffold *scaffold, int index);

/**
 * Get current scaffold for new window/tab creation
 */
struct gui_scaffold *nsws_scaffold_get_current(void);

/**
 * Create main window class for scaffold
 */
nserror nsws_create_scaffold_class(HINSTANCE hinstance);

#endif
```

#### 1.2 Modified gui_window Structure

Modify `frontends/windows/window.h`:

```c
struct gui_window {
    /* Browser window this gui_window represents */
    struct browser_window *bw;
    
    /** The scaffold this window belongs to */
    struct gui_scaffold *scaffold;
    
    /** Tab index in scaffold */
    int tab_index;
    
    /** Drawing area handle */
    HWND drawingarea;
    
    /** Local history window */
    struct nsws_localhistory *localhistory;
    
    /** Width of drawing area */
    int width;
    
    /** Height of drawing area */
    int height;
    
    /** Current scroll position X */
    int scrollx;
    
    /** Current scroll position Y */
    int scrolly;
    
    /** Requested scroll X */
    int requestscrollx;
    
    /** Requested scroll Y */
    int requestscrolly;
    
    /** Whether currently throbbing */
    bool throbbing;
    
    /** Mouse state */
    struct browser_mouse *mouse;
    
    /** Page info button handle */
    HWND pageinfo_button;
    
    /** Page info bitmap handles */
    HBITMAP hPageInfo[8];
    
    /** Fullscreen rectangle memory */
    RECT *fullscreen;
    
    /** Redraw area */
    RECT redraw;
    
    /** Has gradients flag */
    bool has_gradients;
    
    /** Linked list */
    struct gui_window *next;
    struct gui_window *prev;
};
```

### 2. New Files to Create

| File | Purpose |
|------|---------|
| `scaffold.h` | Scaffold structure definition |
| `scaffold.c` | Scaffold implementation (tab container) |
| `actions.h` | Action IDs and command handling |
| `actions.c` | Action implementation (centralized commands) |

### 3. Files to Modify

| File | Changes |
|------|---------|
| `window.h` | Remove main window handles, add scaffold reference |
| `window.c` | Remove window creation, add tab content creation |
| `drawable.c` | Update to work with scaffold scrollbars |
| `resourceid.h` | Add new control IDs |
| `res/resource.rc` | Remove menu bar, add popup menu definition |
| `main.c` | Initialize scaffold class |
| `gui.h` | Add scaffold-related declarations |

### 4. Resource ID Additions

Add to `frontends/windows/resourceid.h`:

```c
/* Scaffold controls */
#define IDC_SCAFFOLD_TABCONTROL  2000
#define IDC_SCAFFOLD_NEWTAB      2001
#define IDC_SCAFFOLD_MENU        2002
#define IDC_SCAFFOLD_PAGEINFO    2003
#define IDC_SCAFFOLD_BOOKMARK    2004
#define IDC_SCAFFOLD_ZOOMRESET   2005

/* Popup Menu */
#define IDR_MENU_POPUP  12000
```

### 5. Implementation Steps

#### Step 1: Create Scaffold Class

Create `frontends/windows/scaffold.c`:

```c
#include <windows.h>
#include <commctrl.h>

#include "wisp/browser_window.h"
#include "wisp/utils/log.h"

#include "windows/scaffold.h"
#include "windows/window.h"
#include "windows/gui.h"
#include "windows/resourceid.h"

/** Window class name for scaffold */
static const LPCWSTR windowclassname_scaffold = L"nswsscaffold";

/** Global list of scaffold windows */
static struct gui_scaffold *scaffold_list = NULL;

/** Current active scaffold */
static struct gui_scaffold *current_scaffold = NULL;

/** Forward declarations */
static LRESULT CALLBACK nsws_scaffold_callback(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
static HWND nsws_scaffold_create_toolbar(HINSTANCE hInstance, HWND hWndParent, struct gui_scaffold *scaffold);
static HWND nsws_scaffold_create_tabcontrol(HINSTANCE hInstance, HWND hWndParent, struct gui_scaffold *scaffold);

/**
 * Register the scaffold window class
 */
nserror nsws_create_scaffold_class(HINSTANCE hinstance)
{
    WNDCLASSEXW wc;
    
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = 0;
    wc.lpfnWndProc = nsws_scaffold_callback;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(struct gui_scaffold *);
    wc.hInstance = hinstance;
    wc.hIcon = LoadIcon(hinstance, MAKEINTRESOURCE(IDR_WISP_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;  // No menu bar
    wc.lpszClassName = windowclassname_scaffold;
    wc.hIconSm = LoadIcon(hinstance, MAKEINTRESOURCE(IDR_WISP_ICON));
    
    if (RegisterClassExW(&wc) == 0) {
        return NSERROR_INIT_FAILED;
    }
    
    return NSERROR_OK;
}

/**
 * Create a new scaffold window
 */
struct gui_scaffold *nsws_scaffold_create(HINSTANCE hInstance)
{
    struct gui_scaffold *scaffold;
    int xpos = CW_USEDEFAULT;
    int ypos = CW_USEDEFAULT;
    int width = CW_USEDEFAULT;
    int height = CW_USEDEFAULT;
    
    scaffold = calloc(1, sizeof(struct gui_scaffold));
    if (scaffold == NULL) {
        return NULL;
    }
    
    // Use saved window position if available
    if ((nsoption_int(window_width) >= 100) && (nsoption_int(window_height) >= 100)) {
        xpos = nsoption_int(window_x);
        ypos = nsoption_int(window_y);
        width = nsoption_int(window_width);
        height = nsoption_int(window_height);
    }
    
    // Create main window without menu bar
    scaffold->main = CreateWindowExW(
        0,
        windowclassname_scaffold,
        L"Wisp Browser",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        xpos, ypos, width, height,
        NULL, NULL, hInstance, scaffold
    );
    
    if (scaffold->main == NULL) {
        free(scaffold);
        return NULL;
    }
    
    // Create tab control
    scaffold->tab_control = nsws_scaffold_create_tabcontrol(hInstance, scaffold->main, scaffold);
    
    // Create toolbar
    scaffold->toolbar = nsws_scaffold_create_toolbar(hInstance, scaffold->main, scaffold);
    
    // Create status bar
    scaffold->statusbar = CreateWindowEx(
        0, STATUSCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0,
        scaffold->main, (HMENU)IDC_MAIN_STATUSBAR, hInstance, NULL
    );
    
    // Create scrollbars
    scaffold->vscroll = CreateWindowExW(
        0, L"SCROLLBAR", NULL,
        WS_CHILD | SBS_VERT,
        0, 0, 0, 0,
        scaffold->main, NULL, hInstance, NULL
    );
    
    scaffold->hscroll = CreateWindowExW(
        0, L"SCROLLBAR", NULL,
        WS_CHILD | SBS_HORZ,
        0, 0, 0, 0,
        scaffold->main, NULL, hInstance, NULL
    );
    
    // Initialize tab array
    scaffold->tabs = NULL;
    scaffold->tab_count = 0;
    scaffold->active_tab = -1;
    
    // Add to global list
    scaffold->next = scaffold_list;
    scaffold->prev = NULL;
    if (scaffold_list != NULL) {
        scaffold_list->prev = scaffold;
    }
    scaffold_list = scaffold;
    current_scaffold = scaffold;
    
    // Show window
    ShowWindow(scaffold->main, SW_SHOW);
    
    return scaffold;
}

/**
 * Create tab control
 */
static HWND nsws_scaffold_create_tabcontrol(HINSTANCE hInstance, HWND hWndParent, struct gui_scaffold *scaffold)
{
    HWND hTab;
    RECT rc;
    
    GetClientRect(hWndParent, &rc);
    
    hTab = CreateWindowExW(
        0, WC_TABCONTROLW, NULL,
        WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE |
        TCS_TABS | TCS_SINGLELINE | TCS_RIGHTJUSTIFY,
        0, 0, rc.right - rc.left, 30,
        hWndParent, (HMENU)IDC_SCAFFOLD_TABCONTROL, hInstance, NULL
    );
    
    if (hTab) {
        // Set font
        SendMessage(hTab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
    }
    
    return hTab;
}

/**
 * Add a tab to the scaffold
 */
nserror nsws_scaffold_add_tab(struct gui_scaffold *scaffold, struct gui_window *gw)
{
    TCITEMW tie;
    wchar_t title[] = L"Loading...";
    
    // Expand tabs array
    struct gui_window **new_tabs = realloc(scaffold->tabs, (scaffold->tab_count + 1) * sizeof(struct gui_window *));
    if (new_tabs == NULL) {
        return NSERROR_NOMEM;
    }
    scaffold->tabs = new_tabs;
    
    // Add tab item
    tie.mask = TCIF_TEXT | TCIF_IMAGE;
    tie.iImage = -1;
    tie.pszText = title;
    
    TabCtrl_InsertItem(scaffold->tab_control, scaffold->tab_count, &tie);
    
    // Store gui_window pointer
    scaffold->tabs[scaffold->tab_count] = gw;
    gw->scaffold = scaffold;
    gw->tab_index = scaffold->tab_count;
    
    scaffold->tab_count++;
    
    // Switch to new tab
    nsws_scaffold_switch_tab(scaffold, scaffold->tab_count - 1);
    
    return NSERROR_OK;
}

/**
 * Switch to a different tab
 */
void nsws_scaffold_switch_tab(struct gui_scaffold *scaffold, int index)
{
    if (index < 0 || index >= scaffold->tab_count) {
        return;
    }
    
    // Hide previous tab's drawing area
    if (scaffold->active_tab >= 0 && scaffold->active_tab < scaffold->tab_count) {
        if (scaffold->tabs[scaffold->active_tab]->drawingarea) {
            ShowWindow(scaffold->tabs[scaffold->active_tab]->drawingarea, SW_HIDE);
        }
    }
    
    // Show new tab's drawing area
    if (scaffold->tabs[index]->drawingarea) {
        ShowWindow(scaffold->tabs[index]->drawingarea, SW_SHOW);
    }
    
    scaffold->active_tab = index;
    TabCtrl_SetCurSel(scaffold->tab_control, index);
    
    // Update toolbar state
    // TODO: Update back/forward buttons based on current tab
    
    // Update window title
    // TODO: Get title from browser window
}

/**
 * Scaffold window procedure
 */
static LRESULT CALLBACK nsws_scaffold_callback(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct gui_scaffold *scaffold;
    
    scaffold = (struct gui_scaffold *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    switch (msg) {
    case WM_CREATE:
        // Store scaffold pointer in window data
        scaffold = (struct gui_scaffold *)((CREATESTRUCT *)lparam)->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)scaffold);
        break;
        
    case WM_SIZE:
        // Resize controls
        if (scaffold != NULL) {
            nsws_scaffold_layout(scaffold);
        }
        return 0;
        
    case WM_NOTIFY:
        {
            LPNMHDR pnmh = (LPNMHDR)lparam;
            if (pnmh->hwndFrom == scaffold->tab_control) {
                switch (pnmh->code) {
                case TCN_SELCHANGE:
                    {
                        int index = TabCtrl_GetCurSel(scaffold->tab_control);
                        nsws_scaffold_switch_tab(scaffold, index);
                    }
                    break;
                }
            }
        }
        break;
        
    case WM_COMMAND:
        // Handle toolbar and menu commands
        return nsws_scaffold_command(scaffold, LOWORD(wparam), HIWORD(wparam));
        
    case WM_CLOSE:
        // Close all tabs
        for (int i = scaffold->tab_count - 1; i >= 0; i--) {
            browser_window_destroy(scaffold->tabs[i]->bw);
        }
        return 0;
        
    case WM_DESTROY:
        // Remove from global list
        if (scaffold->prev) {
            scaffold->prev->next = scaffold->next;
        } else {
            scaffold_list = scaffold->next;
        }
        if (scaffold->next) {
            scaffold->next->prev = scaffold->prev;
        }
        if (current_scaffold == scaffold) {
            current_scaffold = scaffold_list;
        }
        free(scaffold->tabs);
        free(scaffold);
        
        // Check if this was the last window
        if (scaffold_list == NULL) {
            win32_set_quit(true);
        }
        break;
    }
    
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
```

#### Step 2: Create Actions Module

Create `frontends/windows/actions.h`:

```c
#ifndef WISP_WINDOWS_ACTIONS_H
#define WISP_WINDOWS_ACTIONS_H

#include <windows.h>

struct gui_scaffold;
struct gui_window;

/**
 * Handle a command from toolbar or menu
 */
LRESULT nsws_scaffold_command(struct gui_scaffold *scaffold, int identifier, int notification);

/**
 * Update navigation buttons state
 */
void nsws_update_navigation(struct gui_scaffold *scaffold, bool can_back, bool can_forward);

/**
 * Update stop/reload button state
 */
void nsws_update_stop_reload(struct gui_scaffold *scaffold, bool loading);

/**
 * Create popup menu
 */
HMENU nsws_create_popup_menu(struct gui_scaffold *scaffold);

#endif
```

Create `frontends/windows/actions.c`:

```c
#include <windows.h>

#include "wisp/browser_window.h"
#include "wisp/utils/messages.h"

#include "windows/actions.h"
#include "windows/scaffold.h"
#include "windows/window.h"
#include "windows/resourceid.h"
#include "windows/gui.h"

/**
 * Handle command messages
 */
LRESULT nsws_scaffold_command(struct gui_scaffold *scaffold, int identifier, int notification)
{
    struct gui_window *gw;
    
    if (scaffold == NULL || scaffold->active_tab < 0) {
        return 1;
    }
    
    gw = scaffold->tabs[scaffold->active_tab];
    
    switch (identifier) {
    case IDC_MAIN_URLBAR:
        // URL bar handling
        if (notification == EN_CHANGE) {
            // Text changed
        }
        break;
        
    case IDC_MAIN_LAUNCH_URL:
        // Navigate to URL
        nsws_window_go(scaffold->main, NULL);  // Get URL from urlbar
        break;
        
    case IDM_NAV_BACK:
        if (gw->bw && browser_window_back_available(gw->bw)) {
            browser_window_history_back(gw->bw, false);
        }
        break;
        
    case IDM_NAV_FORWARD:
        if (gw->bw && browser_window_forward_available(gw->bw)) {
            browser_window_history_forward(gw->bw, false);
        }
        break;
        
    case IDM_NAV_HOME:
        // Navigate to homepage
        break;
        
    case IDM_NAV_RELOAD:
        if (gw->bw) {
            browser_window_reload(gw->bw, true);
        }
        break;
        
    case IDM_NAV_STOP:
        if (gw->bw) {
            browser_window_stop(gw->bw);
        }
        break;
        
    case IDC_SCAFFOLD_NEWTAB:
    case IDM_FILE_OPEN_WINDOW:
        // Create new tab
        NS_Application::create_browser_widget(NULL, true);  // Qt style
        break;
        
    case IDM_FILE_CLOSE_WINDOW:
        // Close current tab
        if (scaffold->tab_count > 1) {
            browser_window_destroy(gw->bw);
        }
        break;
        
    case IDM_FILE_QUIT:
        win32_set_quit(true);
        break;
        
    case IDC_SCAFFOLD_MENU:
        // Show popup menu
        {
            RECT rc;
            GetWindowRect(scaffold->menu_button, &rc);
            TrackPopupMenu(scaffold->popup_menu, TPM_LEFTALIGN | TPM_TOPALIGN,
                          rc.left, rc.bottom, 0, scaffold->main, NULL);
        }
        break;
        
    default:
        return 1;  // Unhandled
    }
    
    return 0;
}

/**
 * Update navigation button states
 */
void nsws_update_navigation(struct gui_scaffold *scaffold, bool can_back, bool can_forward)
{
    if (scaffold->toolbar) {
        SendMessage(scaffold->toolbar, TB_SETSTATE, IDM_NAV_BACK,
                   MAKELONG(can_back ? TBSTATE_ENABLED : TBSTATE_INDETERMINATE, 0));
        SendMessage(scaffold->toolbar, TB_SETSTATE, IDM_NAV_FORWARD,
                   MAKELONG(can_forward ? TBSTATE_ENABLED : TBSTATE_INDETERMINATE, 0));
    }
}

/**
 * Create popup menu (burger menu)
 */
HMENU nsws_create_popup_menu(struct gui_scaffold *scaffold)
{
    HMENU hMenu = CreatePopupMenu();
    
    // New Tab / New Window
    AppendMenu(hMenu, MF_STRING, IDM_FILE_OPEN_WINDOW, messages_get("NewTab"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    
    // Bookmarks submenu
    HMENU hBookmarks = CreatePopupMenu();
    AppendMenu(hBookmarks, MF_STRING, IDM_NAV_BOOKMARKS, messages_get("AddBookmark"));
    AppendMenu(hBookmarks, MF_STRING, IDM_NAV_BOOKMARKS, messages_get("ShowBookmarks"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hBookmarks, messages_get("Bookmarks"));
    
    // History
    AppendMenu(hMenu, MF_STRING, IDM_NAV_GLOBALHISTORY, messages_get("GlobalHistory"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    
    // Zoom submenu
    HMENU hZoom = CreatePopupMenu();
    AppendMenu(hZoom, MF_STRING, IDM_VIEW_ZOOMPLUS, messages_get("ZoomIn"));
    AppendMenu(hZoom, MF_STRING, IDM_VIEW_ZOOMMINUS, messages_get("ZoomOut"));
    AppendMenu(hZoom, MF_STRING, IDM_VIEW_ZOOMNORMAL, messages_get("ZoomNormal"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hZoom, messages_get("Zoom"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    
    // Settings
    AppendMenu(hMenu, MF_STRING, IDM_EDIT_PREFERENCES, messages_get("Settings"));
    
    // More Tools submenu
    HMENU hTools = CreatePopupMenu();
    AppendMenu(hTools, MF_STRING, IDM_TOOLS_COOKIES, messages_get("Cookies"));
    AppendMenu(hTools, MF_STRING, IDM_VIEW_SOURCE, messages_get("ViewSource"));
    AppendMenu(hTools, MF_STRING, IDM_VIEW_TOGGLE_DEBUG_RENDERING, messages_get("DebugRender"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hTools, messages_get("MoreTools"));
    
    // Help submenu
    HMENU hHelp = CreatePopupMenu();
    AppendMenu(hHelp, MF_STRING, IDM_HELP_ABOUT, messages_get("About"));
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hHelp, messages_get("Help"));
    
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    
    // Quit
    AppendMenu(hMenu, MF_STRING, IDM_FILE_QUIT, messages_get("Quit"));
    
    return hMenu;
}
```

#### Step 3: Modify Window Creation

Modify `frontends/windows/window.c`:

```c
/**
 * Create a new browser window (tab content)
 */
struct gui_window *gui_window_create(struct browser_window *bw, struct gui_window *existing, gui_window_create_flags flags)
{
    struct gui_window *gw;
    struct gui_scaffold *scaffold;
    bool intab = (flags & GW_CREATE_TAB) != 0;
    
    gw = calloc(1, sizeof(struct gui_window));
    if (gw == NULL) {
        return NULL;
    }
    
    gw->bw = bw;
    gw->mouse = calloc(1, sizeof(struct browser_mouse));
    if (gw->mouse == NULL) {
        free(gw);
        return NULL;
    }
    gw->mouse->gui = gw;
    
    // Get or create scaffold
    if (existing != NULL && existing->scaffold != NULL && intab) {
        scaffold = existing->scaffold;
    } else {
        scaffold = nsws_scaffold_get_current();
        if (scaffold == NULL || !intab) {
            scaffold = nsws_scaffold_create(hinst);
            if (scaffold == NULL) {
                free(gw->mouse);
                free(gw);
                return NULL;
            }
        }
    }
    
    // Create drawing area as child of scaffold
    gw->drawingarea = nsws_window_create_drawable(hinst, scaffold->main, gw);
    gw->scaffold = scaffold;
    
    // Add to scaffold's tab list
    nsws_scaffold_add_tab(scaffold, gw);
    
    // Add to global window list
    gw->next = window_list;
    gw->prev = NULL;
    if (window_list != NULL) {
        window_list->prev = gw;
    }
    window_list = gw;
    
    return gw;
}

/**
 * Destroy a browser window (tab)
 */
void gui_window_destroy(struct gui_window *gw)
{
    struct gui_scaffold *scaffold;
    int index;
    
    if (gw == NULL) {
        return;
    }
    
    scaffold = gw->scaffold;
    index = gw->tab_index;
    
    // Remove from scaffold
    nsws_scaffold_remove_tab(scaffold, index);
    
    // Destroy drawing area
    if (gw->drawingarea != NULL) {
        DestroyWindow(gw->drawingarea);
    }
    
    // Free local history
    if (gw->localhistory != NULL) {
        nsws_local_history_destroy(gw->localhistory);
    }
    
    // Remove from global list
    if (gw->prev != NULL) {
        gw->prev->next = gw->next;
    } else {
        window_list = gw->next;
    }
    if (gw->next != NULL) {
        gw->next->prev = gw->prev;
    }
    
    free(gw->mouse);
    free(gw);
}
```

#### Step 4: Toolbar with Burger Menu

Create toolbar with menu button on the right:

```c
/**
 * Create toolbar with menu button
 */
static HWND nsws_scaffold_create_toolbar(HINSTANCE hInstance, HWND hWndParent, struct gui_scaffold *scaffold)
{
    HIMAGELIST hImageList;
    HWND hWndToolbar;
    TBBUTTON tbButtons[] = {
        {0, IDM_NAV_BACK, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0},
        {1, IDM_NAV_FORWARD, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0},
        {2, IDM_NAV_RELOAD, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0, 0},  // Stop/Reload combined
        // URL bar will be inserted here
        // Menu button will be at the end
    };
    
    hWndToolbar = CreateWindowEx(
        0, TOOLBARCLASSNAME, "Toolbar",
        WS_CHILD | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_LIST,
        0, 0, 0, 0,
        hWndParent, NULL, HINST_COMMCTRL, NULL
    );
    
    if (!hWndToolbar) {
        return NULL;
    }
    
    // Set button size
    int buttonsize = 24;
    SendMessage(hWndToolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(buttonsize, buttonsize));
    
    // Set image lists
    hImageList = get_imagelist(hInstance, IDR_TOOLBAR_BITMAP, buttonsize, 5);
    if (hImageList != NULL) {
        SendMessage(hWndToolbar, TB_SETIMAGELIST, 0, (LPARAM)hImageList);
    }
    
    // Add buttons
    SendMessage(hWndToolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessage(hWndToolbar, TB_ADDBUTTONS, sizeof(tbButtons)/sizeof(TBBUTTON), (LPARAM)&tbButtons);
    
    // Create URL bar as embedded control
    scaffold->urlbar = nsws_create_urlbar(hInstance, hWndToolbar, scaffold);
    
    // Insert URL bar into toolbar
    TBBUTTON tbUrl = {0};
    tbUrl.idCommand = IDC_MAIN_URLBAR;
    tbUrl.fsStyle = BTNS_SEP;
    tbUrl.iBitmap = 200;  // Width in pixels
    SendMessage(hWndToolbar, TB_INSERTBUTTON, 3, (LPARAM)&tbUrl);
    
    // Create menu button (burger menu)
    scaffold->menu_button = CreateWindowExW(
        0, L"BUTTON", L"⋮",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 30, 24,
        hWndToolbar, (HMENU)IDC_SCAFFOLD_MENU, hInstance, NULL
    );
    
    // Create popup menu
    scaffold->popup_menu = nsws_create_popup_menu(scaffold);
    
    // Create new tab button
    scaffold->newtab_button = CreateWindowExW(
        0, L"BUTTON", L"+",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 24, 24,
        hWndParent, (HMENU)IDC_SCAFFOLD_NEWTAB, hInstance, NULL
    );
    
    SendMessage(hWndToolbar, TB_AUTOSIZE, 0, 0);
    ShowWindow(hWndToolbar, TRUE);
    
    return hWndToolbar;
}
```

#### Step 5: Layout Function

```c
/**
 * Layout scaffold controls
 */
void nsws_scaffold_layout(struct gui_scaffold *scaffold)
{
    RECT rc, rcTool, rcStatus, rcTab;
    int toolbarHeight = 0;
    int statusbarHeight = 0;
    int tabHeight = 0;
    
    GetClientRect(scaffold->main, &rc);
    
    // Position tab control
    if (scaffold->tab_control) {
        // Reserve space for new tab button
        RECT rcTab;
        SendMessage(scaffold->tab_control, TCM_GETITEMRECT, 0, (LPARAM)&rcTab);
        tabHeight = rcTab.bottom - rcTab.top;
        
        MoveWindow(scaffold->tab_control, 0, 0, rc.right - 30, tabHeight, TRUE);
        
        // Position new tab button
        if (scaffold->newtab_button) {
            MoveWindow(scaffold->newtab_button, rc.right - 28, 2, 24, tabHeight - 4, TRUE);
        }
    }
    
    // Position toolbar
    if (scaffold->toolbar) {
        SendMessage(scaffold->toolbar, TB_AUTOSIZE, 0, 0);
        GetWindowRect(scaffold->toolbar, &rcTool);
        toolbarHeight = rcTool.bottom - rcTool.top;
        MoveWindow(scaffold->toolbar, 0, tabHeight, rc.right, toolbarHeight, TRUE);
    }
    
    // Position status bar
    if (scaffold->statusbar) {
        SendMessage(scaffold->statusbar, WM_SIZE, 0, 0);
        GetWindowRect(scaffold->statusbar, &rcStatus);
        statusbarHeight = rcStatus.bottom - rcStatus.top;
    }
    
    // Calculate drawing area
    int drawingTop = tabHeight + toolbarHeight;
    int drawingHeight = rc.bottom - drawingTop - statusbarHeight;
    int drawingWidth = rc.right;
    
    // Position scrollbars and drawing area for active tab
    if (scaffold->active_tab >= 0 && scaffold->tabs[scaffold->active_tab] != NULL) {
        struct gui_window *gw = scaffold->tabs[scaffold->active_tab];
        
        // Position drawing area
        MoveWindow(gw->drawingarea, 0, drawingTop, drawingWidth - 20, drawingHeight - 20, TRUE);
        
        // Position vertical scrollbar
        MoveWindow(scaffold->vscroll, drawingWidth - 20, drawingTop, 20, drawingHeight - 20, TRUE);
        
        // Position horizontal scrollbar
        MoveWindow(scaffold->hscroll, 0, drawingTop + drawingHeight - 20, drawingWidth - 20, 20, TRUE);
    }
}
```

---

## Migration Checklist

### Phase 1: Core Infrastructure
- [ ] Create `scaffold.h` with structure definitions
- [ ] Create `scaffold.c` with basic window creation
- [ ] Create `actions.h` and `actions.c` for command handling
- [ ] Register scaffold window class in `main.c`

### Phase 2: Tab Control
- [ ] Implement tab control creation
- [ ] Implement tab switching logic
- [ ] Implement tab closing with close button
- [ ] Implement new tab button

### Phase 3: Toolbar Refactoring
- [ ] Move toolbar to scaffold
- [ ] Add URL bar to toolbar
- [ ] Add menu button (burger menu)
- [ ] Create popup menu

### Phase 4: Window Management
- [ ] Modify `gui_window_create` to use scaffold
- [ ] Modify `gui_window_destroy` to handle tabs
- [ ] Update scroll handling for scaffold
- [ ] Update title and icon handling

### Phase 5: Polish
- [ ] Save/restore window position
- [ ] Handle keyboard shortcuts
- [ ] Update throbber animation
- [ ] Test all menu items

---

## API Reference

### Key Win32 Controls Used

| Control | Class Name | Purpose |
|---------|------------|---------|
| Tab Control | `WC_TABCONTROL` | Tab bar |
| Toolbar | `TOOLBARCLASSNAME` | Navigation buttons |
| Status Bar | `STATUSCLASSNAME` | Status text |
| Button | `BUTTON` | Menu button, new tab button |
| Edit | `EDIT` | URL entry |
| Scrollbar | `SCROLLBAR` | Content scrolling |

### Key Messages

| Message | Control | Purpose |
|---------|---------|---------|
| `TCM_INSERTITEM` | Tab Control | Add new tab |
| `TCM_DELETEITEM` | Tab Control | Remove tab |
| `TCN_SELCHANGE` | Tab Control | Tab switched |
| `TB_ADDBUTTONS` | Toolbar | Add buttons |
| `TB_SETSTATE` | Toolbar | Enable/disable button |

---

## Testing Plan

1. **Basic Tab Operations**
   - Open new tab
   - Switch between tabs
   - Close tab
   - Close last tab (should close window)

2. **Navigation**
   - Back/forward buttons update correctly
   - URL bar updates on navigation
   - Stop/reload button toggles

3. **Menu Operations**
   - All menu items work
   - Keyboard shortcuts work
   - Context menu works

4. **Window Management**
   - Window position saved/restored
   - Multiple windows work independently
   - Closing window closes all tabs

---

## References

- Qt Scaffolding: [`frontends/qt/scaffolding.cpp`](../frontends/qt/scaffolding.cpp)
- Qt URL Bar: [`frontends/qt/urlbar.cpp`](../frontends/qt/urlbar.cpp)
- Qt Actions: [`frontends/qt/actions.cls.h`](../frontends/qt/actions.cls.h)
- Windows Window: [`frontends/windows/window.c`](../frontends/windows/window.c)
- Windows Resources: [`frontends/windows/resourceid.h`](../frontends/windows/resourceid.h)
