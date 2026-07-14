/* menumeasure.c — measure the REAL Ableton main window's menu bar vs metrics.
 * Finds "Ableton Live Window Class" / "Live", then prints window/client rects,
 * GetMenuBarInfo rect, per-item GetMenuItemRect, and SM_CYMENU — to pin the
 * +4px NC divergence (Live thinks NC v = 57, Wine NCCALCSIZE applies 53).
 * output: menumeasure.txt in cwd.  build: like metricprobe (see build cmd).
 */
#include <windows.h>

static HANDLE g_out;
static char buf[512];
static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

int mainCRTStartup( void )
{
    HWND hwnd;
    HMENU menu;
    RECT wr, cr, ir;
    MENUBARINFO mbi;
    int i, count;

    g_out = CreateFileA( "menumeasure.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );

    SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

    /* several windows share the class (e.g. "Messaging Window"); take the
     * one with a menu and a real size */
    hwnd = NULL;
    while ((hwnd = FindWindowExW( NULL, hwnd, L"Ableton Live Window Class", NULL )))
    {
        GetWindowRect( hwnd, &wr );
        if (GetMenu( hwnd ) && wr.right - wr.left > 400) break;
    }
    P( "live main hwnd: %p\n", hwnd );
    if (!hwnd) goto done;

    GetWindowRect( hwnd, &wr );
    GetClientRect( hwnd, &cr );
    P( "window rect (%d,%d)-(%d,%d) %dx%d\n", (int)wr.left, (int)wr.top,
       (int)wr.right, (int)wr.bottom, (int)(wr.right - wr.left), (int)(wr.bottom - wr.top) );
    P( "client %dx%d -> NC total v %d h %d\n", (int)cr.right, (int)cr.bottom,
       (int)((wr.bottom - wr.top) - cr.bottom), (int)((wr.right - wr.left) - cr.right) );

    P( "SM_CYMENU %d SM_CYCAPTION %d SM_CYSIZEFRAME %d SM_CXPADDEDBORDER %d\n",
       GetSystemMetrics( SM_CYMENU ), GetSystemMetrics( SM_CYCAPTION ),
       GetSystemMetrics( SM_CYSIZEFRAME ), GetSystemMetrics( SM_CXPADDEDBORDER ) );

    mbi.cbSize = sizeof(mbi);
    if (GetMenuBarInfo( hwnd, OBJID_MENU, 0, &mbi ))
        P( "GetMenuBarInfo rcBar (%d,%d)-(%d,%d) HEIGHT %d  (window top %d -> bar offset %d)\n",
           (int)mbi.rcBar.left, (int)mbi.rcBar.top, (int)mbi.rcBar.right, (int)mbi.rcBar.bottom,
           (int)(mbi.rcBar.bottom - mbi.rcBar.top), (int)wr.top, (int)(mbi.rcBar.top - wr.top) );
    else
        P( "GetMenuBarInfo failed %d\n", (int)GetLastError() );

    menu = GetMenu( hwnd );
    P( "menu %p\n", menu );
    if (menu)
    {
        count = GetMenuItemCount( menu );
        P( "items: %d\n", count );
        for (i = 0; i < count && i < 12; i++)
        {
            char text[64] = {0};
            GetMenuStringA( menu, i, text, sizeof(text) - 1, MF_BYPOSITION );
            if (GetMenuItemRect( hwnd, menu, i, &ir ))
                P( "  item %d '%s' rect (%d,%d)-(%d,%d) h %d\n", i, text,
                   (int)ir.left, (int)ir.top, (int)ir.right, (int)ir.bottom,
                   (int)(ir.bottom - ir.top) );
        }
    }
done:
    CloseHandle( g_out );
    return 0;
}
