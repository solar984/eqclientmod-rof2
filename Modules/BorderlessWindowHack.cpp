#include "../eqclientmod.h"

#ifdef BORDERLESS_WINDOW_HACK
#include <stdlib.h>
#include "../common.h"
#include "../util.h"
#include "../settings.h"

static void InstallHooksForModule(HMODULE hMod, const char* moduleName);

//#define BWH_DEBUG

// read eqclient.ini and see if it's configured in windowed mode with the size equal to the desktop size
MONITORINFO monitorInfo;
bool IsGameConfigurationEligibleForBorderlessMode()
{
    char buf[2048];
	GetPrivateProfileStringA("Defaults", "WindowedMode", "*", buf, sizeof(buf), ".\\eqclient.ini");
	if(!ParseINIBool(buf))
		return false;

	UINT WindowedWidth = GetPrivateProfileIntA("VideoMode", "WindowedWidth", 0, ".\\eqclient.ini");
	UINT WindowedHeight = GetPrivateProfileIntA("VideoMode", "WindowedHeight", 0, ".\\eqclient.ini");

	POINT pt = { 0, 0 };
	HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&monitorInfo, sizeof(MONITORINFO));
    monitorInfo.cbSize = sizeof(MONITORINFO);
	if(GetMonitorInfo(mon, &monitorInfo))
	{
#ifdef BWH_DEBUG
		Log("IsGameConfigurationEligibleForBorderlessMode: desktop %dx%d EQ window %dx%d", 
			monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
			WindowedWidth, WindowedHeight
		);
#endif

		if(WindowedWidth == monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left && WindowedHeight == monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top)
			return true;
	}

	return false;
}

bool IsEQWindowClass(LPCSTR lpClassName)
{
	if(lpClassName && !lstrcmpiA(lpClassName, "_EverQuestwndclass"))
		return true;
	return false;
}

bool IsEQWindow(HWND hwnd)
{
	char className[256];
	int len = GetClassNameA(hwnd, className, sizeof(className));
	if(len > 0 && IsEQWindowClass(className))
	{
		return true;
	}
	return false;
}

static LONG SanitizeStyleValue(int nIndex, LONG value)
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

// we do this instead of SetWindowPos() so we can keep our window how we want it and avoid the flicker when entering world
static void ApplyBorderlessStyle(HWND hwnd)
{
    LONG style = SanitizeStyleValue(GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE));
    LONG exstyle = SanitizeStyleValue(GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE));

    SetWindowLongA(hwnd, GWL_STYLE, style);
    SetWindowLongA(hwnd, GWL_EXSTYLE, exstyle);

    SetWindowPos(
        hwnd,
        HWND_TOP,
        monitorInfo.rcMonitor.left,
        monitorInfo.rcMonitor.top,
        monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
        monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER
    );
}

//
// CreateWindowExA - hooks the initial creation of the game window in eqgame.exe
//
typedef HWND (WINAPI *_CreateWindowExA)(
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
static _CreateWindowExA CreateWindowExA_Original;
static HWND WINAPI CreateWindowExA_Detour(
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
#ifdef BWH_DEBUG
    Log(
        "CreateWindowExA_Detour: class='%s' title='%s' style=0x%08X ex=0x%08X x=%d y=%d w=%d h=%d parent=0x%08X",
		lpClassName ? lpClassName : "(null)",
        lpWindowName ? lpWindowName : "(null)",
        (unsigned int)dwStyle,
        (unsigned int)dwExStyle,
        X,
        Y,
        nWidth,
        nHeight,
        (unsigned int)(ULONG_PTR)hWndParent
    );
#endif

	if(IsEQWindowClass(lpClassName))
	{
		// rewrite window creation parameters for fullscreen borderless window mode
	    dwStyle = SanitizeStyleValue(GWL_STYLE, dwStyle);
	    dwExStyle = SanitizeStyleValue(GWL_EXSTYLE, dwExStyle);

		X = monitorInfo.rcMonitor.left;
		Y = monitorInfo.rcMonitor.top;
		nWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
		nHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
	}

    HWND hwnd = CreateWindowExA_Original(
        dwExStyle,
        lpClassName,
        lpWindowName,
        dwStyle,
        X,
        Y,
        nWidth,
        nHeight,
        hWndParent,
        hMenu,
        hInstance,
        lpParam
    );

    return hwnd;
}

//
// SetWindowPos - this is the core of the mod, making sure the window stays fullscreen borderless window, hooked in eqgame.exe and eqmain.dll
//
typedef BOOL (WINAPI *_SetWindowPos)(
    HWND hWnd,
    HWND hWndInsertAfter,
    int X,
    int Y,
    int cx,
    int cy,
    UINT uFlags
);
static _SetWindowPos SetWindowPos_Original;
static BOOL WINAPI SetWindowPos_Detour(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
    BOOL ret;

#ifdef BWH_DEBUG
	Log("SetWindowPos_Detour: hwnd=0x%08X after=0x%08X X=%d Y=%d cx=%d cy=%d uFlags=0x%08X", 
		(unsigned int)hWnd, (unsigned int)hWndInsertAfter, X, Y, cx, cy, uFlags
	);
#endif

	if(IsEQWindow(hWnd))
	{
		ApplyBorderlessStyle(hWnd);
	}
	else
	{
		ret = SetWindowPos_Original(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
	}

    return ret;
}

//
// LoadLibraryA - this is so we can hook the SetWindowPos import when eqmain.dll is loaded
//
typedef HMODULE (WINAPI *_LoadLibraryA)(LPCSTR lpLibFileName);
static _LoadLibraryA LoadLibraryA_Original;
static HMODULE WINAPI LoadLibraryA_Detour(LPCSTR lpLibFileName)
{
    HMODULE hMod;

#ifdef BWH_DEBUG
    Log("LoadLibraryA_Detour: name='%s'", lpLibFileName ? lpLibFileName : "(null)");
#endif

    hMod = LoadLibraryA_Original(lpLibFileName);

	// we need to hook eqmain.dll's imports when the front end is loaded
	if (lpLibFileName && strlen(lpLibFileName) >= 10 && !strncmp(lpLibFileName + strlen(lpLibFileName) - 10, "eqmain.dll", 10))
	{
		InstallHooksForModule(hMod, "eqmain.dll");
	}

    return hMod;
}

static BOOL HookImport(HMODULE hMod, const char* moduleName, const char* funcName, void* hookFn, void** realFn)
{
    uintptr_t *pIAT;
    uintptr_t current;
    uintptr_t addr;

    if (hMod == NULL)
        return FALSE;

    pIAT = (uintptr_t *)FindIATPointer(hMod, funcName);

    if (pIAT == NULL)
    {
#ifdef BWH_DEBUG
		Log("HookImport: %s %s not imported", moduleName, funcName);
#endif
        return FALSE;
    }

    current = *pIAT;

    if (current == (uintptr_t)hookFn)
    {
#ifdef BWH_DEBUG
        Log("HookImport: %s %s already patched", moduleName, funcName);
#endif
        return TRUE;
    }

	// save the original value in realFn if it's not already set
    if (realFn != NULL && *realFn == NULL)
        *realFn = (void *)current;

    addr = (uintptr_t)hookFn;
    Patch((void *)pIAT, &addr, 4);

#ifdef BWH_DEBUG
    Log(
        "HookImport: %s %s IAT=0x%08X old=0x%08X new=0x%08X",
        moduleName,
        funcName,
        (unsigned int)pIAT,
        (unsigned int)current,
        (unsigned int)addr
    );
#endif

    return TRUE;
}

static void InstallHooksForModule(HMODULE hMod, const char* moduleName)
{
    if (hMod == NULL || moduleName == NULL)
        return;

	if (moduleName && !lstrcmpiA(moduleName, "eqgame.exe"))
	{
		HookImport(hMod, moduleName, "CreateWindowExA", (void*)CreateWindowExA_Detour, (void**)&CreateWindowExA_Original);
		HookImport(hMod, moduleName, "SetWindowPos", (void*)SetWindowPos_Detour, (void**)&SetWindowPos_Original);
		HookImport(hMod, moduleName, "LoadLibraryA", (void*)LoadLibraryA_Detour, (void**)&LoadLibraryA_Original);
	}
	else if (moduleName && !stricmp(moduleName, "eqmain.dll"))
	{
		HookImport(hMod, moduleName, "SetWindowPos", (void*)SetWindowPos_Detour, (void**)&SetWindowPos_Original);
	}
}

void LoadBorderlessWindowHack()
{
    bool enable = false;

#ifdef INI_FILE
    char buf[2048];
    const char *desc = "This hack makes EQ borderless windowed when the startup window is desktop-sized.  The game should be in windowed mode (WindowedMode=TRUE) and the resolution (WindowedWidth/WindowedHeight) should be set equal to the desktop resolution to trigger this hack.";
    WritePrivateProfileStringA("BorderlessWindow", "Description", desc, INI_FILE);

    GetINIString("BorderlessWindow", "Enabled", "TRUE", buf, sizeof(buf), true);
    enable = ParseINIBool(buf);
#endif

    Log("LoadBorderlessWindowHack(): hack is %s", enable ? "ENABLED" : "DISABLED");

    if (enable)
    {
		if(IsGameConfigurationEligibleForBorderlessMode())
		{
			if (hEQGameEXE)
				InstallHooksForModule(hEQGameEXE, "eqgame.exe");

			// we need to patch eqmain.dll as well but that one gets loaded/unloaded so we'll do it at load time in the LoadLibraryA hook
		}
		else
		{
			Log("LoadBorderlessWindowHack(): game configuration not eligible for borderless windowed mode.  The game should be in windowed mode (WindowedMode=TRUE) and the resolution (WindowedWidth/WindowedHeight) should be set equal to the desktop resolution to trigger this hack.");
		}
    }
}

#endif
