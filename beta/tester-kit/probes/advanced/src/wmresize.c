/* wmresize.c — minimal reproducer probe for the Ableton +4px resize settle.
 *
 * Creates a VISIBLE, mutter-managed overlapped window with a menu bar
 * (Live's main-window anatomy: style 0x16cf0000, ex 0x100), then mimics
 * Live's WM_WINDOWPOSCHANGED handler exactly:
 *     GetClientRect -> AdjustWindowRectExForDpi(menu=1, dpi 96)
 *     -> SetWindowPos(0,0,cx,cy, NOMOVE|NOZORDER|NOACTIVATE|NOOWNERZORDER)
 * Logs every rect/value.  Drive it from the X side with:
 *     xsettle moveresize ... (class "WmResizeProbe" appears in _NET_CLIENT_LIST)
 * If this window grows +4 per WM-driven configure, Wine reproduces the bug
 * without Live; if it holds size, Live's own arithmetic adds the 4.
 * Runs for ~30 s, output wmresize.txt in cwd.
 */
#include <windows.h>

static HANDLE g_out;
static char buf[512];
static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

static int g_reentry;

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_WINDOWPOSCHANGED && !g_reentry)
    {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        RECT cr, wr, adj;

        GetClientRect( hwnd, &cr );
        GetWindowRect( hwnd, &wr );
        SetRect( &adj, 0, 0, cr.right, cr.bottom );
        AdjustWindowRectExForDpi( &adj, (DWORD)GetWindowLongPtrW( hwnd, GWL_STYLE ),
                                  GetMenu( hwnd ) != NULL,
                                  (DWORD)GetWindowLongPtrW( hwnd, GWL_EXSTYLE ), 96 );
        P( "WPC flags %04x pos %dx%d | window %dx%d client %dx%d | adj -> %dx%d (drift %d)\n",
           (UINT)pos->flags, (int)pos->cx, (int)pos->cy,
           (int)(wr.right - wr.left), (int)(wr.bottom - wr.top),
           (int)cr.right, (int)cr.bottom,
           (int)(adj.right - adj.left), (int)(adj.bottom - adj.top),
           (int)((adj.bottom - adj.top) - (wr.bottom - wr.top)) );

        /* mimic Live: re-request the adjust-derived outer size */
        g_reentry = 1;
        SetWindowPos( hwnd, NULL, 0, 0, adj.right - adj.left, adj.bottom - adj.top,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER );
        g_reentry = 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int mainCRTStartup( void )
{
    WNDCLASSW wc = {0};
    HMENU menu;
    HWND hwnd;
    MSG msg;
    DWORD64 t0;

    g_out = CreateFileA( "wmresize.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );

    SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.lpszClassName = L"WmResizeProbe";
    RegisterClassW( &wc );

    menu = CreateMenu();
    AppendMenuW( menu, MF_STRING, 1, L"File" );
    AppendMenuW( menu, MF_STRING, 2, L"Edit" );
    AppendMenuW( menu, MF_STRING, 3, L"Create" );
    AppendMenuW( menu, MF_STRING, 4, L"View" );
    AppendMenuW( menu, MF_STRING, 5, L"Options" );
    AppendMenuW( menu, MF_STRING, 6, L"Help" );

    hwnd = CreateWindowExW( 0x100, wc.lpszClassName, L"wmresize probe",
                            0x06cf0000 | WS_VISIBLE, 200, 200, 900, 600,
                            NULL, menu, wc.hInstance, NULL );
    if (!hwnd) { P( "create failed %d\n", (int)GetLastError() ); goto done; }
    P( "created hwnd %p\n", hwnd );

    t0 = GetTickCount64();
    while (GetTickCount64() - t0 < 30000)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 200, QS_ALLINPUT );
    }
    DestroyWindow( hwnd );
done:
    CloseHandle( g_out );
    return 0;
}
