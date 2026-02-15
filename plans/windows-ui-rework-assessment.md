# Windows UI Rework Plan Assessment

## Executive Summary

The Windows UI rework plan is **feasible and well-designed**, following the correct architectural pattern to bring the Windows frontend in line with the Qt frontend's tabbed interface. The plan correctly identifies the architectural changes needed to transition from a "one-window-per-browser-context" model to a "tabbed scaffolding" model.

**Overall Assessment: ✓ Feasible with Recommended Improvements**

---

## 1. Current Architecture Analysis

### Existing Windows Frontend Structure

The current Windows frontend in [`frontends/windows/window.h`](frontends/windows/window.h) uses a 1:1 relationship between `gui_window` and native Windows windows:

```c
struct gui_window {
    struct browser_window *bw;
    HWND main;          // Top-level window
    HWND toolbar;       // Toolbar
    HWND urlbar;        // URL input
    HWND throbber;      // Loading animation
    HWND drawingarea;   // Browser canvas
    HWND statusbar;     // Status bar
    HWND vscroll;       // Vertical scrollbar
    HWND hscroll;       // Horizontal scrollbar
    HMENU mainmenu;     // Traditional menu bar
    // ...
};
```

### Target Architecture (Qt Reference)

The Qt frontend uses a scaffolding pattern in [`frontends/qt/scaffolding.cpp`](frontends/qt/scaffolding.cpp):
- **NS_Scaffold**: QTabWidget containing multiple tabs
- **NS_Window**: Individual tab content with browser canvas
- **NS_URLBar**: Toolbar with embedded URL bar and burger menu (⋮)
- Shared toolbar/navigation across all tabs

---

## 2. Feasibility Assessment

### ✅ Strengths of the Plan

1. **Correct Architectural Pattern**: The scaffold approach mirrors the Qt frontend correctly
2. **Comprehensive Data Structures**: The proposed [`gui_scaffold`](plans/windows-ui-rework-plan.md#11-scaffold-structure) structure is well-designed with all necessary handles
3. **Clear Migration Path**: The phased approach in the Migration Checklist is logical
4. **Appropriate Win32 Controls**: Use of WC_TABCONTROL, TOOLBARCLASSNAME, STATUSCLASSNAME is correct

### ⚠️ Gaps and Concerns

#### 2.1 Tab Close Buttons

**Issue**: The plan mentions "Tab Item (with close button)" but doesn't detail implementation.

**Windows Tab Control Capabilities**: The standard `WC_TABCONTROL` (SysTabControl32) does **NOT** have built-in per-tab close button support across any Windows version. This is a well-known limitation of the native control.

**Available Approaches**:

1. **Custom Drawing (TCS_OWNERDRAWFIXED)**
   - Handle `WM_DRAWITEM` and draw the close button (X) yourself
   - Handle `WM_MOUSEMOVE` for hover states
   - Handle `WM_LBUTTONDOWN` to detect clicks on the close button
   - Most flexible but most code to write

2. **Toolbar Embedded in Tab Control**
   - Create a `TOOLBARCLASSNAME` as a child of the tab control
   - Position small toolbar buttons at the right edge of each tab
   - Uses standard toolbar button functionality

3. **Separate Close Button**
   - Position a small button (with "X" text or bitmap) to the right of the tab bar
   - The button closes the currently selected tab
   - Simplest implementation but less intuitive (no per-tab close)

4. **REBAR with Toolbar**
   - Use a REBAR control containing a toolbar
   - More modern appearance possible
   - More complex to implement

#### REBAR with Toolbar - Detailed Explanation

The **REBAR** control (`WC_REBAR` / Rebar32) is a container for other controls, commonly used to create toolbars that can be resized and repositioned by users (like in Internet Explorer). It allows grouping multiple controls (toolbars, combo boxes, etc.) into a single banded bar.

**Implementation Structure**:
```
REBAR Band 0: Toolbar (Back | Forward | Reload | Stop)
REBAR Band 1: ComboBox/Edit (URL Bar)
REBAR Band 2: Toolbar (Zoom controls | Menu Button)
```

**For Tab Close Buttons with REBAR**:
```
Tab Control
REBAR (below tabs, contains):
  - Toolbar with close buttons positioned dynamically
```

**REBAR Advantages**:
- Automatic sizing and positioning
- Can have gripper bands
- Modern look similar to IE/Edge
- Bands can be customized at runtime

**REBAR Disadvantages**:
- More complex to set up
- Requires ICC_REBAR_CLASSES in InitCommonControlsEx
- More code overhead
- Overkill if simple toolbar suffices

**Code Setup**:
```c
INITCOMMONCONTROLSEX icex;
icex.dwICC = ICC_REBAR_CLASSES;
InitCommonControlsEx(&icex);

HWND hwndRebar = CreateWindowExW(
    0, WC_REBARW, NULL,
    WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_BORDER | CCS_NODIVIDER,
    0, 0, 0, 0, hParent, NULL, hInst, NULL
);

// Add bands with toolbars containing tab close buttons
REBARBANDINFO rbbi;
rbbi.cbSize = sizeof(REBARBANDINFO);
rbbi.hwndChild = hwndToolbar;
rbbi.lpText = (LPWSTR)L"";
rbbi.cx = 200;
rbbi.fStyle = RBBS_CHILDEDGE | RBBS_NOGRIPPER;
SendMessage(hwndRebar, RB_INSERTBAND, (WPARAM)-1, (LPARAM)&rbbi);
```

**Recommendation**: For this project, start with **separate close button** or **toolbar in tab control**. REBAR is more appropriate if you want IE/Edge-style bands with grippers, which adds complexity without much benefit for a browser toolbar.

#### Available Libraries for Tab Close Buttons

After reviewing the project dependencies (only standard Windows libraries are used), here are the available options:

1. **Windows Template Library (WTL)** - Not currently used
   - Provides `CTabView` with close button support
   - Requires adding WTL as a dependency
   - C++ template library, lightweight
   - Not currently in project

2. **MFC (Microsoft Foundation Classes)** - Not used
   - `CTabCtrl` doesn't have built-in close buttons
   - Would require switching from raw Win32 to MFC
   - Heavy dependency

3. **DuiLib / UILib** - Not used
   - Third-party C++ UI libraries
   - Include tab controls with close buttons
   - Not in project, would add external dependency

4. **Win32++** - Not used
   - C++ wrapper around Win32
   - Provides tab controls with close buttons
   - Not in project

5. **No library - Custom Implementation** (Recommended)
   - The standard approach in Win32 applications
   - Implement ~50-100 lines of custom drawing code
   - Gives full control over appearance
   - No external dependencies

**Recommendation**: Use **custom implementation** - it's the standard approach for Win32 applications (used by Firefox, Chrome's Windows port, etc.) and doesn't add external dependencies. The code is straightforward and well-documented.

#### How Other Browsers Implement Tab Close Buttons

1. **Chrome (Chromium)**
   - Uses its own **custom tab control** called `TabStrip`
   - Implemented in `chrome/browser/ui/views/tab_search_container.cc` and related files
   - Completely custom-drawn tabs with close buttons, hover states, favicons
   - NOT using native Windows controls at all
   - Uses the **views** framework (cross-platform UI toolkit)
   - This is why Chrome's tabs look different on every platform

2. **Firefox**
   - Uses **XUL/HTML-based tabs** in older versions (Firefox 52-)
   - Modern Firefox uses **Photon components** with custom rendering
   - Tab strip is implemented as a flexiblebox/grid with custom JavaScript
   - Close button is a simple `<image>` or `<button>` element styled with CSS

3. **Edge (Chromium-based)**
   - Same as Chrome since it uses Chromium
   - Custom tab strip implementation

4. **Internet Explorer**
   - Used **REBAR** control with custom bands
   - Toolbar embedded within the REBAR for navigation
   - Custom drawing for hover states

### Common Pattern

All modern browsers **do NOT use native Windows tab controls**. They either:
- Build custom tab controls from scratch (Chrome/Edge)
- Use web technologies for UI (Firefox's CSS-based tabs)
- Use cross-platform UI frameworks (Electron apps)

This is because the native Windows Tab Control is too limited for modern browser requirements.

### Recommendation for This Project

Given that even major browsers build custom solutions, the simplest approach for this project is:
1. **Start with separate close button** (like Ctrl+W keyboard shortcut UX)
2. **Later upgrade to custom drawing** if needed for polished appearance

This avoids the complexity of building a full custom tab strip while still providing good UX.

**Recommendation**: Use **Option 3 (Separate Close Button)** for initial implementation - it's simplest and provides good UX (like pressing Ctrl+W). Later upgrade to **Option 1 (Custom Drawing)** for a more polished look matching Qt.

**Current Qt Implementation**: Uses QTabWidget's built-in `setTabsClosable(true)` which handles this automatically (from [`qt/scaffolding.cpp:84`](frontends/qt/scaffolding.cpp:84)):
```cpp
connect(tabBar(), &QTabBar::tabCloseRequested, this, &NS_Scaffold::destroyTab);
setTabsClosable(true);
```

#### 2.2 Browser Window Callbacks Integration

**Issue**: The plan references navigation callbacks but doesn't detail how the scaffold receives:
- Title changes (`NS_Scaffold::changeTabTitle`)
- URL changes 
- Loading state (throbber updates)
- Page info updates

**Current Qt Implementation** (from [`qt/scaffolding.cpp`](frontends/qt/scaffolding.cpp:141)):
```cpp
void NS_Scaffold::changeTabTitle(const char *title)
{
    // iterates tabs to find sender and update title
}
```

**Recommendation**: Add callback integration section explaining how browser window events propagate to scaffold.

#### 2.3 Toolbar Layout Complexity

**Issue**: The proposed toolbar layout in Step 4 requires embedding an EDIT control (URL bar) into a toolbar. This is non-trivial in Win32.

**Details**: The plan shows:
```c
// Insert URL bar into toolbar
TBBUTTON tbUrl = {0};
tbUrl.idCommand = IDC_MAIN_URLBAR;
tbUrl.fsStyle = BTNS_SEP;
tbUrl.iBitmap = 200;  // Width in pixels
SendMessage(hWndToolbar, TB_INSERTBUTTON, 3, (LPARAM)&tbUrl);
```

This approach works but the URL bar won't be properly aligned. A REBAR control or custom layout would be more robust.

**Recommendation**: Consider using a REBAR control or separate toolbar for URL bar, or document the limitations.

#### 2.4 Zoom Controls

**Issue**: The plan mentions "Zoom Reset Button" but:
- Doesn't fully implement zoom UI in toolbar
- Doesn't address per-tab vs global zoom state

**Current Qt Implementation** (from [`qt/actions.cls.h`](frontends/qt/actions.cls.h:85)):
```cpp
QAction *m_page_scale;
QAction *m_reset_page_scale;
QAction *m_reduce_page_scale;
QAction *m_increase_page_scale;
```

**Recommendation**: Add zoom menu implementation to the burger menu.

#### 2.5 Thread Safety

**Issue**: Not explicitly addressed in the plan.

**Details**: Windows message loop is single-threaded, but browser core may invoke callbacks from different threads. Need to ensure `SendMessage` is used for cross-thread communication.

**Recommendation**: Add thread safety considerations to the plan.

---

## 3. Missing Implementation Details

### 3.1 Files to Modify (Incomplete)

The plan lists these files to modify:

| File | Changes | Status |
|------|---------|--------|
| window.h | Remove main window handles, add scaffold reference | ✓ Defined |
| window.c | Remove window creation, add tab content creation | ✓ Defined |
| drawable.c | Update to work with scaffold scrollbars | ⚠️ Needs detail |
| resourceid.h | Add new control IDs | ✓ Defined |
| res/resource.rc | Remove menu bar, add popup menu definition | ⚠️ Needs detail |
| main.c | Initialize scaffold class | ⚠️ Needs detail |
| gui.h | Add scaffold-related declarations | ⚠️ Needs detail |

**Missing from plan**:
- **CMakeLists.txt**: New files need to be added to build
- **gui.c**: May need modifications for scaffold lifecycle
- **local_history.c**: May need updates for scaffold model

### 3.2 Window Message Routing

**Missing details**:
- How WM_COMMAND messages route between scaffold and tab content
- How WM_NOTIFY (especially TCN_SELCHANGE) is handled
- How scroll bar messages propagate to active tab's drawing area

### 3.3 State Management

**Missing details**:
- How back/forward button state syncs with current tab's history
- How URL bar content updates on tab switch
- How throbber state syncs with current tab's loading state

---

## 4. API Compatibility Analysis

### 4.1 Required Core APIs

The plan correctly uses these Win32 APIs:

| API | Purpose | ✓/⚠ |
|-----|---------|-----|
| `RegisterClassExW` | Window class registration | ✓ |
| `CreateWindowExW` | Window creation | ✓ |
| `TabCtrl_InsertItem` | Add tabs | ✓ |
| `TabCtrl_SetCurSel` | Switch tabs | ✓ |
| `TB_ADDBUTTONS` | Toolbar buttons | ✓ |
| `TrackPopupMenu` | Burger menu | ✓ |

### 4.2 Resource IDs

The plan adds:
```c
#define IDC_SCAFFOLD_TABCONTROL  2000
#define IDC_SCAFFOLD_NEWTAB      2001
#define IDC_SCAFFOLD_MENU        2002
#define IDC_SCAFFOLD_PAGEINFO    2003
#define IDC_SCAFFOLD_BOOKMARK    2004
#define IDC_SCAFFOLD_ZOOMRESET   2005
#define IDR_MENU_POPUP           12000
```

**Assessment**: IDs are in reasonable ranges and don't conflict with existing IDs in [`resourceid.h`](frontends/windows/resourceid.h).

---

## 5. Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Complex message routing | Medium | Add detailed WM_COMMAND/WM_NOTIFY handling |
| Tab close button styling | Medium | Use custom draw or toolbar approach |
| Toolbar layout alignment | Low | Accept simple separator or use REBAR |
| Regression in navigation | High | Comprehensive testing for each phase |
| Resource cleanup | Medium | Ensure proper DestroyWindow calls |

---

## 6. Recommendations

### High Priority

1. **Add callback integration section** explaining how browser window events (title, URL, loading) propagate to scaffold

2. **Detail tab close button implementation** - either custom draw or toolbar approach

3. **Add test cases to plan** for:
   - Tab switching preserves scroll position
   - Back/forward updates per-tab history
   - Last tab close exits application

### Medium Priority

4. **Add REBAR or alternative toolbar** consideration for better URL bar alignment

5. **Document thread safety requirements** for message handling

6. **Add zoom controls to burger menu** implementation

### Low Priority

7. **Consider keyboard shortcuts per-tab** (Ctrl+Tab, Ctrl+Shift+Tab)

8. **Add window restore/position** persistence for scaffold (not just individual windows)

---

## 7. Conclusion

The Windows UI rework plan is **feasible and well-structured**. The proposed scaffold architecture correctly replicates the Qt frontend's tabbed interface. The main areas needing attention are:

1. Tab close button implementation details
2. Browser window callback integration
3. Toolbar layout complexity
4. Comprehensive testing plan

The plan should proceed with the recommended additions to ensure successful implementation.

---

## Appendix: Code References

- Current Windows window: [`frontends/windows/window.c`](frontends/windows/window.c)
- Current gui_window: [`frontends/windows/window.h`](frontends/windows/window.h)
- Qt scaffolding: [`frontends/qt/scaffolding.cpp`](frontends/qt/scaffolding.cpp)
- Qt actions: [`frontends/qt/actions.cls.h`](frontends/qt/actions.cls.h)
- Qt URL bar: [`frontends/qt/urlbar.cls.h`](frontends/qt/urlbar.cls.h)
