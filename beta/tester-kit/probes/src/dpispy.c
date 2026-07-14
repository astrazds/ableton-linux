/* dpispy: dump DPI awareness context + dpi + rects (logical and physical
 * views) for every visible window. Output: dpispy.txt */
#include <windows.h>

static HANDLE g_out;
static char buf[512];

static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

static void dump( HWND hwnd, int depth )
{
    char cls[128] = "", title[128] = "";
    RECT wr_l = {0}, wr_p = {0};
    DPI_AWARENESS_CONTEXT wctx = GetWindowDpiAwarenessContext( hwnd );
    UINT dpi = GetDpiForWindow( hwnd );
    DPI_AWARENESS_CONTEXT prev;

    GetClassNameA( hwnd, cls, sizeof(cls) );
    GetWindowTextA( hwnd, title, sizeof(title) );

    prev = SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_UNAWARE );
    GetWindowRect( hwnd, &wr_l );
    SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    GetWindowRect( hwnd, &wr_p );
    SetThreadDpiAwarenessContext( prev );

    P( "%s%p cls=\"%s\" title=\"%s\" ctx=%p dpi=%u log=(%d,%d)-(%d,%d) %dx%d phys=(%d,%d)-(%d,%d) %dx%d\n",
       depth ? "  " : "", hwnd, cls, title, wctx, dpi,
       (int)wr_l.left, (int)wr_l.top, (int)wr_l.right, (int)wr_l.bottom,
       (int)(wr_l.right - wr_l.left), (int)(wr_l.bottom - wr_l.top),
       (int)wr_p.left, (int)wr_p.top, (int)wr_p.right, (int)wr_p.bottom,
       (int)(wr_p.right - wr_p.left), (int)(wr_p.bottom - wr_p.top) );
}

static BOOL CALLBACK child_cb( HWND hwnd, LPARAM lp )
{
    dump( hwnd, 1 );
    return TRUE;
}

static BOOL CALLBACK top_cb( HWND hwnd, LPARAM lp )
{
    if (!IsWindowVisible( hwnd )) return TRUE;
    dump( hwnd, 0 );
    EnumChildWindows( hwnd, child_cb, 0 );
    return TRUE;
}

int __stdcall WinMainCRTStartup( void )
{
    g_out = CreateFileA( "dpispy.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, 0, NULL );
    if (g_out == INVALID_HANDLE_VALUE) ExitProcess( 1 );
    EnumWindows( top_cb, 0 );
    CloseHandle( g_out );
    ExitProcess( 0 );
}
