/* showrestore.c — restore Ableton's main window if it booted minimized. */
#include <windows.h>

static HANDLE g_out;
static char buf[512];
static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

int mainCRTStartup( void )
{
    HWND hwnd = NULL, found = NULL;
    RECT wr;
    WINDOWPLACEMENT wp = { .length = sizeof(wp) };

    g_out = CreateFileA( "showrestore.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );

    while ((hwnd = FindWindowExW( NULL, hwnd, L"Ableton Live Window Class", NULL )))
    {
        BOOL has_menu = GetMenu( hwnd ) != NULL;
        BOOL iconic = IsIconic( hwnd );
        GetWindowRect( hwnd, &wr );
        GetWindowPlacement( hwnd, &wp );
        P( "hwnd %p menu=%d iconic=%d showCmd=%d rect (%d,%d)-(%d,%d)\n",
           hwnd, has_menu, iconic, (int)wp.showCmd,
           (int)wr.left, (int)wr.top, (int)wr.right, (int)wr.bottom );
        if (has_menu && (iconic || (wr.right - wr.left > 400))) found = hwnd;
    }

    if (found)
    {
        /* Wine and mutter re-assert Iconic at each other in a loop anchored
         * by Live's saved minimized placement.  Break it: force the Win32
         * side to normal, then immediately ask Live to quit while normal so
         * it saves a sane placement for the next boot. */
        P( "restoring %p then closing Live\n", found );
        ShowWindow( found, SW_HIDE );
        Sleep( 200 );
        ShowWindow( found, SW_SHOWNORMAL );
        Sleep( 200 );
        GetWindowPlacement( found, &wp );
        P( "pre-close: iconic=%d showCmd=%d\n", IsIconic( found ), (int)wp.showCmd );
        PostMessageW( found, WM_CLOSE, 0, 0 );
        P( "WM_CLOSE posted\n" );
    }
    CloseHandle( g_out );
    return found ? 0 : 1;
}
