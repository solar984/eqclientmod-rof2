# eqclientmod for EverQuest RoF2 client
solar@heliacal.net

This is a game mod implemented as a planted DLL file.  It works by wrapping an existing DLL the game already uses and because it's in the game directory it's loaded before the one in the system directory.

# Installation
To install just copy `winmm.dll` to the game directory (the directory that contains `eqgame.exe`).
To uninstall remove or rename `winmm.dll`.

Once the game runs the mod will create a config file named `eqclientmod.ini` which contains toggles for the included mods along with a brief description of what they do.

# Mods

### CPU High Speed Fix
CPU high clock speed overflow fix.  If you have a CPU that's more than 4.2 Ghz you probably need this to make the game run at the right speed.
This issue happens with all AMD Ryzen 7xxx and 9xxxx CPUs at least.  The game normally samples the TSC by using the `rdtsc` instruction.  This mod makes it use a modern API called `QueryPerformanceCounter`.

### Disable Gamma Change
The game has a gamma slider in it but it does it in a really annoying way that changes the desktop gamma.  It tries to restore it when the game exits but this doesn't always happen with multiple clients and crashes.
This mod disables the gamma functionality by detouring the function that would do this and instead doing nothing.  If you like the gamma, make sure you edit `eqclientmod.ini` and disable this mod.

### CommandHandler
This mod adds the extra command handling that some of the other hacks use but it's not strictly necessary to enable this to use the other hacks.  Basic commands included: /eqclientmod /crash

### Program Launch
This hack removes the requirement to pass the 'patchme' argument to the program to start it.

### Window Title
This hack renames the application window from EverQuest to Client1, Client2, etc depending on how many other windows are already open.  This is intended for macro software like hotkeynet and AutoHotKey to make it easier to search for the window.
