#include "../eqclientmod.h"
// this hack is not intended to be used for general play, it is for inspecting things in the client state during gameplay to aid in development

#ifdef DEV_HACK
#include <stdlib.h>
#include "../common.h"
#include "../util.h"
#include "../settings.h"
#include "../eq_titanium.h"

void command_consumption(void *LocalPlayer, char *text, char *cmd, char *&sep)
{
	int (__thiscall *ProfileManager__GetCurrentProfile)(int this_ptr) = (int (__thiscall *)(int))0x005EC220;

	int LocalPC = *(int *)Offset_LocalPC;
	int ProfileManager = *(int *)(*(int *)(LocalPC + 8) + 4) + LocalPC + 12;
	
	int pp = ProfileManager__GetCurrentProfile(ProfileManager);
	short hunger = *(short *)(pp + 0x121C);
	short thirst = *(short *)(pp + 0x1218);
	
	char buf[200];
	sprintf(buf, "hunger = %d thirst = %d", hunger, thirst);
	EverQuestObject->dsp_chat(buf, 269, 1);
}

// 00420F51
// int __thiscall EQ_Character::ProcessHungerandThirst(EQ_PC *this, int a2)

//class Dev;
//typedef int (__thiscall *_ProcessHungerandThirst)(Dev *, int);



void LoadDevHack()
{
	bool enable = false;

#ifdef INI_FILE
	char buf[2048];
	const char *desc = "This hack is not intended to be used for general play, it is for inspecting things in the client state during gameplay to aid in development.";
	WritePrivateProfileStringA("Dev", "Description", desc, INI_FILE);
	GetINIString("Dev", "Enabled", "FALSE", buf, sizeof(buf), true);
	enable = ParseINIBool(buf);
#endif

	Log("LoadDevHack(): hack is %s", enable ? "ENABLED" : "DISABLED");

	if(enable)
	{
#ifdef COMMAND_HANDLER
		ChatCommandMap["/consumption"] = command_consumption;
#endif
	}
}

#endif

