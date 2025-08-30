#pragma once

#include "eqclientmod.h"

// eqgame.exe - offsets relative to 0x00400000
#define Offset_WindowNameString 0x009CC9CC
#define Offset_rdtsc_elapsed 0x008097A0
#define Offset_CEverQuest__dsp_chat 0x0051F1A0
//#define Offset_CEverQuest__dsp_chat_simple 0x00498436
#define Offset_EverQuestObject 0x00E67CCC
#define Offset_CEverQuest__InterpretCmd 0x0051FCE0

// EQGraphicsDX9.DLL
#define Offset_DX9_t3dSetGammaLevel 0x000914D0

#define EQ_FUNCTION_AT_ADDRESS(function, offset) __declspec(naked) function { __asm{mov eax, offset}; __asm{jmp eax}; }

