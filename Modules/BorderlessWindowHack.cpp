#include "../eqclientmod.h"

#ifdef BORDERLESS_WINDOW_HACK
#include <stdlib.h>
#include "../common.h"
#include "../util.h"
#include "../settings.h"

/*
    EQ windowed borderless hack.

    Startup policy:
      - The first CreateWindowExA for "_EverQuestwndclass" decides whether
        this process should use borderless windowed mode.
      - If EQ asks for a decorated outer window whose client area would equal
        the monitor/desktop resolution, this mod activates.
      - If EQ starts with a smaller resizable window, this mod remains passive
        for the whole process.
      - Later manual resize to monitor size does NOT activate the mod.

    Front-end policy:
      - eqmain.dll owns some of the front-end window behavior.
      - Patch eqgame.exe before entry.
      - Hook LoadLibraryA / LoadLibraryExA / LoadModuleA.
      - When eqmain.dll is loaded, patch its IAT too.
      - eqmain.dll may unload/reload, so patch it each time it appears.

    Enforcement when active:
      - CreateWindowExA
      - SetWindowLongA
      - SetWindowPos
      - ShowWindow

    Uses rcMonitor, not rcWork, so it covers the taskbar.
*/


/* ------------------------------------------------------------------------- */
/* Constants                                                                 */
/* ------------------------------------------------------------------------- */

#define FWHB_EQ_WINDOW_CLASS_NAME                   "_EverQuestwndclass"
#define FWHB_EQMAIN_DLL_NAME                        "eqmain.dll"

/*
    We are matching the requested decorated outer size against what Windows
    says a monitor-sized client would need for the original style/exstyle.

    This should normally match exactly. A tiny slop avoids false negatives
    from old-client / DWM / compatibility-mode rounding differences without
    turning this into broad fuzzy detection.
*/
#define FWHB_STARTUP_MATCH_SLOP                     8

#define FWHB_PATCH_CREATEWINDOWEXA                  1
#define FWHB_PATCH_SETWINDOWLONGA                   1
#define FWHB_PATCH_SETWINDOWPOS                     1
#define FWHB_PATCH_SHOWWINDOW                       1
#define FWHB_PATCH_LOADLIBRARYA                     1
#define FWHB_PATCH_LOADLIBRARYEXA                   1
#define FWHB_PATCH_LOADMODULEA                      1

static BOOL g_fwhb_debug = FALSE;


/* ------------------------------------------------------------------------- */
/* Function pointer types                                                    */
/* ------------------------------------------------------------------------- */

typedef HWND (WINAPI *FWHB_CreateWindowExA_t)(
    DWORD dwExStyle,
    LPCSTR lpClassName,
    LPCSTR lpWindowName,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam
);

typedef BOOL (WINAPI *FWHB_SetWindowPos_t)(
    HWND hWnd,
    HWND hWndInsertAfter,
    int X,
    int Y,
    int cx,
    int cy,
    UINT uFlags
);

typedef BOOL (WINAPI *FWHB_ShowWindow_t)(HWND hWnd, int nCmdShow);

typedef LONG (WINAPI *FWHB_SetWindowLongA_t)(HWND hWnd, int nIndex, LONG dwNewLong);

typedef HMODULE (WINAPI *FWHB_LoadLibraryA_t)(LPCSTR lpLibFileName);

typedef HMODULE (WINAPI *FWHB_LoadLibraryExA_t)(
    LPCSTR lpLibFileName,
    HANDLE hFile,
    DWORD dwFlags
);

typedef UINT (WINAPI *FWHB_LoadModuleA_t)(
    LPCSTR lpModuleName,
    LPVOID lpParameterBlock
);


/* ------------------------------------------------------------------------- */
/* Originals                                                                 */
/* ------------------------------------------------------------------------- */

static FWHB_CreateWindowExA_t      g_fwhb_real_CreateWindowExA = NULL;
static FWHB_SetWindowPos_t         g_fwhb_real_SetWindowPos = NULL;
static FWHB_ShowWindow_t           g_fwhb_real_ShowWindow = NULL;
static FWHB_SetWindowLongA_t       g_fwhb_real_SetWindowLongA = NULL;

static FWHB_LoadLibraryA_t         g_fwhb_real_LoadLibraryA = NULL;
static FWHB_LoadLibraryExA_t       g_fwhb_real_LoadLibraryExA = NULL;
static FWHB_LoadModuleA_t          g_fwhb_real_LoadModuleA = NULL;


/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

static BOOL g_fwhb_enabled = FALSE;

static BOOL g_fwhb_startupDecisionMade = FALSE;
static BOOL g_fwhb_startupEligible = FALSE;

static HWND g_fwhb_targetHwnd = NULL;

static BOOL g_fwhb_inApply = FALSE;
static BOOL g_fwhb_inPatchModule = FALSE;


/* ------------------------------------------------------------------------- */
/* Forward declarations                                                      */
/* ------------------------------------------------------------------------- */

static void FWHB_ApplyBorderless(HWND hwnd, const char* reason);

static BOOL FWHB_IsEQMainWindow(HWND hwnd);
static BOOL FWHB_IsActiveTargetWindow(HWND hwnd, const char* where);

static void FWHB_PatchWindowedBorderlessForModule(HMODULE hMod, const char* moduleName);
static void FWHB_TryPatchEqMainDll(const char* where, LPCSTR requestedName, HMODULE hMaybe);


/* ------------------------------------------------------------------------- */
/* Raw Win32 calls                                                           */
/* ------------------------------------------------------------------------- */

static LONG FWHB_RawSetWindowLongA(HWND hwnd, int index, LONG value)
{
    if (g_fwhb_real_SetWindowLongA)
        return g_fwhb_real_SetWindowLongA(hwnd, index, value);

    return SetWindowLongA(hwnd, index, value);
}

static BOOL FWHB_RawSetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    if (g_fwhb_real_SetWindowPos)
        return g_fwhb_real_SetWindowPos(hwnd, after, x, y, cx, cy, flags);

    return SetWindowPos(hwnd, after, x, y, cx, cy, flags);
}


/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static int FWHB_AbsInt(int x)
{
    return x < 0 ? -x : x;
}

static int FWHB_RectWidth(const RECT* r)
{
    return (int)(r->right - r->left);
}

static int FWHB_RectHeight(const RECT* r)
{
    return (int)(r->bottom - r->top);
}

static BOOL FWHB_IsProbablyAtomStringA(LPCSTR s)
{
    return s != NULL && HIWORD((ULONG_PTR)s) == 0;
}

static const char* FWHB_SafeClassNameA(LPCSTR s)
{
    if (s == NULL)
        return "(null)";

    if (FWHB_IsProbablyAtomStringA(s))
        return "(atom)";

    return s;
}

static BOOL FWHB_IsEQClassNameA(LPCSTR className)
{
    if (className == NULL)
        return FALSE;

    if (FWHB_IsProbablyAtomStringA(className))
        return FALSE;

    return lstrcmpiA(className, FWHB_EQ_WINDOW_CLASS_NAME) == 0;
}

static const char* FWHB_BaseNameA(const char* path)
{
    const char* p;
    const char* last;

    if (path == NULL)
        return NULL;

    last = path;
    p = path;

    while (*p)
    {
        if (*p == '\\' || *p == '/')
            last = p + 1;

        ++p;
    }

    return last;
}

static BOOL FWHB_IsEqMainModuleNameA(LPCSTR name)
{
    const char* base;

    if (name == NULL)
        return FALSE;

    base = FWHB_BaseNameA(name);

    if (base == NULL)
        return FALSE;

    return lstrcmpiA(base, FWHB_EQMAIN_DLL_NAME) == 0;
}

static BOOL FWHB_IsEqMainModuleHandle(HMODULE hMod)
{
    char path[MAX_PATH];
    const char* base;

    if (hMod == NULL)
        return FALSE;

    ZeroMemory(path, sizeof(path));

    if (!GetModuleFileNameA(hMod, path, sizeof(path)))
        return FALSE;

    base = FWHB_BaseNameA(path);

    if (base == NULL)
        return FALSE;

    return lstrcmpiA(base, FWHB_EQMAIN_DLL_NAME) == 0;
}

static BOOL FWHB_IsCurrentProcessWindow(HWND hwnd)
{
    DWORD pid;

    if (hwnd == NULL)
        return FALSE;

    if (!IsWindow(hwnd))
        return FALSE;

    pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    return pid == GetCurrentProcessId();
}

static BOOL FWHB_IsTopLevelCurrentProcessWindow(HWND hwnd)
{
    if (!FWHB_IsCurrentProcessWindow(hwnd))
        return FALSE;

    if (GetParent(hwnd) != NULL)
        return FALSE;

    return TRUE;
}

static BOOL FWHB_IsEQMainWindow(HWND hwnd)
{
    char cls[128];

    if (!FWHB_IsTopLevelCurrentProcessWindow(hwnd))
        return FALSE;

    ZeroMemory(cls, sizeof(cls));
    GetClassNameA(hwnd, cls, sizeof(cls));

    return lstrcmpiA(cls, FWHB_EQ_WINDOW_CLASS_NAME) == 0;
}

static BOOL FWHB_GetMonitorInfoForWindow(HWND hwnd, MONITORINFO* mi)
{
    HMONITOR mon;

    if (hwnd == NULL || mi == NULL)
        return FALSE;

    mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    ZeroMemory(mi, sizeof(MONITORINFO));
    mi->cbSize = sizeof(MONITORINFO);

    return GetMonitorInfo(mon, mi);
}

static BOOL FWHB_GetMonitorInfoForPointOrPrimary(int x, int y, MONITORINFO* mi)
{
    POINT pt;
    HMONITOR mon;

    if (mi == NULL)
        return FALSE;

    if (x == CW_USEDEFAULT)
        x = 0;

    if (y == CW_USEDEFAULT)
        y = 0;

    pt.x = x;
    pt.y = y;

    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    ZeroMemory(mi, sizeof(MONITORINFO));
    mi->cbSize = sizeof(MONITORINFO);

    return GetMonitorInfo(mon, mi);
}

static void FWHB_ClearStaleTarget(void)
{
    if (g_fwhb_targetHwnd != NULL && !IsWindow(g_fwhb_targetHwnd))
    {
        Log(
            "FWHB_ClearStaleTarget: stale target hwnd=0x%08X cleared",
            (unsigned int)(ULONG_PTR)g_fwhb_targetHwnd
        );

        g_fwhb_targetHwnd = NULL;
    }
}

static void FWHB_LogWindowState(const char* where, HWND hwnd)
{
    RECT wr;
    RECT cr;
    char cls[128];
    char title[256];
    LONG style;
    LONG exstyle;
    BOOL visible;
    BOOL iconic;
    BOOL zoomed;

    if (hwnd == NULL)
    {
        Log("%s: hwnd=NULL", where);
        return;
    }

    ZeroMemory(&wr, sizeof(wr));
    ZeroMemory(&cr, sizeof(cr));
    ZeroMemory(cls, sizeof(cls));
    ZeroMemory(title, sizeof(title));

    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    GetClassNameA(hwnd, cls, sizeof(cls));
    GetWindowTextA(hwnd, title, sizeof(title));

    style = GetWindowLong(hwnd, GWL_STYLE);
    exstyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    visible = IsWindowVisible(hwnd);
    iconic = IsIconic(hwnd);
    zoomed = IsZoomed(hwnd);

    Log(
        "%s: hwnd=0x%08X class='%s' title='%s' style=0x%08X ex=0x%08X "
        "window=%ld,%ld,%ld,%ld (%dx%d) client=%ld,%ld,%ld,%ld (%dx%d) visible=%u iconic=%u zoomed=%u",
        where,
        (unsigned int)(ULONG_PTR)hwnd,
        cls,
        title,
        (unsigned int)style,
        (unsigned int)exstyle,
        wr.left,
        wr.top,
        wr.right,
        wr.bottom,
        FWHB_RectWidth(&wr),
        FWHB_RectHeight(&wr),
        cr.left,
        cr.top,
        cr.right,
        cr.bottom,
        FWHB_RectWidth(&cr),
        FWHB_RectHeight(&cr),
        (unsigned int)visible,
        (unsigned int)iconic,
        (unsigned int)zoomed
    );
}


/* ------------------------------------------------------------------------- */
/* Startup eligibility                                                       */
/* ------------------------------------------------------------------------- */

static BOOL FWHB_CreateArgsMatchDesktopSizedClient(
    DWORD dwExStyle,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    MONITORINFO* outMi,
    int* outMonitorW,
    int* outMonitorH,
    int* outExpectedOuterW,
    int* outExpectedOuterH)
{
    MONITORINFO mi;
    RECT r;
    int monitorW;
    int monitorH;
    int expectedOuterW;
    int expectedOuterH;

    if (!FWHB_GetMonitorInfoForPointOrPrimary(X, Y, &mi))
        return FALSE;

    monitorW = FWHB_RectWidth(&mi.rcMonitor);
    monitorH = FWHB_RectHeight(&mi.rcMonitor);

    r.left = 0;
    r.top = 0;
    r.right = monitorW;
    r.bottom = monitorH;

    if (!AdjustWindowRectEx(&r, dwStyle, FALSE, dwExStyle))
        return FALSE;

    expectedOuterW = FWHB_RectWidth(&r);
    expectedOuterH = FWHB_RectHeight(&r);

    if (outMi)
        *outMi = mi;

    if (outMonitorW)
        *outMonitorW = monitorW;

    if (outMonitorH)
        *outMonitorH = monitorH;

    if (outExpectedOuterW)
        *outExpectedOuterW = expectedOuterW;

    if (outExpectedOuterH)
        *outExpectedOuterH = expectedOuterH;

    if (FWHB_AbsInt(nWidth - expectedOuterW) <= FWHB_STARTUP_MATCH_SLOP &&
        FWHB_AbsInt(nHeight - expectedOuterH) <= FWHB_STARTUP_MATCH_SLOP)
    {
        return TRUE;
    }

    return FALSE;
}

static void FWHB_MakeStartupDecisionFromCreateA(
    LPCSTR lpClassName,
    DWORD dwExStyle,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight)
{
    MONITORINFO mi;
    int monitorW;
    int monitorH;
    int expectedOuterW;
    int expectedOuterH;

    if (g_fwhb_startupDecisionMade)
        return;

    if (!FWHB_IsEQClassNameA(lpClassName))
        return;

    ZeroMemory(&mi, sizeof(mi));

    monitorW = 0;
    monitorH = 0;
    expectedOuterW = 0;
    expectedOuterH = 0;

    g_fwhb_startupDecisionMade = TRUE;

    g_fwhb_startupEligible = FWHB_CreateArgsMatchDesktopSizedClient(
        dwExStyle,
        dwStyle,
        X,
        Y,
        nWidth,
        nHeight,
        &mi,
        &monitorW,
        &monitorH,
        &expectedOuterW,
        &expectedOuterH
    );

    Log(
        "FWHB startup decision: eligible=%u requestedOuter=%dx%d expectedOuter=%dx%d desktopClient=%dx%d "
        "monitor=%ld,%ld,%ld,%ld style=0x%08X ex=0x%08X slop=%d",
        (unsigned int)g_fwhb_startupEligible,
        nWidth,
        nHeight,
        expectedOuterW,
        expectedOuterH,
        monitorW,
        monitorH,
        mi.rcMonitor.left,
        mi.rcMonitor.top,
        mi.rcMonitor.right,
        mi.rcMonitor.bottom,
        (unsigned int)dwStyle,
        (unsigned int)dwExStyle,
        FWHB_STARTUP_MATCH_SLOP
    );

    if (!g_fwhb_startupEligible)
    {
        Log(
            "FWHB startup decision: not activating; normal resizable window behavior will be preserved for this process"
        );
    }
}

static BOOL FWHB_IsActive(void)
{
    if (!g_fwhb_enabled)
        return FALSE;

    if (!g_fwhb_startupDecisionMade)
        return FALSE;

    if (!g_fwhb_startupEligible)
        return FALSE;

    return TRUE;
}


/* ------------------------------------------------------------------------- */
/* Style and rect policy                                                     */
/* ------------------------------------------------------------------------- */

static LONG FWHB_SanitizeStyleValue(int nIndex, LONG value)
{
    if (nIndex == GWL_STYLE)
    {
        value &= ~(WS_CAPTION |
                   WS_THICKFRAME |
                   WS_MINIMIZEBOX |
                   WS_MAXIMIZEBOX |
                   WS_SYSMENU);

        value |= WS_POPUP;
        value |= WS_VISIBLE;

        return value;
    }

    if (nIndex == GWL_EXSTYLE)
    {
        value &= ~(WS_EX_DLGMODALFRAME |
                   WS_EX_CLIENTEDGE |
                   WS_EX_STATICEDGE |
                   WS_EX_WINDOWEDGE);

        return value;
    }

    return value;
}

static DWORD FWHB_SanitizeCreateStyle(DWORD style)
{
    style &= ~(WS_CAPTION |
               WS_THICKFRAME |
               WS_MINIMIZEBOX |
               WS_MAXIMIZEBOX |
               WS_SYSMENU);

    style |= WS_POPUP;
    style |= WS_VISIBLE;

    return style;
}

static DWORD FWHB_SanitizeCreateExStyle(DWORD exstyle)
{
    exstyle &= ~(WS_EX_DLGMODALFRAME |
                 WS_EX_CLIENTEDGE |
                 WS_EX_STATICEDGE |
                 WS_EX_WINDOWEDGE);

    return exstyle;
}

static BOOL FWHB_IsActiveTargetWindow(HWND hwnd, const char* where)
{
    if (!FWHB_IsActive())
        return FALSE;

    if (hwnd == NULL)
        return FALSE;

    FWHB_ClearStaleTarget();

    if (!FWHB_IsEQMainWindow(hwnd))
        return FALSE;

    if (g_fwhb_targetHwnd == NULL)
    {
        g_fwhb_targetHwnd = hwnd;

        Log(
            "FWHB target selected: where=%s hwnd=0x%08X",
            where ? where : "(unknown)",
            (unsigned int)(ULONG_PTR)hwnd
        );

        if (g_fwhb_debug)
            FWHB_LogWindowState("FWHB target selected detail", hwnd);
    }

    return hwnd == g_fwhb_targetHwnd;
}

static BOOL FWHB_RewriteCreateWindowForBorderlessA(
    DWORD* pStyle,
    DWORD* pExStyle,
    int* pX,
    int* pY,
    int* pWidth,
    int* pHeight)
{
    MONITORINFO mi;

    if (!FWHB_IsActive())
        return FALSE;

    if (pStyle == NULL || pExStyle == NULL || pX == NULL || pY == NULL || pWidth == NULL || pHeight == NULL)
        return FALSE;

    if (!FWHB_GetMonitorInfoForPointOrPrimary(*pX, *pY, &mi))
        return FALSE;

    Log(
        "FWHB_RewriteCreateWindowForBorderlessA: old x=%d y=%d w=%d h=%d style=0x%08X ex=0x%08X",
        *pX,
        *pY,
        *pWidth,
        *pHeight,
        (unsigned int)(*pStyle),
        (unsigned int)(*pExStyle)
    );

    *pStyle = FWHB_SanitizeCreateStyle(*pStyle);
    *pExStyle = FWHB_SanitizeCreateExStyle(*pExStyle);

    *pX = mi.rcMonitor.left;
    *pY = mi.rcMonitor.top;
    *pWidth = FWHB_RectWidth(&mi.rcMonitor);
    *pHeight = FWHB_RectHeight(&mi.rcMonitor);

    Log(
        "FWHB_RewriteCreateWindowForBorderlessA: new x=%d y=%d w=%d h=%d style=0x%08X ex=0x%08X",
        *pX,
        *pY,
        *pWidth,
        *pHeight,
        (unsigned int)(*pStyle),
        (unsigned int)(*pExStyle)
    );

    return TRUE;
}

static BOOL FWHB_RewriteSetWindowPosForBorderless(
    HWND hWnd,
    HWND* pAfter,
    int* pX,
    int* pY,
    int* pCx,
    int* pCy,
    UINT* pFlags)
{
    MONITORINFO mi;
    int oldX;
    int oldY;
    int oldCx;
    int oldCy;
    UINT oldFlags;

    if (!FWHB_IsActiveTargetWindow(hWnd, "SetWindowPos"))
        return FALSE;

    if (!FWHB_GetMonitorInfoForWindow(hWnd, &mi))
        return FALSE;

    if (pAfter == NULL || pX == NULL || pY == NULL || pCx == NULL || pCy == NULL || pFlags == NULL)
        return FALSE;

    oldX = *pX;
    oldY = *pY;
    oldCx = *pCx;
    oldCy = *pCy;
    oldFlags = *pFlags;

    *pAfter = HWND_TOP;
    *pX = mi.rcMonitor.left;
    *pY = mi.rcMonitor.top;
    *pCx = FWHB_RectWidth(&mi.rcMonitor);
    *pCy = FWHB_RectHeight(&mi.rcMonitor);

    *pFlags &= ~SWP_NOMOVE;
    *pFlags &= ~SWP_NOSIZE;
    *pFlags &= ~SWP_HIDEWINDOW;

    *pFlags |= SWP_SHOWWINDOW;
    *pFlags |= SWP_NOOWNERZORDER;

    if (g_fwhb_debug ||
        oldX != *pX ||
        oldY != *pY ||
        oldCx != *pCx ||
        oldCy != *pCy ||
        oldFlags != *pFlags)
    {
        Log(
            "FWHB_RewriteSetWindowPosForBorderless: hwnd=0x%08X old x=%d y=%d cx=%d cy=%d flags=0x%08X "
            "new x=%d y=%d cx=%d cy=%d flags=0x%08X",
            (unsigned int)(ULONG_PTR)hWnd,
            oldX,
            oldY,
            oldCx,
            oldCy,
            (unsigned int)oldFlags,
            *pX,
            *pY,
            *pCx,
            *pCy,
            (unsigned int)(*pFlags)
        );
    }

    return TRUE;
}


/* ------------------------------------------------------------------------- */
/* Apply                                                                     */
/* ------------------------------------------------------------------------- */

static void FWHB_ForceFrameRedraw(HWND hwnd)
{
    DrawMenuBar(hwnd);

    RedrawWindow(
        hwnd,
        NULL,
        NULL,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME | RDW_ALLCHILDREN
    );
}

static void FWHB_ApplyBorderless(HWND hwnd, const char* reason)
{
    MONITORINFO mi;
    LONG style;
    LONG exstyle;
    int x;
    int y;
    int w;
    int h;

    if (!FWHB_IsActiveTargetWindow(hwnd, reason))
        return;

    if (g_fwhb_inApply)
        return;

    if (!FWHB_GetMonitorInfoForWindow(hwnd, &mi))
        return;

    g_fwhb_inApply = TRUE;

    if (g_fwhb_debug)
        FWHB_LogWindowState("FWHB_ApplyBorderless before", hwnd);

    style = GetWindowLong(hwnd, GWL_STYLE);
    exstyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    style = FWHB_SanitizeStyleValue(GWL_STYLE, style);
    exstyle = FWHB_SanitizeStyleValue(GWL_EXSTYLE, exstyle);

    x = mi.rcMonitor.left;
    y = mi.rcMonitor.top;
    w = FWHB_RectWidth(&mi.rcMonitor);
    h = FWHB_RectHeight(&mi.rcMonitor);

    if (g_fwhb_debug)
    {
        Log(
            "FWHB_ApplyBorderless: hwnd=0x%08X monitor=%ld,%ld,%ld,%ld size=%dx%d style=0x%08X ex=0x%08X reason=%s",
            (unsigned int)(ULONG_PTR)hwnd,
            mi.rcMonitor.left,
            mi.rcMonitor.top,
            mi.rcMonitor.right,
            mi.rcMonitor.bottom,
            w,
            h,
            (unsigned int)style,
            (unsigned int)exstyle,
            reason ? reason : "(null)"
        );
    }

    SetMenu(hwnd, NULL);

    FWHB_RawSetWindowLongA(hwnd, GWL_STYLE, style);
    FWHB_RawSetWindowLongA(hwnd, GWL_EXSTYLE, exstyle);

    FWHB_RawSetWindowPos(
        hwnd,
        HWND_TOP,
        x,
        y,
        w,
        h,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER
    );

    FWHB_ForceFrameRedraw(hwnd);

    if (g_fwhb_debug)
        FWHB_LogWindowState("FWHB_ApplyBorderless after", hwnd);

    g_fwhb_inApply = FALSE;
}


/* ------------------------------------------------------------------------- */
/* Hooks                                                                     */
/* ------------------------------------------------------------------------- */

static HWND WINAPI FWHB_Hook_CreateWindowExA(
    DWORD dwExStyle,
    LPCSTR lpClassName,
    LPCSTR lpWindowName,
    DWORD dwStyle,
    int X,
    int Y,
    int nWidth,
    int nHeight,
    HWND hWndParent,
    HMENU hMenu,
    HINSTANCE hInstance,
    LPVOID lpParam)
{
    HWND hwnd;
    DWORD newStyle;
    DWORD newExStyle;
    int newX;
    int newY;
    int newWidth;
    int newHeight;
    BOOL rewritten;

    newStyle = dwStyle;
    newExStyle = dwExStyle;
    newX = X;
    newY = Y;
    newWidth = nWidth;
    newHeight = nHeight;
    rewritten = FALSE;

    if (FWHB_IsEQClassNameA(lpClassName))
    {
        Log(
            "FWHB_Hook_CreateWindowExA: EQ class='%s' title='%s' style=0x%08X ex=0x%08X x=%d y=%d w=%d h=%d parent=0x%08X",
            FWHB_SafeClassNameA(lpClassName),
            lpWindowName ? lpWindowName : "(null)",
            (unsigned int)dwStyle,
            (unsigned int)dwExStyle,
            X,
            Y,
            nWidth,
            nHeight,
            (unsigned int)(ULONG_PTR)hWndParent
        );

        FWHB_MakeStartupDecisionFromCreateA(
            lpClassName,
            dwExStyle,
            dwStyle,
            X,
            Y,
            nWidth,
            nHeight
        );

        rewritten = FWHB_RewriteCreateWindowForBorderlessA(
            &newStyle,
            &newExStyle,
            &newX,
            &newY,
            &newWidth,
            &newHeight
        );
    }

    hwnd = g_fwhb_real_CreateWindowExA(
        newExStyle,
        lpClassName,
        lpWindowName,
        newStyle,
        newX,
        newY,
        newWidth,
        newHeight,
        hWndParent,
        hMenu,
        hInstance,
        lpParam
    );

    if (FWHB_IsEQMainWindow(hwnd))
    {
        Log(
            "FWHB_Hook_CreateWindowExA: EQ returned hwnd=0x%08X rewritten=%u active=%u",
            (unsigned int)(ULONG_PTR)hwnd,
            (unsigned int)rewritten,
            (unsigned int)FWHB_IsActive()
        );

        if (FWHB_IsActive())
        {
            g_fwhb_targetHwnd = hwnd;
            FWHB_ApplyBorderless(hwnd, rewritten ? "CREATE_REWRITTEN" : "CREATE_ACTIVE");
        }
    }

    return hwnd;
}

static BOOL WINAPI FWHB_Hook_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    BOOL ret;
    HWND newAfter;
    int newX;
    int newY;
    int newCx;
    int newCy;
    UINT newFlags;

    newAfter = hWndInsertAfter;
    newX = X;
    newY = Y;
    newCx = cx;
    newCy = cy;
    newFlags = uFlags;

    FWHB_RewriteSetWindowPosForBorderless(hWnd, &newAfter, &newX, &newY, &newCx, &newCy, &newFlags);

    ret = g_fwhb_real_SetWindowPos(hWnd, newAfter, newX, newY, newCx, newCy, newFlags);

    if (ret && hWnd == g_fwhb_targetHwnd)
        FWHB_ApplyBorderless(hWnd, "SetWindowPos after");

    return ret;
}

static BOOL WINAPI FWHB_Hook_ShowWindow(HWND hWnd, int nCmdShow)
{
    BOOL ret;

    if (g_fwhb_debug && hWnd == g_fwhb_targetHwnd)
    {
        Log(
            "FWHB_Hook_ShowWindow: target hwnd=0x%08X cmd=%d",
            (unsigned int)(ULONG_PTR)hWnd,
            nCmdShow
        );
    }

    ret = g_fwhb_real_ShowWindow(hWnd, nCmdShow);

    if (hWnd == g_fwhb_targetHwnd)
        FWHB_ApplyBorderless(hWnd, "ShowWindow after");

    return ret;
}

static LONG WINAPI FWHB_Hook_SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong)
{
    LONG ret;
    LONG newValue;

    newValue = dwNewLong;

    if (FWHB_IsActiveTargetWindow(hWnd, "SetWindowLongA") &&
        (nIndex == GWL_STYLE || nIndex == GWL_EXSTYLE))
    {
        newValue = FWHB_SanitizeStyleValue(nIndex, dwNewLong);
    }

    if (newValue != dwNewLong)
    {
        Log(
            "FWHB_Hook_SetWindowLongA: sanitized hwnd=0x%08X index=%d old=0x%08X new=0x%08X",
            (unsigned int)(ULONG_PTR)hWnd,
            nIndex,
            (unsigned int)dwNewLong,
            (unsigned int)newValue
        );
    }

    ret = g_fwhb_real_SetWindowLongA(hWnd, nIndex, newValue);

    if (hWnd == g_fwhb_targetHwnd)
        FWHB_ApplyBorderless(hWnd, "SetWindowLongA after");

    return ret;
}

static HMODULE WINAPI FWHB_Hook_LoadLibraryA(LPCSTR lpLibFileName)
{
    HMODULE hMod;

    if (g_fwhb_debug || FWHB_IsEqMainModuleNameA(lpLibFileName))
    {
        Log(
            "FWHB_Hook_LoadLibraryA: name='%s'",
            lpLibFileName ? lpLibFileName : "(null)"
        );
    }

    hMod = g_fwhb_real_LoadLibraryA(lpLibFileName);

    FWHB_TryPatchEqMainDll("LoadLibraryA after", lpLibFileName, hMod);

    return hMod;
}

static HMODULE WINAPI FWHB_Hook_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE hMod;

    if (g_fwhb_debug || FWHB_IsEqMainModuleNameA(lpLibFileName))
    {
        Log(
            "FWHB_Hook_LoadLibraryExA: name='%s' flags=0x%08X",
            lpLibFileName ? lpLibFileName : "(null)",
            (unsigned int)dwFlags
        );
    }

    hMod = g_fwhb_real_LoadLibraryExA(lpLibFileName, hFile, dwFlags);

    FWHB_TryPatchEqMainDll("LoadLibraryExA after", lpLibFileName, hMod);

    return hMod;
}

static UINT WINAPI FWHB_Hook_LoadModuleA(LPCSTR lpModuleName, LPVOID lpParameterBlock)
{
    UINT ret;

    if (g_fwhb_debug || FWHB_IsEqMainModuleNameA(lpModuleName))
    {
        Log(
            "FWHB_Hook_LoadModuleA: name='%s'",
            lpModuleName ? lpModuleName : "(null)"
        );
    }

    ret = g_fwhb_real_LoadModuleA(lpModuleName, lpParameterBlock);

    FWHB_TryPatchEqMainDll("LoadModuleA after", lpModuleName, NULL);

    return ret;
}


/* ------------------------------------------------------------------------- */
/* Patch modules                                                             */
/* ------------------------------------------------------------------------- */

typedef unsigned long FWHB_UINTPTR;

static BOOL FWHB_PatchImportIfPresent(HMODULE hMod, const char* moduleName, const char* funcName, void* hookFn, void** realFn)
{
    FWHB_UINTPTR* pIAT;
    FWHB_UINTPTR current;
    FWHB_UINTPTR addr;

    if (hMod == NULL)
        return FALSE;

    pIAT = (FWHB_UINTPTR*)FindIATPointer(hMod, funcName);

    if (pIAT == NULL)
    {
        if (g_fwhb_debug)
            Log("FWHB_PatchImportIfPresent: %s %s not imported", moduleName, funcName);

        return FALSE;
    }

    current = *pIAT;

    if (current == (FWHB_UINTPTR)hookFn)
    {
        if (g_fwhb_debug)
            Log("FWHB_PatchImportIfPresent: %s %s already patched", moduleName, funcName);

        return TRUE;
    }

    if (realFn != NULL && *realFn == NULL)
        *realFn = (void*)current;

    addr = (FWHB_UINTPTR)hookFn;
    Patch((void*)pIAT, &addr, 4);

    Log(
        "FWHB_PatchImportIfPresent: %s %s IAT=0x%08X old=0x%08X new=0x%08X",
        moduleName,
        funcName,
        (unsigned int)(ULONG_PTR)pIAT,
        (unsigned int)current,
        (unsigned int)addr
    );

    return TRUE;
}

static void FWHB_PatchWindowedBorderlessForModule(HMODULE hMod, const char* moduleName)
{
    if (hMod == NULL)
        return;

    if (g_fwhb_inPatchModule)
        return;

    g_fwhb_inPatchModule = TRUE;

#if FWHB_PATCH_CREATEWINDOWEXA
    FWHB_PatchImportIfPresent(hMod, moduleName, "CreateWindowExA", (void*)FWHB_Hook_CreateWindowExA, (void**)&g_fwhb_real_CreateWindowExA);
#endif

#if FWHB_PATCH_SETWINDOWPOS
    FWHB_PatchImportIfPresent(hMod, moduleName, "SetWindowPos", (void*)FWHB_Hook_SetWindowPos, (void**)&g_fwhb_real_SetWindowPos);
#endif

#if FWHB_PATCH_SHOWWINDOW
    FWHB_PatchImportIfPresent(hMod, moduleName, "ShowWindow", (void*)FWHB_Hook_ShowWindow, (void**)&g_fwhb_real_ShowWindow);
#endif

#if FWHB_PATCH_SETWINDOWLONGA
    FWHB_PatchImportIfPresent(hMod, moduleName, "SetWindowLongA", (void*)FWHB_Hook_SetWindowLongA, (void**)&g_fwhb_real_SetWindowLongA);
#endif

#if FWHB_PATCH_LOADLIBRARYA
    FWHB_PatchImportIfPresent(hMod, moduleName, "LoadLibraryA", (void*)FWHB_Hook_LoadLibraryA, (void**)&g_fwhb_real_LoadLibraryA);
#endif

#if FWHB_PATCH_LOADLIBRARYEXA
    FWHB_PatchImportIfPresent(hMod, moduleName, "LoadLibraryExA", (void*)FWHB_Hook_LoadLibraryExA, (void**)&g_fwhb_real_LoadLibraryExA);
#endif

#if FWHB_PATCH_LOADMODULEA
    FWHB_PatchImportIfPresent(hMod, moduleName, "LoadModuleA", (void*)FWHB_Hook_LoadModuleA, (void**)&g_fwhb_real_LoadModuleA);
#endif

    g_fwhb_inPatchModule = FALSE;
}

static void FWHB_TryPatchEqMainDll(const char* where, LPCSTR requestedName, HMODULE hMaybe)
{
    HMODULE hEqMain;

    if (!g_fwhb_enabled)
        return;

    hEqMain = NULL;

    if (hMaybe != NULL && FWHB_IsEqMainModuleHandle(hMaybe))
        hEqMain = hMaybe;

    if (hEqMain == NULL && FWHB_IsEqMainModuleNameA(requestedName))
        hEqMain = GetModuleHandleA(FWHB_EQMAIN_DLL_NAME);

    if (hEqMain == NULL)
        return;

    Log(
        "FWHB_TryPatchEqMainDll: %s patching %s hMod=0x%08X",
        where,
        FWHB_EQMAIN_DLL_NAME,
        (unsigned int)(ULONG_PTR)hEqMain
    );

    /*
        Do this every time eqmain.dll loads. The DLL can unload/reload.
        FWHB_PatchImportIfPresent is idempotent for still-loaded modules.
    */
    FWHB_PatchWindowedBorderlessForModule(hEqMain, FWHB_EQMAIN_DLL_NAME);
}


/* ------------------------------------------------------------------------- */
/* Loader                                                                    */
/* ------------------------------------------------------------------------- */

void LoadBorderlessWindowHack()
{
    bool enable = false;

#ifdef INI_FILE
    char buf[2048];
    const char *desc = "This hack makes EQ borderless windowed when the startup window is desktop-sized.  The game should be in windowed mode (WindowedMode=TRUE) and the resolution (WindowedWidth/WindowedHeight) should be set equal to the desktop resolution to trigger this hack.";
    WritePrivateProfileStringA("BorderlessWindow", "Description", desc, INI_FILE);

    GetINIString("BorderlessWindow", "Enabled", "TRUE", buf, sizeof(buf), true);
    enable = ParseINIBool(buf);

    GetINIString("BorderlessWindow", "Debug", "FALSE", buf, sizeof(buf), true);
    g_fwhb_debug = ParseINIBool(buf);
#endif

    g_fwhb_enabled = enable ? TRUE : FALSE;

    Log("LoadBorderlessWindowHack(): hack is %s", enable ? "ENABLED" : "DISABLED");

    if (enable)
    {
        Log(
            "LoadBorderlessWindowHack(): targetClass='%s' eqmain='%s' StartupMatchSlop=%d Debug=%u",
            FWHB_EQ_WINDOW_CLASS_NAME,
            FWHB_EQMAIN_DLL_NAME,
            FWHB_STARTUP_MATCH_SLOP,
            (unsigned int)g_fwhb_debug
        );

        /*
            This runs before eqgame.exe entry point. Patch eqgame.exe now.
            eqmain.dll will be patched whenever it is loaded.
        */
        FWHB_PatchWindowedBorderlessForModule(GetModuleHandle(NULL), "eqgame.exe");

        if (hEQGfxDll)
            FWHB_PatchWindowedBorderlessForModule((HMODULE)hEQGfxDll, "hEQGfxDll");

        /*
            If some loader path already brought eqmain.dll in before this point,
            patch it too. Usually it will not exist yet.
        */
        //FWHB_TryPatchEqMainDll("initial check", FWHB_EQMAIN_DLL_NAME, GetModuleHandleA(FWHB_EQMAIN_DLL_NAME));
    }
}

#endif
