/* fakeplugin.c — minimal reproducer for the Ableton-on-Wine plugin titlebar bug.
 *
 * Mirrors Ableton Live 12's window anatomy at 192 DPI:
 *  - process is per-monitor-v2 aware (like the IFEO dpiAwareness=2 fix)
 *  - main window: overlapped, resizable, pm-v2 (192-DPI space)
 *  - two "plugin editor" windows created while the thread DPI context is
 *    UNAWARE (the VST3-host trick), owned by the main window, styled exactly
 *    like Live's Vst3PlugWindow: WS_CAPTION|WS_SYSMENU + WS_EX_TOOLWINDOW.
 *      P1 "FakePlugDef":  default DefWindowProc non-client handling
 *      P2 "FakePlugLive": custom WM_NCCALCSIZE insetting top by 58 / sides by
 *         3 units, replicating the rects observed from Live's Vst3PlugWindow
 *         (window (526,315)-(1257,965) vs client (529,373)-(1254,962)).
 *
 * Writes fakeplugin-report.txt (cwd) with awareness/dpi/rects per window,
 * then quits on its own after 180 s.
 */

#include <windows.h>
#include <stdio.h>

static HWND main_win, plug_def, plug_live, shadow_win;

/* mimic a JUCE DropShadower window: per-pixel-alpha layered popup drawn with
 * UpdateLayeredWindow, premultiplied black with an alpha gradient */
static void create_shadow( HINSTANCE inst, int px, int py )
{
    const int w = 300, h = 200;
    BITMAPINFO bi = {0};
    void *bits = NULL;
    HDC scr, mem;
    HBITMAP bmp;
    HGDIOBJ old;
    POINT src = {0, 0}, dst = {px, py};
    SIZE sz = {w, h};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    int x, y;

    shadow_win = CreateWindowExW( WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
                                  L"FakeShadow", L"", WS_POPUP | WS_VISIBLE,
                                  dst.x, dst.y, w, h, main_win, NULL, inst, NULL );

    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    scr = GetDC( 0 );
    mem = CreateCompatibleDC( scr );
    bmp = CreateDIBSection( mem, &bi, DIB_RGB_COLORS, &bits, NULL, 0 );
    old = SelectObject( mem, bmp );

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            BYTE a = (BYTE)(255 * x / w);  /* transparent left -> opaque right */
            ((DWORD *)bits)[y * w + x] = ((DWORD)a << 24); /* premultiplied black */
        }

    UpdateLayeredWindow( shadow_win, scr, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA );

    SelectObject( mem, old );
    DeleteObject( bmp );
    DeleteDC( mem );
    ReleaseDC( 0, scr );
}

static void report( void )
{
    FILE *f = fopen( "fakeplugin-report.txt", "w" );
    HWND wins[3] = { main_win, plug_def, plug_live };
    const char *names[3] = { "main", "plug_def", "plug_live" };
    int i;

    if (!f) return;
    for (i = 0; i < 3; i++)
    {
        HWND hwnd = wins[i];
        RECT w = {0}, c = {0};
        POINT origin = {0, 0};
        DPI_AWARENESS_CONTEXT ctx = GetWindowDpiAwarenessContext( hwnd );
        UINT dpi = GetDpiForWindow( hwnd );

        GetWindowRect( hwnd, &w );
        GetClientRect( hwnd, &c );
        ClientToScreen( hwnd, &origin );
        fprintf( f, "%s hwnd=%p awareness_ctx=%p dpi=%u window=(%ld,%ld)-(%ld,%ld) %ldx%ld "
                 "client_at=(%ld,%ld) size %ldx%ld top_nc=%ld\n",
                 names[i], (void *)hwnd, (void *)ctx, dpi,
                 w.left, w.top, w.right, w.bottom, w.right - w.left, w.bottom - w.top,
                 origin.x, origin.y, c.right - c.left, c.bottom - c.top,
                 origin.y - w.top );
    }
    fclose( f );
}

static LRESULT CALLBACK def_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_DESTROY && hwnd == main_win) { PostQuitMessage( 0 ); return 0; }
    if (msg == WM_TIMER && wp == 1) { report(); KillTimer( hwnd, 1 ); return 0; }
    if (msg == WM_TIMER && wp == 2) { PostQuitMessage( 0 ); return 0; }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

/* mimic Live's ALF: it computes the editor frame with 192-dpi metrics even
 * though the window itself lives in unaware 96-dpi space */
static LRESULT CALLBACK live_nc_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_NCCALCSIZE)
    {
        RECT *r = wp ? &((NCCALCSIZE_PARAMS *)lp)->rgrc[0] : (RECT *)lp;
        r->left += 3; r->right -= 3; r->bottom -= 3; r->top += 58;
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show )
{
    WNDCLASSW wc = {0};
    DPI_AWARENESS_CONTEXT old;
    MSG msg;

    SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

    wc.lpfnWndProc = def_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"FakeMainWindow";
    RegisterClassW( &wc );
    wc.lpszClassName = L"FakePlugDef";
    RegisterClassW( &wc );
    wc.lpfnWndProc = live_nc_proc;
    wc.lpszClassName = L"FakePlugLive";
    RegisterClassW( &wc );
    wc.lpfnWndProc = def_proc;
    wc.lpszClassName = L"FakeShadow";
    RegisterClassW( &wc );

    main_win = CreateWindowExW( 0, L"FakeMainWindow", L"Live (fake main)",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                100, 100, 3000, 2200, NULL, NULL, inst, NULL );

    /* the VST3-host trick: editor windows are created from an unaware context */
    old = SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_UNAWARE );
    plug_def = CreateWindowExW( WS_EX_TOOLWINDOW, L"FakePlugDef", L"Mellotron/DefNC",
                                WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VISIBLE,
                                47, 72, 731, 650, main_win, NULL, inst, NULL );
    plug_live = CreateWindowExW( WS_EX_TOOLWINDOW, L"FakePlugLive", L"Mellotron/LiveNC",
                                 WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VISIBLE,
                                 820, 72, 731, 650, main_win, NULL, inst, NULL );
    create_shadow( inst, 200, 900 );   /* unaware, 96-space -> DPI-scaled surface */
    SetThreadDpiAwarenessContext( old );
    create_shadow( inst, 2000, 1800 ); /* pm-v2 control, 192-space -> direct surface */

    SetTimer( main_win, 1, 3000, NULL );    /* write report once settled */
    SetTimer( main_win, 2, 180000, NULL );  /* self-destruct */

    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}
