/* hwndspy.c — dump the full HWND tree with class/styles/rect/pid (PE, CRT-free).
 *
 * Walks every top-level window in the session and recurses through children,
 * printing class name, WS_/WS_EX_ styles (raw hex + CLIPCHILDREN/CLIPSIBLINGS
 * flags spelled out), window rect, and owning pid/tid. For diagnosing the
 * WebView2 doc-sidebar flicker: is the webview a cross-process child of
 * Live's main window, and does the parent chain carry WS_CLIPCHILDREN?
 *
 * output: hwndspy.txt in cwd.  build: build_hwndspy.sh (build_swamprobe.sh style)
 */
#include <windows.h>

static HANDLE g_out;
static char buf[1024];
static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

static void indent( int depth )
{
    int i;
    for (i = 0; i < depth; i++) emit( "  " );
}

static void dump( HWND hwnd, int depth )
{
    char cls[128] = "", title[128] = "";
    RECT r = {0};
    DWORD pid = 0, tid;
    LONG style, exstyle;
    HWND child;

    GetClassNameA( hwnd, cls, sizeof(cls) );
    GetWindowTextA( hwnd, title, sizeof(title) );
    GetWindowRect( hwnd, &r );
    tid = GetWindowThreadProcessId( hwnd, &pid );
    style = GetWindowLongA( hwnd, GWL_STYLE );
    exstyle = GetWindowLongA( hwnd, GWL_EXSTYLE );

    indent( depth );
    P( "%p pid=%u tid=%u cls=\"%s\" title=\"%s\" rect=(%d,%d)-(%d,%d) style=%08x%s%s%s ex=%08x%s\r\n",
       hwnd, (unsigned)pid, (unsigned)tid, cls, title,
       (int)r.left, (int)r.top, (int)r.right, (int)r.bottom,
       (unsigned)style,
       (style & WS_CLIPCHILDREN) ? " CLIPCHILDREN" : "",
       (style & WS_CLIPSIBLINGS) ? " CLIPSIBLINGS" : "",
       (style & WS_VISIBLE) ? " VIS" : " hidden",
       (unsigned)exstyle,
       (exstyle & WS_EX_LAYERED) ? " LAYERED" : "" );

    for (child = GetWindow( hwnd, GW_CHILD ); child; child = GetWindow( child, GW_HWNDNEXT ))
        dump( child, depth + 1 );
}

static BOOL CALLBACK top_cb( HWND hwnd, LPARAM lp )
{
    dump( hwnd, 0 );
    return TRUE;
}

int mainCRTStartup( void )
{
    g_out = CreateFileA( "hwndspy.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, 0, NULL );
    EnumWindows( top_cb, 0 );
    CloseHandle( g_out );
    return 0;
}
