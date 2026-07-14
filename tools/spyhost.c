/* spyhost.c — installs the global WH_MOUSE hook from mousespy.dll (PE, CRT-free).
 * usage: spyhost <seconds>
 * build: see build_mousespy.sh
 */
#include <windows.h>

typedef LRESULT (CALLBACK *hookproc_t)( int, WPARAM, LPARAM );

static int my_atoi( const char *s )
{
    int v = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

void mainCRTStartup( void )
{
    HMODULE dll = LoadLibraryA( "Z:\\home\\theo\\Projects\\Code\\ableton\\mousespy.dll" );
    hookproc_t proc;
    HHOOK hook;
    int secs = 15;
    const char *cmd = GetCommandLineA();
    MSG msg;
    DWORD end;

    /* last token = seconds, if numeric */
    { const char *s = cmd, *last = cmd;
      while (*s) { if (*s == ' ' && s[1]) last = s + 1; s++; }
      if (*last >= '0' && *last <= '9') secs = my_atoi( last ); }

    if (!dll) ExitProcess( 2 );
    proc = (hookproc_t)GetProcAddress( dll, "mouse_hook" );
    if (!proc) ExitProcess( 3 );

    hook = SetWindowsHookExA( WH_MOUSE, (HOOKPROC)proc, dll, 0 );
    if (!hook) ExitProcess( 4 );

    end = GetTickCount() + secs * 1000;
    while (GetTickCount() < end)
    {
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        Sleep( 20 );
    }
    UnhookWindowsHookEx( hook );
    ExitProcess( 0 );
}
