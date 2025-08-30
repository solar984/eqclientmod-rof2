#include "../eqclientmod.h"
#include "../common.h"
#include "../util.h"
#include "../settings.h"
#include <stdio.h>

#ifdef WINDOW_TITLE_HACK

typedef BOOL(WINAPI *_SetWindowTextA)(HWND hWnd, LPCSTR lpString);
_SetWindowTextA SetWindowTextA_Original;

BOOL WINAPI SetWindowTextA_Detour(HWND hWnd, LPCSTR lpString)
{
	static int first = 1;

	// set the title on the first call, ignore afterwards
	if (first)
	{
		first = 0;
		char title[50];
		for (int i = 1; i < 100; i++)
		{
			_snprintf(title, 50, "Client%d", i);
			if (FindWindow(NULL, title) == 0)
				break;
		}
		lpString = title;

		return SetWindowTextA_Original(hWnd, lpString);
	}

	return TRUE;
}

void LoadWindowTitleHack()
{
	bool enable = true;

#ifdef INI_FILE
	char buf[2048];
	const char *desc = "This mod sets a static window title on the application window in the format Client1, Client2, etc.  This is intended to make it easy to find the window with macro software like hotkeynet or AutoHotKey.";
	WritePrivateProfileStringA("WindowTitle", "Description", desc, INI_FILE);
	GetINIString("WindowTitle", "Enabled", "TRUE", buf, sizeof(buf), true);
	enable = ParseINIBool(buf);
#endif

	Log("LoadWindowTitleHack(): hack is %s", enable ? "ENABLED" : "DISABLED");

	if(enable)
	{
		// detour via import table
		// .idata:00000001407878C8                                   ; BOOL __stdcall SetWindowTextA(HWND hWnd, LPCSTR lpString)
		// .idata:00000001407878C8 ?? ?? ?? ?? ?? ?? ?? ??                           extrn SetWindowTextA:qword
		uintptr_t *SetWindowTextA_Ptr = (uintptr_t *)FindIATPointer(hEQGameEXE, "SetWindowTextA");
		if (SetWindowTextA_Ptr)
		{
			SetWindowTextA_Original = (_SetWindowTextA)*SetWindowTextA_Ptr;
			uintptr_t addr = (uintptr_t)SetWindowTextA_Detour;
			Patch((void *)SetWindowTextA_Ptr, &addr, 8);
		}
	}
}

#endif
