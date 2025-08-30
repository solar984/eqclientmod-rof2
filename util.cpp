// util.cpp

#include "eqclientmod.h"
#include <stdio.h>
#include <cstdarg>
#include <string>

// string functions using a static buffer
static char text[2048];
char *StringFormat(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf_s(text, sizeof(text), _TRUNCATE, format, ap);
	va_end(ap);

	return text;
}

void Log(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vsnprintf_s(text, sizeof(text), _TRUNCATE, format, ap);
	va_end(ap);

	OutputDebugStringA(text);
}
void Log(const char *msg)
{
	return Log("%s", msg);
}

void Patch(void *dst, const void *src, size_t len)
{
	DWORD oldProtect;
	VirtualProtect(dst, len, PAGE_EXECUTE_READWRITE, &oldProtect);
	memcpy(dst, src, len);
	VirtualProtect(dst, len, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), dst, len);
}

bool Detour(void *address, void *dst)
{
	DWORD oldProtect;
	int len = 5; // 5 bytes for E9 + address

	// make memory writeable
	VirtualProtect(address, len, PAGE_EXECUTE_READWRITE, &oldProtect);

	// calculate relative jump target address
	intptr_t relativeAddress = ((intptr_t)dst - (intptr_t)address) - len;

	// write jmp instruction
	*(unsigned char *)address = 0xE9;	// jmp
	*(intptr_t *)((intptr_t)address + 1) = relativeAddress; // target

	// restore memory protection
	VirtualProtect(address, len, oldProtect, &oldProtect);
	FlushInstructionCache(GetCurrentProcess(), NULL, NULL);

	return true;
}

void *MakeTrampoline(void *target, int len)
{
	// allocate memory for trampoline function (len + 5 for the return jmp)
	void *trampoline = VirtualAlloc(0, len + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!trampoline) return NULL;

	// copy len bytes from the target into the trampoline
	// for Detour() it only needs 5 bytes saved but the entire function could be copied if needed
	memcpy(trampoline, target, len);

	// calculate relative jump target address
	intptr_t trampRelativeAddress = (intptr_t)target - (intptr_t)trampoline - 5;

	// write jmp instruction to the end of the trampoline
	*(unsigned char *)((intptr_t)trampoline + len) = 0xE9;
	*(intptr_t *)((intptr_t)trampoline + len + 1) = trampRelativeAddress;

	return trampoline;
}

void *DetourWithTrampoline(void *address, void *dst, int len)
{
	// Detour will overwrite 5 bytes at address, save it in a trampoline first
	void *trampoline = MakeTrampoline(address, len);
	Detour(address, dst);

	return trampoline;
}

PIMAGE_OPTIONAL_HEADER GetPEOptionalHeader(HMODULE module)
{
	uintptr_t baseAddr = (uintptr_t)module;

	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)baseAddr;
	if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE)
	{
		PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)(dosHeader->e_lfanew + baseAddr);
		if (ntHeader->Signature == IMAGE_NT_SIGNATURE)
		{
			PIMAGE_OPTIONAL_HEADER oh = &ntHeader->OptionalHeader;
			if (oh->Magic == 0x020b /*64*/ || oh->Magic == 0x010b /*32*/)
			{
				return oh;
			}
		}
	}

	return NULL;
}

void *FindIATPointer(HMODULE module, const char *functionName)
{
	uintptr_t baseAddr = (uintptr_t)module;
	void *ret = NULL;

	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)baseAddr;
	if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE)
	{
		PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)(dosHeader->e_lfanew + baseAddr);
		if (ntHeader->Signature == IMAGE_NT_SIGNATURE)
		{
			PIMAGE_OPTIONAL_HEADER oh = &ntHeader->OptionalHeader;
			if (oh->Magic == 0x020b /*64*/ || oh->Magic == 0x010b /*32*/)
			{
				// import table should be second entry
				PIMAGE_DATA_DIRECTORY importDataDirectory = &oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
				PIMAGE_IMPORT_DESCRIPTOR idt = (PIMAGE_IMPORT_DESCRIPTOR)(importDataDirectory->VirtualAddress + baseAddr);

				// loop through each imported dll
				for (DWORD i = 0; i < importDataDirectory->Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); i++)
				{
					if (idt[i].Characteristics && idt[i].Name)
					{
						//Log("IAT %i %s\n", i, (char *)(idt[i].Name + baseAddr)); // dll name

						PIMAGE_THUNK_DATA ilt = (PIMAGE_THUNK_DATA)(idt[i].OriginalFirstThunk + baseAddr);
						PIMAGE_THUNK_DATA iat = (PIMAGE_THUNK_DATA)(idt[i].FirstThunk + baseAddr);

						// loop through each imported function
						for (int j = 0; ilt[j].u1.AddressOfData; j++)
						{
							if (!(ilt[j].u1.Ordinal & IMAGE_ORDINAL_FLAG)) // skip ordinal imports
							{
								PIMAGE_IMPORT_BY_NAME import = (PIMAGE_IMPORT_BY_NAME)(ilt[j].u1.AddressOfData + baseAddr);
								if (import->Name)
								{
									//Log("    %s %p %p\n", import->Name, iat[j].u1.Function, &iat[j].u1.Function);
									if (!strcmp((char *)import->Name, functionName))
									{
										ret = &iat[j].u1.Function;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return ret;
}

char *ltrim(char *s)
{
	while (isspace(*s)) s++;
	return s;
}

char *rtrim(char *s)
{
	char *back = s + strlen(s);
	while (isspace(*--back));
	*(back + 1) = '\0';
	return s;
}

char *trim(char *s)
{
	return rtrim(ltrim(s));
}

int LoadPatchFile(const char *filename)
{
	char tmp[200], line[200];
	char targetFilename[260];
	int success = 1;

	// open patch file
	FILE *fp = fopen(filename, "r");
	if (!fp)
	{
		Log("LoadPatchFile: Error opening '%s': %s", filename, strerror(errno));
		return 0;
	}
	Log("LoadPatchFile: Processing patch file '%s'", filename);

	HMODULE target = NULL;
	int line_number = 0;

	while (fgets(tmp, 200, fp))
	{
		line_number++;
		tmp[199] = 0;
		strncpy(line, trim(tmp), 200);
		line[199] = 0;
		
		// skip empty lines
		if (strlen(line) == 0) continue;

		// skip comments
		if (line[0] == '#') continue;
		if (strlen(line) > 1 && line[0] == '/' && line[1] == '/') continue;

		if (!target) // expect to find a >target.dll line
		{
			if (line[0] == '>')
			{
				// load target library
				strcpy(targetFilename, trim(line + 1));
				Log("LoadPatchFile: Patching '%s'", targetFilename);
				target = LoadLibraryA(targetFilename);
				if (!target)
				{
					Log("LoadPatchFile: Error loading '%s'", targetFilename);
					success = 0;
					break;
				}

				// try to prevent the dll from being unloaded so that our patches remain in memory
				LoadLibrary(targetFilename);
				//GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, targetFilename, &target);
			}
		}
		else // we have a module loaded at target so we're looking for offset:patch lines
		{
			if (strlen(line) > 8 && line[8] == ':')
			{
				DWORD offset;
				DWORD original_value, new_value;

				// parse byte patch line
				int sscanf_ret = sscanf(line, "%08X:%02X->%02X", &offset, &original_value, &new_value);
				if (sscanf_ret != 3)
				{
					Log("LoadPatchFile: Error parsing line %d", line_number);
					success = 0;
					break;
				}
				Log("LoadPatchFile: %s:%08X %02X -> %02X", targetFilename, offset, original_value, new_value);

				DWORD addr = (DWORD)target + offset;
				if (*(BYTE *)addr != original_value)
				{
					Log("LoadPatchFile: Error patching '%s' at line %d offset %08X.  Expected %02X but found %02X", targetFilename, line_number, offset, original_value, *(BYTE *)addr);
					success = 0;
					break;
				}
				else
				{
					// apply single byte patch
					Patch((void *)addr, &new_value, 1);
				}
			}
		}
	}

	fclose(fp);

	if (success)
	{
		Log("LoadPatchFile: Successfully applied patches from '%s'", filename);
	}
	else
	{
		Log("LoadPatchFile: Failed to apply patches from '%s'", filename);
	}

	return success;
}

#include "InstructionLength.h"
