/* metricprobe.c — pin down the AdjustWindowRectExForDpi vs WM_NCCALCSIZE
 * non-client delta that makes Live's main window grow +4px per WM configure.
 *
 * Creates a window with Live's exact main-window style (0x16cf0000 after
 * WS_VISIBLE, ex 0x100) and a real menu bar, then prints:
 *   - the relevant system metrics (plain and ForDpi 96)
 *   - SPI_GETNONCLIENTMETRICS(ForDpi 96) heights
 *   - AdjustWindowRectEx / AdjustWindowRectExForDpi results with bMenu on/off
 *   - the actual window/client rects (i.e. the NCCALCSIZE result)
 *   - GetMenuBarInfo's real menu-bar rect
 * output: metricprobe.txt in cwd.  build: build_metricprobe.sh
 */
#include <windows.h>

static HANDLE g_out;
static char buf[512];
static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int mainCRTStartup( void )
{
    static const DWORD style = 0x06cf0000;   /* live's 0x16cf0000 minus WS_VISIBLE */
    static const DWORD ex_style = 0x100;     /* WS_EX_WINDOWEDGE */
    WNDCLASSW wc = {0};
    HMENU menu;
    HWND hwnd;
    RECT r, wr, cr;
    MENUBARINFO mbi;
    NONCLIENTMETRICSW ncm;
    int i;

    g_out = CreateFileA( "metricprobe.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );

    /* replicate Live's main thread: ALF sets per-monitor-aware v2 */
    SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

    P( "system dpi (GetDpiForSystem): %d\n", (int)GetDpiForSystem() );
    P( "thread dpi awareness ctx: %p\n", GetThreadDpiAwarenessContext() );

    P( "\n-- system metrics (plain / ForDpi96) --\n" );
    static const struct { int sm; const char *name; } sms[] = {
        { SM_CYCAPTION, "SM_CYCAPTION" }, { SM_CYMENU, "SM_CYMENU" },
        { SM_CYMENUSIZE, "SM_CYMENUSIZE" },
        { SM_CXSIZEFRAME, "SM_CXSIZEFRAME" }, { SM_CYSIZEFRAME, "SM_CYSIZEFRAME" },
        { SM_CXPADDEDBORDER, "SM_CXPADDEDBORDER" },
        { SM_CXBORDER, "SM_CXBORDER" }, { SM_CYBORDER, "SM_CYBORDER" },
        { SM_CXEDGE, "SM_CXEDGE" }, { SM_CYEDGE, "SM_CYEDGE" },
    };
    for (i = 0; i < (int)(sizeof(sms)/sizeof(sms[0])); i++)
        P( "%s: %d / %d\n", sms[i].name,
           GetSystemMetrics( sms[i].sm ), GetSystemMetricsForDpi( sms[i].sm, 96 ) );

    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi( SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, 96 );
    P( "\n-- SPI_GETNONCLIENTMETRICS ForDpi(96) --\n" );
    P( "iBorderWidth %d iPaddedBorderWidth %d iCaptionHeight %d iMenuHeight %d\n",
       (int)ncm.iBorderWidth, (int)ncm.iPaddedBorderWidth,
       (int)ncm.iCaptionHeight, (int)ncm.iMenuHeight );
    P( "MenuFont height %d  CaptionFont height %d\n",
       (int)ncm.lfMenuFont.lfHeight, (int)ncm.lfCaptionFont.lfHeight );

    P( "\n-- AdjustWindowRect* on (0,0)-(1000,500), style %08x ex %08x --\n",
       (UINT)(style | WS_VISIBLE), (UINT)ex_style );
    SetRect( &r, 0, 0, 1000, 500 );
    AdjustWindowRectEx( &r, style | WS_VISIBLE, FALSE, ex_style );
    P( "AdjustWindowRectEx   menu=0: (%d,%d)-(%d,%d)  v-extra %d\n",
       (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, (int)(r.bottom - r.top - 500) );
    SetRect( &r, 0, 0, 1000, 500 );
    AdjustWindowRectEx( &r, style | WS_VISIBLE, TRUE, ex_style );
    P( "AdjustWindowRectEx   menu=1: (%d,%d)-(%d,%d)  v-extra %d\n",
       (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, (int)(r.bottom - r.top - 500) );
    SetRect( &r, 0, 0, 1000, 500 );
    AdjustWindowRectExForDpi( &r, style | WS_VISIBLE, FALSE, ex_style, 96 );
    P( "AdjustWindowRectExForDpi(96) menu=0: (%d,%d)-(%d,%d)  v-extra %d\n",
       (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, (int)(r.bottom - r.top - 500) );
    SetRect( &r, 0, 0, 1000, 500 );
    AdjustWindowRectExForDpi( &r, style | WS_VISIBLE, TRUE, ex_style, 96 );
    P( "AdjustWindowRectExForDpi(96) menu=1: (%d,%d)-(%d,%d)  v-extra %d\n",
       (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, (int)(r.bottom - r.top - 500) );

    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW( NULL );
    wc.lpszClassName = L"MetricProbeClass";
    RegisterClassW( &wc );

    menu = CreateMenu();
    AppendMenuW( menu, MF_STRING, 1, L"File" );
    AppendMenuW( menu, MF_STRING, 2, L"Edit" );
    AppendMenuW( menu, MF_STRING, 3, L"Create" );
    AppendMenuW( menu, MF_STRING, 4, L"View" );
    AppendMenuW( menu, MF_STRING, 5, L"Options" );
    AppendMenuW( menu, MF_STRING, 6, L"Help" );

    hwnd = CreateWindowExW( ex_style, wc.lpszClassName, L"metricprobe",
                            style, 100, 100, 1000, 500, NULL, menu,
                            wc.hInstance, NULL );
    if (!hwnd) { P( "CreateWindowExW failed %d\n", (int)GetLastError() ); goto done; }

    GetWindowRect( hwnd, &wr );
    GetClientRect( hwnd, &cr );
    P( "\n-- real window (not shown), outer 1000x500 --\n" );
    P( "window rect (%d,%d)-(%d,%d) %dx%d\n", (int)wr.left, (int)wr.top,
       (int)wr.right, (int)wr.bottom, (int)(wr.right - wr.left), (int)(wr.bottom - wr.top) );
    P( "client rect %dx%d  -> NC total v %d, h %d\n",
       (int)cr.right, (int)cr.bottom,
       (int)((wr.bottom - wr.top) - cr.bottom), (int)((wr.right - wr.left) - cr.right) );

    mbi.cbSize = sizeof(mbi);
    if (GetMenuBarInfo( hwnd, OBJID_MENU, 0, &mbi ))
        P( "menu bar rect (%d,%d)-(%d,%d) height %d\n",
           (int)mbi.rcBar.left, (int)mbi.rcBar.top, (int)mbi.rcBar.right,
           (int)mbi.rcBar.bottom, (int)(mbi.rcBar.bottom - mbi.rcBar.top) );

    /* round-trip check: what Live does on WM_WINDOWPOSCHANGED */
    SetRect( &r, 0, 0, cr.right, cr.bottom );
    AdjustWindowRectExForDpi( &r, style | WS_VISIBLE, TRUE, ex_style, 96 );
    P( "round trip: client %dx%d + adjust(menu=1) = %dx%d (actual outer %dx%d, drift %+d)\n",
       (int)cr.right, (int)cr.bottom,
       (int)(r.right - r.left), (int)(r.bottom - r.top),
       (int)(wr.right - wr.left), (int)(wr.bottom - wr.top),
       (int)((r.bottom - r.top) - (wr.bottom - wr.top)) );

    DestroyWindow( hwnd );
done:
    CloseHandle( g_out );
    return 0;
}
