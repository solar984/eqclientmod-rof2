#include "common.h"
#include "util.h"

#define PERSONALITY_DLL "dinput8.dll"

#define WRAPPER_GENFUNC(name) FARPROC orig_##name; extern "C" __declspec(naked,dllexport) void name##() { __asm jmp[orig_##name] }
WRAPPER_GENFUNC(DirectInput8Create)
WRAPPER_GENFUNC(DllCanUnloadNow)
WRAPPER_GENFUNC(DllGetClassObject)
WRAPPER_GENFUNC(DllRegisterServer)
WRAPPER_GENFUNC(DllUnregisterServer)
WRAPPER_GENFUNC(GetdfDIJoystick)

#define WRAPPER_FUNC(name) orig_##name = GetProcAddress(hOriginalDll, ###name)
bool DllPersonalityInit()
{
	HMODULE hOriginalDll = LoadLibrary("C:\\Windows\\system32\\dinput8.dll"); // this gets redirected to C:\windows\syswow64 on win64
	if (!hOriginalDll)
	{
		Log("DllInit(): NULL result from LoadLibrary(\"C:\\Windows\\system32\\dinput8.dll\")");
		return false;
	}
	WRAPPER_FUNC(DirectInput8Create);
	WRAPPER_FUNC(DllCanUnloadNow);
	WRAPPER_FUNC(DllGetClassObject);
	WRAPPER_FUNC(DllRegisterServer);
	WRAPPER_FUNC(DllUnregisterServer);
	WRAPPER_FUNC(GetdfDIJoystick);

	return true;
}

