/* menutest.c — does popup-menu window creation work in this prefix? (PE, CRT-free)
 *
 * Creates an owner window + popup menu, tracks it, and self-dismisses via a
 * timer. A vectored exception handler logs any access violation (the Ableton
 * failure mode is a swallowed NULL-wndproc call during the menu window's
 * WM_NCCREATE). Also counts visible #32768 windows while the menu is up.
 *
 * output: menutest.txt in cwd.  build: via build_swamprobe.sh style (see build_mousespy.sh)
 */
#include <windows.h>

static HANDLE g_out;
static char buf[512];
static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

static int g_menu_windows;

static LONG CALLBACK veh( EXCEPTION_POINTERS *ep )
{
    P( "VEH: code=%08x addr=%p\n",
       (UINT)ep->ExceptionRecord->ExceptionCode,
       ep->ExceptionRecord->ExceptionAddress );
    return EXCEPTION_CONTINUE_SEARCH;
}

static BOOL CALLBACK count_cb( HWND h, LPARAM lp )
{
    char cls[64];
    GetClassNameA( h, cls, sizeof cls );
    if (!lstrcmpA( cls, "#32768" ) && IsWindowVisible( h ))
    {
        RECT r; GetWindowRect( h, &r );
        g_menu_windows++;
        P( "menu window %p visible rect=(%d,%d)-(%d,%d)\n", h,
           (int)r.left, (int)r.top, (int)r.right, (int)r.bottom );
    }
    return TRUE;
}

static LRESULT CALLBACK wndproc( HWND h, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_TIMER:
        EnumWindows( count_cb, 0 );
        P( "WM_TIMER: visible menu windows: %d\n", g_menu_windows );
        KillTimer( h, 1 );
        EndMenu();
        return 0;
    case WM_ENTERMENULOOP: P( "WM_ENTERMENULOOP\n" ); return 0;
    case WM_EXITMENULOOP:  P( "WM_EXITMENULOOP\n" ); return 0;
    }
    return DefWindowProcW( h, msg, wp, lp );
}

void mainCRTStartup( void )
{
    WNDCLASSW wc;
    HWND hwnd;
    HMENU menu;
    BOOL ret;

    g_out = CreateFileA( "menutest.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );

    AddVectoredExceptionHandler( 1, veh );

    /* mimic Ableton's IFEO dpiAwareness=2: per-monitor-aware process */
    {
        typedef BOOL (WINAPI *pSet)(DPI_AWARENESS_CONTEXT);
        pSet p = (pSet)GetProcAddress( GetModuleHandleA( "user32.dll" ),
                                       "SetProcessDpiAwarenessContext" );
        if (p)
        {
            BOOL ok = p( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
            P( "SetProcessDpiAwarenessContext(PMv2) -> %d\n", ok );
        }
    }

    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.lpszClassName = L"menutest";
    RegisterClassW( &wc );

    hwnd = CreateWindowExW( 0, L"menutest", L"menutest", WS_OVERLAPPEDWINDOW,
                            100, 100, 300, 200, NULL, NULL, wc.hInstance, NULL );
    P( "owner hwnd=%p\n", hwnd );
    ShowWindow( hwnd, SW_SHOWNOACTIVATE );

    menu = CreatePopupMenu();
    AppendMenuA( menu, MF_STRING, 1, "Item One" );
    AppendMenuA( menu, MF_STRING, 2, "Item Two" );
    AppendMenuA( menu, MF_STRING, 3, "Item Three" );

    SetTimer( hwnd, 1, 900, NULL );
    SetForegroundWindow( hwnd );
    ret = TrackPopupMenu( menu, TPM_LEFTALIGN | TPM_RETURNCMD, 500, 500, 0, hwnd, NULL );
    P( "TrackPopupMenu returned %d lasterr=%u menu_windows_seen=%d\n",
       ret, (unsigned)GetLastError(), g_menu_windows );

    CloseHandle( g_out );
    ExitProcess( 0 );
}
