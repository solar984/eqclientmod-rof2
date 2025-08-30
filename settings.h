#pragma once

void GetINIString(LPCSTR appName, LPCSTR keyName, LPCSTR defaultStr, LPSTR result, DWORD result_size, bool writeDefault);
bool ParseINIBool(const char *str);
