#include "eqclientmod.h"

#include "util.h"
#include "settings.h"

#ifdef INI_FILE
void GetINIString(LPCSTR appName, LPCSTR keyName, LPCSTR defaultStr, LPSTR result, DWORD result_size, bool writeDefault)
{
	DWORD ret = GetPrivateProfileStringA(appName, keyName, "*", result, result_size, INI_FILE);
	if (result[0] == '*')
	{
		WritePrivateProfileStringA(appName, keyName, defaultStr, INI_FILE);
		strncpy(result, defaultStr, result_size);
	}
}

bool ParseINIBool(const char *str)
{
	if (str && (*str == 'T' || *str == 't' || *str == '1'))
		return true;
	return false;
}
#endif

