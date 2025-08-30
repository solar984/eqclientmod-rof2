#pragma once
#include "eqclientmod.h"

//#define MethodAddressToVariable(v, method) intptr_t  v; { auto fp = &method; memcpy(&v, &fp, 4); }

#ifdef COMMAND_HANDLER
#include <map>
typedef void (*_slashCommandHandler)(void *LocalPlayer, char *text, char *cmd, char *&sep);
extern std::map<const char *, _slashCommandHandler> ChatCommandMap;
#endif

extern void LoadCommon();
