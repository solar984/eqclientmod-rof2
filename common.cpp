// the command handler here is wedged in before the normal command handler.
// it doesn't do partial matches like the usual commands

#include "eqclientmod.h"
#include "common.h"
#include "util.h"
#include "settings.h"
#include "eq_rof2.h"
#include <string.h>

//#define DETOUR_SLEEP

// globals
#ifdef COMMAND_HANDLER
std::map<const char *, _slashCommandHandler> ChatCommandMap;
#endif

#ifdef COMMAND_HANDLER
#include <map>

void command_eqclientmod(void *LocalPlayer, char *text, char *cmd, char *&sep)
{
	char buf[200];
	sprintf(buf, "eqclientmod %s solar@heliacal.net", BUILD_VERSION);
	// TODO
	//EverQuestObject->dsp_chat(buf, 269, 1);
}

//#include <WerApi.h>
void command_crash(void *LocalPlayer, char *text, char *cmd, char *&sep)
{
	//WerSetFlags(WER_FAULT_REPORTING_ALWAYS_SHOW_UI);
	//char *crash = 0;
	//*crash = 0x90;
	//RaiseException(0x0000DEAD, 0, 0, 0);
	ZeroMemory(NULL, 100);
	//int a = 0;
	//int b = 100 / a;
}

// this __fastcall prototype is equivalent to __thiscall, the second argument just isn't meaningful
// int __fastcall CEverQuest__InterpretCmd_Detour_ExecuteCmd(void *this_ptr, void *edx, void *localplayer, char *text)
typedef int(__thiscall *_CEverQuest__InterpretCmd)(void *this_ptr, void *localplayer, char *text);
_CEverQuest__InterpretCmd CEverQuest__InterpretCmd;
class CEverQuest__InterpretCmd_Detour_type
{
public:
	int CEverQuest__InterpretCmd_Detour(void *LocalPlayer, char *text)
	{
		bool handled = false;
		if (LocalPlayer && text && *text == '/')
		{
			char buf2[201];
			strncpy(buf2, text, 200);
			buf2[200] = 0;

			char *sep = buf2;
			char *cmd = strtok_s(sep, " ", &sep);

			//for (auto item : ChatCommandMap)
			for (std::map<const char *, _slashCommandHandler>::iterator item = ChatCommandMap.begin(); item != ChatCommandMap.end(); ++item)
			{
				if (item->first && !strcmp(cmd, item->first))
				{
					handled = true;
					item->second(LocalPlayer, text, cmd, sep);
				}
			}
		}

		if (handled)
		{
			return CEverQuest__InterpretCmd(this, NULL, NULL);
		}

		return CEverQuest__InterpretCmd(this, LocalPlayer, text);
	}
};
#endif

#ifdef DETOUR_SLEEP
#include <intrin.h>

typedef void (__stdcall *_Sleep)(DWORD );
_Sleep Sleep_Trampoline;
void __stdcall Sleep_Detour(DWORD ms)
{
	void* returnAddress = _ReturnAddress();
	if(ms)
	{
		if((int)returnAddress != 0x005EBE61)
		{
			Log("Sleep(%d) 0x%08X thread %d", ms, returnAddress, GetCurrentThreadId());
		}
	}
	Sleep(ms);
}
#endif

void LoadCommon()
{
	Log("LoadCommon()");

	hEQGfxDll = LoadLibrary("EQGraphicsDX9.DLL");
	if (!hEQGfxDll)
	{
		Log("LoadCommon(): NULL result from LoadLibrary(\"EQGraphicsDX9.DLL\")");
		return;
	}
	
	//SetProcessAffinityMask(GetCurrentProcess(), 1);
	//SetThreadAffinityMask(GetCurrentThread(), 1);

	// detour sleep
#ifdef DETOUR_SLEEP
	{
		intptr_t addr = (intptr_t)Sleep_Detour;
		Sleep_Trampoline = (_Sleep)(*(int *)0x0063F120);
		Patch((void *)0x0063F120, &addr, 4);
	}
#endif

#ifdef COMMAND_HANDLER
	bool enableCommandHandler = true;

#ifdef INI_FILE
	char buf[2048];
	const char *desc = "This mod adds the extra command handling that some of the other hacks use but it's not strictly necessary to enable this to use the other hacks.  Basic commands included: /eqclientmod /crash";
	WritePrivateProfileStringA("CommandHandler", "Description", desc, INI_FILE);
	GetINIString("CommandHandler", "Enabled", "TRUE", buf, sizeof(buf), true);
	enableCommandHandler = ParseINIBool(buf);
#endif

	Log("LoadCommon(): CommandHandler hack is %s thread %d", enableCommandHandler ? "ENABLED" : "DISABLED", GetCurrentThreadId());

	if (enableCommandHandler)
	{
		//MethodAddressToVariable(NP_CEverQuest__InterpretCmd_Detour, CEverQuest__InterpretCmd_Detour_type::CEverQuest__InterpretCmd_Detour);
		intptr_t  NP_CEverQuest__InterpretCmd_Detour; { int(__thiscall CEverQuest__InterpretCmd_Detour_type::* fp)(void *, char *) = &CEverQuest__InterpretCmd_Detour_type::CEverQuest__InterpretCmd_Detour; memcpy(&NP_CEverQuest__InterpretCmd_Detour, &fp, 4); }

		// CChatWindow::WndNotification - this one is for when text is typed into a chat window's input control
		// .text:004F7390 2518 E8 E3 17 FA FF                               call    CEverQuest__InterpretCmd
		// .text:00656C99 2AC8 E8 42 90 EC FF                         call    CEverQuest__InterpretCmd
		intptr_t addr = (intptr_t)NP_CEverQuest__InterpretCmd_Detour - (intptr_t)CalcOffset(0x00656C99) - 5;
		Patch((void *)CalcOffset(0x00656C99 + 1), &addr, 4);

		// EQ_Character::DoPassageOfTime
		// .text:00424015 388 E8 5E 4B 07 00                                call    CEverQuest__InterpretCmd
		// .text:0045FBE4 340 E8 F7 00 0C 00                          call    CEverQuest__InterpretCmd
		addr = (intptr_t)NP_CEverQuest__InterpretCmd_Detour - (intptr_t)CalcOffset(0x0045FBE4) - 5;
		Patch((void *)CalcOffset(0x0045FBE4 + 1), &addr, 4);

		CEverQuest__InterpretCmd = (_CEverQuest__InterpretCmd)CalcOffset(Offset_CEverQuest__InterpretCmd);

		ChatCommandMap["/eqclientmod"] = command_eqclientmod;
		ChatCommandMap["/crash"] = command_crash;
	}
#endif
}
