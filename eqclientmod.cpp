// eqclientmod
// hacks/mods for everquest client in a planted dll
// solar@heliacal.net

#include "eqclientmod.h"
#include "Personalities/personality.h"
#include "common.h"
#include "util.h"
#include "eq_rof2.h"

//extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
//extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;
#ifdef BUILD_VERSION
extern "C" __declspec(dllexport) const char *EQCLIENTMOD_BUILD_VERSION = BUILD_VERSION;
#endif

void *Entry_Trampoline;
int tramplen = 0;
DWORD entryPoint;

HMODULE hEQGameEXE;
HMODULE hEQGfxDll;

DWORD CalcOffset(DWORD offset_in, HMODULE base)
{
	//Log("CalcOffset(0x%08X, 0x%08X) = 0x%08X", offset_in, (DWORD)base, offset_in - 0x00400000 + (DWORD)base);
	if(hEQGfxDll != NULL && base == hEQGfxDll) // RVA offsets
	{
		return offset_in + (DWORD)hEQGfxDll;
	}
	else
	{
		if(base == 0) base = hEQGameEXE;
		// this preferrred base is what our hardcoded offset values in code are based on
		// but the image may be loaded at a different address so we calculate the runtime offset
		return offset_in - 0x00400000 + (DWORD)base;
	}
}

void Payload2()
{
	Log("Payload2");

	// restore original entry point code
	Patch((void *)entryPoint, Entry_Trampoline, tramplen);

	LoadCommon();

#ifdef TIMER_HACK
	extern void LoadTimerHack();
	LoadTimerHack();
#endif
#ifdef GAMMA_HACK
	extern void LoadGammaHack();
	LoadGammaHack();
#endif
#ifdef PROGRAM_LAUNCH_HACK
	extern void LoadProgramLaunchHack();
	LoadProgramLaunchHack();
#endif
#ifdef DEV_HACK
	extern void LoadDevHack();
	LoadDevHack();
#endif
#ifdef WINDOW_TITLE_HACK
	extern void LoadWindowTitleHack();
	LoadWindowTitleHack();
#endif
#ifdef BORDERLESS_WINDOW_HACK
	extern void LoadBorderlessWindowHack();
	LoadBorderlessWindowHack();
#endif

	// continue from normal program entry point, this never returns
	((void (*)())entryPoint)();
}

bool Payload()
{
	// make sure we're loading into a process with image file name eqgame.exe
	HMODULE callerModule = GetModuleHandleA(0);
	char lpFilename[260];
	GetModuleFileNameA(callerModule, lpFilename, 260);
	if (!(lpFilename && strlen(lpFilename) >= 10 && !strncmp(lpFilename + strlen(lpFilename) - 10, "eqgame.exe", 10)))
	{
		Log("eqclientmod loaded but process file name is not eqgame.exe, skipping eqgame hacks.");
		return FALSE;
	}

	// This dll expects to be wrapping a dll that the parent module imports from and we're being loaded by the windows dynamic linker before the program starts executing.
	// By detouring the entry point, we can run anything we want at the start of the program once it's actually ready to run.  We can do that here too but we're inside DllMain().
	hEQGameEXE = callerModule;
	PIMAGE_OPTIONAL_HEADER oh = GetPEOptionalHeader(callerModule);
	if((DWORD)callerModule != 0x400000)
	{
		Log("Payload(): eqgame.exe loaded at 0x%08X (relocated)", (DWORD)hEQGameEXE, oh->ImageBase);
	}
	else
	{
		Log("Payload(): eqgame.exe loaded at 0x%08X (default ImageBase 0x%08X)", (DWORD)hEQGameEXE, 0x400000);
	}

	// check a known offset to make sure before we patch anything
	Log("Checking for string at 0x%08X", CalcOffset(Offset_WindowNameString));
	if (strncmp((const char *)CalcOffset(Offset_WindowNameString), "EverQuest", 9))
	{
		Log("This doesn't look like the process we expect, skipping eqgame hacks.");
		return FALSE;
	}

	DWORD entryPointRVA = oh->AddressOfEntryPoint;
	entryPoint = (DWORD)callerModule + entryPointRVA;
	for (; tramplen < 5; tramplen += InstructionLength((BYTE *)entryPoint + tramplen));
	Log("eqclientmod: Detouring entry point of %s at 0x%08X + 0x%08X = 0x%08X trampoline bytes = %d", lpFilename, (DWORD)callerModule, entryPointRVA, (DWORD)callerModule + entryPointRVA, tramplen);
	Entry_Trampoline = DetourWithTrampoline((void *)entryPoint, Payload2, tramplen);

	return TRUE;
}

bool DllPersonalityInit(); // the dll whose personality we take on can be changed

BOOL WINAPI DllMain(HMODULE, DWORD r, LPVOID)
{
	if (r == DLL_PROCESS_ATTACH)
	{
		if (!DllPersonalityInit()) return FALSE;

		if (Payload())
		{
			// prevent self from being unloaded
			HMODULE hThisModule;
			hThisModule = LoadLibrary(PERSONALITY_DLL);
			//GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, PERSONALITY_DLL, &hThisModule);
		}
	}

	return TRUE;
}
