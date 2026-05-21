#pragma once

#define WIN32_LEAN_AND_MEAN
#define WINDOWS_IGNORE_PACKING_MISMATCH
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

// no stdint.h in VS2005
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;

typedef uint8_t byte;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

extern HMODULE hEQGameEXE;
extern HMODULE hEQGfxDll;

#define INI_FILE ".\\eqclientmod.ini"

#define BUILD_VERSION "SOLAR_ROF2_2026052100"

#define COMMAND_HANDLER
#define TIMER_HACK
#define GAMMA_HACK
#define PROGRAM_LAUNCH_HACK
//#define DEV_HACK
#define WINDOW_TITLE_HACK
#define BORDERLESS_WINDOW_HACK

DWORD CalcOffset(DWORD offset_in, HMODULE base = 0);
