#include "../eqclientmod.h"
// This hack prevents the game from changing the desktop gamma

#ifdef GAMMA_HACK
#include <stdlib.h>
#include "../common.h"
#include "../util.h"
#include "../settings.h"
#include "../eq_rof2.h"

typedef int(__fastcall *_t3dSetGammaLevel)(void *thisptr, float gamma);
_t3dSetGammaLevel t3dSetGammaLevel_Trampoline;
int __fastcall t3dSetGammaLevel_Detour(void *thisptr, float gamma) 
{
	return 0;
}

void LoadGammaHack()
{
	bool enable = false;

#ifdef INI_FILE
	char buf[2048];
	const char *desc = "This hack prevents the game from changing the desktop gamma.";
	WritePrivateProfileStringA("DisableGammaChange", "Description", desc, INI_FILE);
	GetINIString("DisableGammaChange", "Enabled", "FALSE", buf, sizeof(buf), true);
	enable = ParseINIBool(buf);
#endif

	Log("LoadGammaHack(): hack is %s", enable ? "ENABLED" : "DISABLED");

	if(enable)
	{
		if(hEQGfxDll)
		{
			_t3dSetGammaLevel t3dSetGammaLevel = (_t3dSetGammaLevel)((intptr_t)hEQGfxDll + (Offset_DX9_t3dSetGammaLevel));
			t3dSetGammaLevel_Trampoline = (_t3dSetGammaLevel)DetourWithTrampoline(t3dSetGammaLevel, t3dSetGammaLevel_Detour, 6);
		}
	}
}


#endif