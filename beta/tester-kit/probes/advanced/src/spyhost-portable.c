/* Portable form of ableton/spyhost.c.
 *
 * Loads mousespy.dll from the same directory as this executable instead of
 * Theo's source checkout, installs its WH_MOUSE hook, pumps messages for the
 * requested number of seconds, then removes the hook.
 */
#include <windows.h>

typedef LRESULT (CALLBACK *hookproc_t)(int, WPARAM, LPARAM);

static int parse_seconds(const char *command)
{
    const char *cursor = command;
    const char *last = command;
    int value = 0;

    while (*cursor)
    {
        if (*cursor == ' ' && cursor[1]) last = cursor + 1;
        cursor++;
    }
    while (*last == ' ') last++;
    while (*last >= '0' && *last <= '9')
    {
        value = value * 10 + (*last - '0');
        last++;
    }
    if (value < 1 || value > 120) value = 15;
    return value;
}

void mainCRTStartup(void)
{
    char path[MAX_PATH];
    char *slash;
    HMODULE library;
    hookproc_t procedure;
    HHOOK hook;
    MSG message;
    DWORD end;
    int seconds = parse_seconds(GetCommandLineA());

    if (!GetModuleFileNameA(NULL, path, sizeof(path))) ExitProcess(2);
    slash = path + lstrlenA(path);
    while (slash > path && slash[-1] != '\\' && slash[-1] != '/') slash--;
    lstrcpyA(slash, "mousespy.dll");

    library = LoadLibraryA(path);
    if (!library) ExitProcess(2);
    procedure = (hookproc_t)GetProcAddress(library, "mouse_hook");
    if (!procedure) ExitProcess(3);
    hook = SetWindowsHookExA(WH_MOUSE, (HOOKPROC)procedure, library, 0);
    if (!hook) ExitProcess(4);

    end = GetTickCount() + (DWORD)seconds * 1000;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
    }

    UnhookWindowsHookEx(hook);
    FreeLibrary(library);
    ExitProcess(0);
}
