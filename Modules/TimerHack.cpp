#include "../eqclientmod.h"
// CPU high clock speed overflow fix

#ifdef TIMER_HACK
#include <stdlib.h>
#include "../common.h"
#include "../util.h"
#include "../settings.h"
#include "../eq_rof2.h"

LARGE_INTEGER g_ProcessorSpeed;
LARGE_INTEGER g_ProcessorTicks;

unsigned __int64 GetCpuTicks_Detour() 
{
	LARGE_INTEGER qpcResult;
	QueryPerformanceCounter(&qpcResult);
	return (qpcResult.QuadPart - g_ProcessorTicks.QuadPart);
}

unsigned __int64 GetCpuSpeed2_Detour() 
{
	LARGE_INTEGER Frequency;

	//OutputDebugString("GetCpuSpeed2_Detour");
	if (!QueryPerformanceFrequency(&Frequency))
	{
		MessageBoxW(0, L"This OS is not supported.", L"Error", 0);
		exit(-1);
	}

	g_ProcessorSpeed.QuadPart = Frequency.QuadPart / 1000;
	QueryPerformanceCounter(&g_ProcessorTicks);
	return g_ProcessorSpeed.QuadPart;
}

void LoadTimerHack()
{
	bool enable = true;

#ifdef INI_FILE
	char buf[2048];
	const char *desc = "CPU high clock speed overflow fix.  If you have a CPU that's more than 4.2 Ghz you probably need this to make the game run at the right speed.";
	WritePrivateProfileStringA("CPUHighSpeedFix", "Description", desc, INI_FILE);
	GetINIString("CPUHighSpeedFix", "Enabled", "TRUE", buf, sizeof(buf), true);
	enable = ParseINIBool(buf);
#endif

	Log("LoadTimerHack(): hack is %s", enable ? "ENABLED" : "DISABLED");

	if(enable)
	{
		// this is in eqgame.exe
		Detour((PBYTE)CalcOffset(Offset_rdtsc_elapsed, hEQGameEXE), (PBYTE)GetCpuTicks_Detour);

		// these two are in eqgfx_dx8.dll
		if (hEQGfxDll)
		{
			// there are two functions: GetCpuSpeed2 and GetCpuSpeed3
			// the game calls both and compares the results usually to see which worked better, so we'll just override both to go to our new function
			intptr_t cpuSpeed2 = (intptr_t)GetProcAddress(hEQGfxDll, "EQG_GetCpuSpeed2");
			if (cpuSpeed2)
			{
				Detour((PBYTE)cpuSpeed2, (PBYTE)GetCpuSpeed2_Detour);
			}
			intptr_t cpuSpeed3 = (intptr_t)GetProcAddress(hEQGfxDll, "EQG_GetCpuSpeed3");
			if (cpuSpeed3)
			{
				Detour((PBYTE)cpuSpeed3, (PBYTE)GetCpuSpeed2_Detour);
			}
		}
	}
}

#endif
