/* mousespy.c — global WH_MOUSE hook DLL (PE, CRT-free).
 *
 * Injected into every GUI process of the session by spyhost.exe via
 * SetWindowsHookEx(WH_MOUSE, ..., hInstDll, 0). The hook fires in the thread
 * that REMOVES a mouse message from its queue — i.e. after wineserver routing
 * and win32u HTTRANSPARENT re-routing — so it shows the FINAL target hwnd and
 * the coordinates the app actually sees.
 *
 * Logs to Z:\tmp\mousespy.txt (append, share-write) from whatever process the
 * hook runs in.
 *
 * build: see build_mousespy.sh
 */
#include <windows.h>

static HINSTANCE dll_instance;

static int StrStrIA_local( const char *hay, const char *needle )
{
    for (; *hay; hay++) {
        const char *a = hay, *b = needle;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca>='A'&&ca<='Z') ca += 32;
            if (cb>='A'&&cb<='Z') cb += 32;
            if (ca != cb) break;
            a++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static void logline( const char *s )
{
    HANDLE f = CreateFileA( "Z:\\tmp\\mousespy.txt", FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    DWORD n;
    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile( f, s, lstrlenA( s ), &n, NULL );
    CloseHandle( f );
}

/* ---- one-shot in-process inspection of JUCE windows' wndprocs ---- */

static void module_of( ULONG_PTR addr, char *out, int outlen )
{
    MEMORY_BASIC_INFORMATION mbi;
    out[0] = 0;
    if (VirtualQuery( (void *)addr, &mbi, sizeof(mbi) ) && mbi.AllocationBase)
        GetModuleFileNameA( (HMODULE)mbi.AllocationBase, out, outlen );
}

static BOOL CALLBACK inspect_cb( HWND h, LPARAM lp )
{
    char cls[80];
    GetClassNameA( h, cls, sizeof cls );
    if (!(cls[0]=='J' && cls[1]=='U' && cls[2]=='C' && cls[3]=='E')) return TRUE;
    {
        ULONG_PTR wp = (ULONG_PTR)GetWindowLongPtrW( h, GWLP_WNDPROC );
        ULONG_PTR cp = (ULONG_PTR)GetClassLongPtrW( h, GCLP_WNDPROC );
        char m1[200], m2[200], buf[640];
        module_of( wp, m1, sizeof m1 );
        module_of( cp, m2, sizeof m2 );
        wsprintfA( buf, "INSPECT hwnd=%p cls='%s' wndproc=%p [%s] classproc=%p [%s]\n",
                   h, cls, (void *)wp, m1, (void *)cp, m2 );
        logline( buf );
    }
    EnumChildWindows( h, inspect_cb, 0 );
    return TRUE;
}

static BOOL CALLBACK inspect_top_cb( HWND h, LPARAM lp )
{
    inspect_cb( h, 0 );
    EnumChildWindows( h, inspect_cb, 0 );
    return TRUE;
}

static void inspect_once( void )
{
    static LONG done;
    char exe[200] = "";
    if (InterlockedExchange( &done, 1 )) return;
    GetModuleFileNameA( NULL, exe, sizeof exe );
    /* only in Ableton's process */
    if (StrStrIA_local( exe, "Ableton Live" ))
    {
        char buf[512];
        WNDCLASSEXW wcx;
        wsprintfA( buf, "INSPECT-START pid=%u exe=%s\n", (unsigned)GetCurrentProcessId(), exe );
        logline( buf );

        memset( &wcx, 0, sizeof(wcx) );
        wcx.cbSize = sizeof(wcx);
        if (GetClassInfoExW( NULL, MAKEINTRESOURCEW(32768), &wcx ))
        {
            char m[200];
            module_of( (ULONG_PTR)wcx.lpfnWndProc, m, sizeof m );
            wsprintfA( buf, "MENUCLASS #32768: proc=%p [%s] style=%08x cbWndExtra=%d\n",
                       (void *)wcx.lpfnWndProc, m, (UINT)wcx.style, wcx.cbWndExtra );
        }
        else
            wsprintfA( buf, "MENUCLASS #32768: GetClassInfoExW FAILED err=%u\n",
                       (unsigned)GetLastError() );
        logline( buf );

        EnumWindows( inspect_top_cb, 0 );
    }
}

/* ---- wndproc subclass: ground truth on what actually reaches the target ---- */

#define MAX_SUB 8
static struct { HWND hwnd; WNDPROC orig; } subs[MAX_SUB];
static LONG sub_count;

static WNDPROC find_orig( HWND hwnd )
{
    int i;
    for (i = 0; i < sub_count; i++) if (subs[i].hwnd == hwnd) return subs[i].orig;
    return NULL;
}

static LRESULT CALLBACK subclass_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    WNDPROC orig = find_orig( hwnd );
    LRESULT ret;
    BOOL log = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEMOVE) ||
               msg == WM_MOUSEACTIVATE || msg == WM_ACTIVATE || msg == WM_SETFOCUS ||
               msg == WM_CAPTURECHANGED || msg == WM_NCHITTEST;
    static LONG hitcount;
    if (msg == WM_NCHITTEST && (InterlockedIncrement( &hitcount ) % 16) != 0) log = FALSE;

    if (!orig) return DefWindowProcW( hwnd, msg, wp, lp );
    ret = CallWindowProcW( orig, hwnd, msg, wp, lp );
    if (log)
    {
        char buf[192];
        wsprintfA( buf, "WNDPROC hwnd=%p msg=%04x wp=%Ix lp=%Ix -> %Id\n",
                   hwnd, msg, (ULONG_PTR)wp, (ULONG_PTR)lp, (LONG_PTR)ret );
        logline( buf );
    }
    return ret;
}

static void try_subclass( HWND hwnd )
{
    int i;
    char buf[128];
    WNDPROC orig;
    if (sub_count >= MAX_SUB) return;
    for (i = 0; i < sub_count; i++) if (subs[i].hwnd == hwnd) return;
    orig = (WNDPROC)GetWindowLongPtrW( hwnd, GWLP_WNDPROC );
    if (!orig || orig == subclass_proc) return;
    subs[sub_count].hwnd = hwnd;
    subs[sub_count].orig = orig;
    InterlockedIncrement( &sub_count );
    SetWindowLongPtrW( hwnd, GWLP_WNDPROC, (LONG_PTR)subclass_proc );
    wsprintfA( buf, "SUBCLASSED hwnd=%p orig=%p\n", hwnd, orig );
    logline( buf );
}

LRESULT CALLBACK mouse_hook( int code, WPARAM wparam, LPARAM lparam )
{
    if (code >= 0)
    {
        MOUSEHOOKSTRUCT *mh = (MOUSEHOOKSTRUCT *)lparam;
        UINT msg = (UINT)wparam;
        /* only log button events and a trickle of moves to keep the file sane */
        static LONG move_count;
        BOOL is_move = (msg == WM_MOUSEMOVE || msg == WM_NCMOUSEMOVE);
        char cls[64] = "";
        GetClassNameA( mh->hwnd, cls, sizeof cls );

        inspect_once();
        if (cls[0] == 'J' && cls[1] == 'U' && cls[2] == 'C' && cls[3] == 'E')
            try_subclass( mh->hwnd );

        if (!is_move || (InterlockedIncrement( &move_count ) % 32 == 0))
        {
            char buf[256];
            wsprintfA( buf, "pid=%u msg=%04x hwnd=%p cls='%s' pt=(%d,%d) ht=%d\n",
                       (unsigned)GetCurrentProcessId(), msg, mh->hwnd, cls,
                       (int)mh->pt.x, (int)mh->pt.y, (int)mh->wHitTestCode );
            logline( buf );
        }
    }
    return CallNextHookEx( NULL, code, wparam, lparam );
}

static const char *msg_name( UINT m )
{
    switch (m) {
        case WM_MOUSEACTIVATE: return "WM_MOUSEACTIVATE";
        case WM_ACTIVATE:      return "WM_ACTIVATE";
        case WM_NCACTIVATE:    return "WM_NCACTIVATE";
        case WM_SETFOCUS:      return "WM_SETFOCUS";
        case WM_KILLFOCUS:     return "WM_KILLFOCUS";
        case WM_ACTIVATEAPP:   return "WM_ACTIVATEAPP";
        case WM_PARENTNOTIFY:  return "WM_PARENTNOTIFY";
        default: return NULL;
    }
}

LRESULT CALLBACK cwp_hook( int code, WPARAM wparam, LPARAM lparam )
{
    if (code >= 0)
    {
        CWPSTRUCT *cs = (CWPSTRUCT *)lparam;
        const char *n = msg_name( cs->message );
        if (n)
        {
            char buf[256]; char cls[64] = "";
            GetClassNameA( cs->hwnd, cls, sizeof cls );
            wsprintfA( buf, "pid=%u SEND %s hwnd=%p cls='%s' wp=%Ix lp=%Ix\n",
                       (unsigned)GetCurrentProcessId(), n, cs->hwnd, cls,
                       (ULONG_PTR)cs->wParam, (ULONG_PTR)cs->lParam );
            logline( buf );
        }
    }
    return CallNextHookEx( NULL, code, wparam, lparam );
}

LRESULT CALLBACK cwpret_hook( int code, WPARAM wparam, LPARAM lparam )
{
    if (code >= 0)
    {
        CWPRETSTRUCT *cs = (CWPRETSTRUCT *)lparam;
        const char *n = msg_name( cs->message );
        if (n)
        {
            char buf[256]; char cls[64] = "";
            GetClassNameA( cs->hwnd, cls, sizeof cls );
            wsprintfA( buf, "pid=%u RET  %s hwnd=%p cls='%s' -> %Id\n",
                       (unsigned)GetCurrentProcessId(), n, cs->hwnd, cls,
                       (LONG_PTR)cs->lResult );
            logline( buf );
        }
    }
    return CallNextHookEx( NULL, code, wparam, lparam );
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HMODULE self;
        dll_instance = inst;
        DisableThreadLibraryCalls( inst );
        /* pin: the subclassed wndprocs must survive hook removal */
        GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            (const WCHAR *)DllMain, &self );
    }
    return TRUE;
}
