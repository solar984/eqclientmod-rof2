#include "../eqclientmod.h"
// This hack removes the requirement to pass the 'patchme' argument to the program to start it.

#ifdef PROGRAM_LAUNCH_HACK
#include "../common.h"
#include "../util.h"
#include "../settings.h"

void LoadProgramLaunchHack()
{
	bool enable = true;

#ifdef INI_FILE
	char buf[2048];
	const char *desc = "This hack removes the need to pass the 'patchme' argument to the program to start it.";
	WritePrivateProfileStringA("ProgramLaunch", "Description", desc, INI_FILE);
	GetINIString("ProgramLaunch", "Enabled", "TRUE", buf, sizeof(buf), true);
	enable = ParseINIBool(buf);
#endif

	Log("LoadProgramLaunchHack(): hack is %s", enable ? "ENABLED" : "DISABLED");

	if(enable)
	{
		unsigned char nop[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
		unsigned char jmp = 0xEB;

		// .text:005FE48C 480 0F 8E BF 02 00 00                       jle     loc_5FE751
		Patch((void *)CalcOffset(0x005FE48C), nop, 6);

		// .text:005FE4AE 480 0F 8E 9D 02 00 00                       jle     loc_5FE751
		Patch((void *)CalcOffset(0x005FE4AE), nop, 6);

		// .text:005FE74F 480 75 1E                                   jnz     short loc_5FE76F
		Patch((void *)CalcOffset(0x005FE74F), &jmp, 1);
	}
}

#endif
