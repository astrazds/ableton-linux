/* stresstest.c — hammer the shared-session allocator (PE, CRT-free).
 *
 * Repeatedly registers window classes and creates/destroys windows, forcing
 * wineserver session-object allocation, free-list reuse, and session mapping
 * growth. This is the exact path that failed intermittently in Ableton
 * ("Failed to get shared session object" -> NULL wndproc -> WM_NCCREATE AV):
 * any regression shows up as a registration/creation failure or a VEH hit.
 *
 * usage: stresstest [iterations]   (default 20000)
 * output: stresstest.txt
 */
#include <windows.h>

static HANDLE g_out;
static char buf[512];
static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

static LONG g_veh_count;

static LONG CALLBACK veh( EXCEPTION_POINTERS *ep )
{
    InterlockedIncrement( &g_veh_count );
    P( "VEH: code=%08x addr=%p\n",
       (UINT)ep->ExceptionRecord->ExceptionCode,
       ep->ExceptionRecord->ExceptionAddress );
    return EXCEPTION_CONTINUE_SEARCH;
}

static LRESULT CALLBACK wndproc( HWND h, UINT msg, WPARAM wp, LPARAM lp )
{
    return DefWindowProcW( h, msg, wp, lp );
}

static int my_atoi( const char *s )
{
    int v = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

void mainCRTStartup( void )
{
    WCHAR clsname[64];
    int iters = 20000, i, fail_reg = 0, fail_win = 0;
    const char *cmd = GetCommandLineA();
    HINSTANCE inst = GetModuleHandleW( NULL );

    { const char *s = cmd, *last = cmd;
      while (*s) { if (*s == ' ' && s[1]) last = s + 1; s++; }
      if (*last >= '0' && *last <= '9') iters = my_atoi( last ); }

    g_out = CreateFileA( "stresstest.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    AddVectoredExceptionHandler( 1, veh );

    P( "stress: %d iterations\n", iters );

    for (i = 0; i < iters; i++)
    {
        WNDCLASSW wc;
        HWND hwnd;
        ATOM atom;
        int j = 0;
        UINT v = (UINT)i;

        /* unique class name per iteration */
        clsname[j++] = 's'; clsname[j++] = 't'; clsname[j++] = 'r';
        do { clsname[j++] = L'0' + (v % 10); v /= 10; } while (v);
        clsname[j] = 0;

        memset( &wc, 0, sizeof(wc) );
        wc.lpfnWndProc = wndproc;
        wc.hInstance = inst;
        wc.lpszClassName = clsname;
        wc.cbWndExtra = (i % 5) * 8;   /* vary object sizes */

        if (!(atom = RegisterClassW( &wc )))
        {
            fail_reg++;
            P( "iter %d: RegisterClassW FAILED err=%u\n", i, (unsigned)GetLastError() );
        }
        else
        {
            if (!(hwnd = CreateWindowExW( 0, clsname, L"s", WS_POPUP,
                                          0, 0, 8, 8, NULL, NULL, inst, NULL )))
            {
                fail_win++;
                P( "iter %d: CreateWindowExW FAILED err=%u\n", i, (unsigned)GetLastError() );
            }
            else DestroyWindow( hwnd );
            UnregisterClassW( clsname, inst );
        }

        if (i % 5000 == 4999)
            P( "iter %d: reg_fail=%d win_fail=%d veh=%d\n", i, fail_reg, fail_win, (int)g_veh_count );
    }

    P( "DONE: iters=%d reg_fail=%d win_fail=%d veh=%d\n", iters, fail_reg, fail_win, (int)g_veh_count );
    CloseHandle( g_out );
    ExitProcess( (fail_reg || fail_win || g_veh_count) ? 1 : 0 );
}
