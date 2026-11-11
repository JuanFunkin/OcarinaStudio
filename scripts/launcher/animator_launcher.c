/*
 * OcarinaAnimator launcher (Windows).
 *
 * Starts OcarinaStudio in Animator mode: the game opens straight on a green screen with Link standing still
 * and the Animator window ready. It is the same program as OcarinaStudio, started with OCARINA_ANIMATOR set.
 * It must sit in the same folder as OcarinaStudio's soh.exe.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmdLine, int show) {
    char folder[MAX_PATH];
    char exe[MAX_PATH];
    char commandLine[MAX_PATH * 2];
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    char* slash;

    (void)instance;
    (void)previous;
    (void)show;

    if (GetModuleFileNameA(NULL, folder, MAX_PATH) == 0) {
        return 1;
    }
    slash = strrchr(folder, '\\');
    if (slash == NULL) {
        return 1;
    }
    *slash = '\0';

    snprintf(exe, sizeof(exe), "%s\\soh.exe", folder);
    if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(NULL, "soh.exe was not found next to OcarinaAnimator.exe.\nKeep both files in the same folder.",
                    "OcarinaAnimator", MB_ICONERROR);
        return 1;
    }

    SetEnvironmentVariableA("OCARINA_ANIMATOR", "1");
    snprintf(commandLine, sizeof(commandLine), "\"%s\" %s", exe, cmdLine);

    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    ZeroMemory(&process, sizeof(process));
    if (!CreateProcessA(exe, commandLine, NULL, NULL, FALSE, 0, NULL, folder, &startup, &process)) {
        MessageBoxA(NULL, "Could not start OcarinaStudio.", "OcarinaAnimator", MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
