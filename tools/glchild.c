/* glchild.c — reproduce baseview's WGL context creation on a child window (PE).
 *
 * baseview (Rust, used by Warmth.dev "Reel Deal" via nih-plug) creates its GL
 * context like this (src/gl/win.rs):
 *   1. dummy window + legacy PFD/ChoosePixelFormat/SetPixelFormat +
 *      wglCreateContext to bootstrap wglGetProcAddress
 *   2. loads wglChoosePixelFormatARB / wglCreateContextAttribsARB /
 *      wglSwapIntervalEXT
 *   3. on the real (child) editor window: wglChoosePixelFormatARB,
 *      SetPixelFormat, wglCreateContextAttribsARB (core profile)
 * Any failure => panic "Could not create OpenGL context: CreationFailed(())"
 * which kills the host. This test runs the same sequence on a WS_CHILD window
 * and logs each step. Output: glchild.txt.  build: like the other PE tools +
 * -lgdi32 -lopengl32.
 */
#include <windows.h>
#include <wingdi.h>

static HANDLE g_out;
static char buf[512];
static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

#define WGL_DRAW_TO_WINDOW_ARB    0x2001
#define WGL_ACCELERATION_ARB      0x2003
#define WGL_SUPPORT_OPENGL_ARB    0x2010
#define WGL_DOUBLE_BUFFER_ARB     0x2011
#define WGL_PIXEL_TYPE_ARB        0x2013
#define WGL_COLOR_BITS_ARB        0x2014
#define WGL_ALPHA_BITS_ARB        0x201B
#define WGL_DEPTH_BITS_ARB        0x2022
#define WGL_STENCIL_BITS_ARB      0x2023
#define WGL_FULL_ACCELERATION_ARB 0x2027
#define WGL_TYPE_RGBA_ARB         0x202B
#define WGL_SAMPLE_BUFFERS_ARB    0x2041
#define WGL_SAMPLES_ARB           0x2042
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001

typedef HGLRC (WINAPI *pCreateCtxAttribs)( HDC, HGLRC, const int * );
typedef BOOL  (WINAPI *pChoosePixelFmt)( HDC, const int *, const FLOAT *, UINT, int *, UINT * );

static LRESULT CALLBACK wndproc( HWND h, UINT m, WPARAM w, LPARAM l )
{ return DefWindowProcW( h, m, w, l ); }

static void try_context( HDC hdc, pCreateCtxAttribs create_attribs, int major, int minor )
{
    const int ctx_attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, major,
        WGL_CONTEXT_MINOR_VERSION_ARB, minor,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };
    HGLRC ctx = create_attribs( hdc, NULL, ctx_attribs );
    P( "  wglCreateContextAttribsARB %d.%d core -> %p (err=%u)\n",
       major, minor, (void *)ctx, (unsigned)GetLastError() );
    if (ctx)
    {
        BOOL cur = wglMakeCurrent( hdc, ctx );
        P( "  wglMakeCurrent -> %d\n", cur );
        if (cur) wglMakeCurrent( NULL, NULL );
        wglDeleteContext( ctx );
    }
}

void mainCRTStartup( void )
{
    WNDCLASSW wc;
    HWND top, child, dummy;
    HDC dummy_dc, child_dc;
    PIXELFORMATDESCRIPTOR pfd;
    int fmt;
    HGLRC dummy_ctx;
    pCreateCtxAttribs create_attribs = NULL;
    pChoosePixelFmt choose_fmt = NULL;
    HINSTANCE inst = GetModuleHandleW( NULL );

    g_out = CreateFileA( "glchild.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );

    /* mimic Ableton: pm-v2 process (IFEO dpiAwareness=2), editor windows
     * created from a DPI-UNAWARE thread context (VST3 host trick) */
    {
        typedef BOOL (WINAPI *pSetP)(DPI_AWARENESS_CONTEXT);
        typedef DPI_AWARENESS_CONTEXT (WINAPI *pSetT)(DPI_AWARENESS_CONTEXT);
        HMODULE u = GetModuleHandleA( "user32.dll" );
        pSetP setp = (pSetP)GetProcAddress( u, "SetProcessDpiAwarenessContext" );
        pSetT sett = (pSetT)GetProcAddress( u, "SetThreadDpiAwarenessContext" );
        if (setp) P( "SetProcessDpiAwarenessContext(PMv2) -> %d\n",
                     setp( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ) );
        if (sett) P( "SetThreadDpiAwarenessContext(UNAWARE) -> %p\n",
                     (void *)sett( DPI_AWARENESS_CONTEXT_UNAWARE ) );
    }

    memset( &wc, 0, sizeof(wc) );
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = L"glchildtest";
    RegisterClassW( &wc );

    top = CreateWindowExW( 0, L"glchildtest", L"gl top", WS_OVERLAPPEDWINDOW,
                           100, 100, 640, 480, NULL, NULL, inst, NULL );
    child = CreateWindowExW( 0, L"glchildtest", L"gl child",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             0, 0, 600, 400, top, NULL, inst, NULL );
    ShowWindow( top, SW_SHOWNOACTIVATE );
    P( "top=%p child=%p\n", top, child );

    /* --- step 1: dummy window bootstrap (baseview does exactly this) --- */
    dummy = CreateWindowExW( 0, L"glchildtest", L"dummy", WS_OVERLAPPEDWINDOW,
                             0, 0, 16, 16, NULL, NULL, inst, NULL );
    dummy_dc = GetDC( dummy );
    memset( &pfd, 0, sizeof(pfd) );
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    fmt = ChoosePixelFormat( dummy_dc, &pfd );
    P( "dummy ChoosePixelFormat -> %d\n", fmt );
    P( "dummy SetPixelFormat -> %d\n", SetPixelFormat( dummy_dc, fmt, &pfd ) );
    dummy_ctx = wglCreateContext( dummy_dc );
    P( "dummy wglCreateContext -> %p (err=%u)\n", (void *)dummy_ctx, (unsigned)GetLastError() );
    if (dummy_ctx && wglMakeCurrent( dummy_dc, dummy_ctx ))
    {
        create_attribs = (pCreateCtxAttribs)wglGetProcAddress( "wglCreateContextAttribsARB" );
        choose_fmt = (pChoosePixelFmt)wglGetProcAddress( "wglChoosePixelFormatARB" );
        P( "ARB loaders: create_attribs=%p choose_fmt=%p\n",
           (void *)create_attribs, (void *)choose_fmt );
        wglMakeCurrent( NULL, NULL );
    }
    else P( "dummy MakeCurrent FAILED err=%u\n", (unsigned)GetLastError() );
    if (dummy_ctx) wglDeleteContext( dummy_ctx );
    ReleaseDC( dummy, dummy_dc );
    DestroyWindow( dummy );

    if (!create_attribs || !choose_fmt)
    {
        P( "FATAL: ARB entry points missing\n" );
        CloseHandle( g_out );
        ExitProcess( 2 );
    }

    /* --- step 2: real (child) window, ARB pixel format + core context --- */
    child_dc = GetDC( child );
    {
        const int fmt_attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, 1,
            WGL_SUPPORT_OPENGL_ARB, 1,
            WGL_DOUBLE_BUFFER_ARB, 1,
            WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
            WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
            WGL_COLOR_BITS_ARB, 32,
            WGL_ALPHA_BITS_ARB, 8,
            WGL_DEPTH_BITS_ARB, 24,
            WGL_STENCIL_BITS_ARB, 8,
            0
        };
        int formats[8];
        UINT count = 0;
        BOOL ok = choose_fmt( child_dc, fmt_attribs, NULL, 8, formats, &count );
        P( "child wglChoosePixelFormatARB -> %d count=%u fmt0=%d (err=%u)\n",
           ok, count, count ? formats[0] : -1, (unsigned)GetLastError() );
        if (ok && count)
        {
            PIXELFORMATDESCRIPTOR tmp;
            DescribePixelFormat( child_dc, formats[0], sizeof(tmp), &tmp );
            P( "child SetPixelFormat(%d) -> %d (err=%u)\n", formats[0],
               SetPixelFormat( child_dc, formats[0], &tmp ), (unsigned)GetLastError() );
            P( "child GetPixelFormat readback -> %d\n", GetPixelFormat( child_dc ) );
        }
    }
    try_context( child_dc, create_attribs, 3, 2 );
    try_context( child_dc, create_attribs, 3, 3 );
    try_context( child_dc, create_attribs, 4, 1 );

    /* --- step 3: baseview's exact GlConfig-default attribs (incl. sRGB) --- */
    {
        #define WGL_RED_BITS_ARB   0x2015
        #define WGL_GREEN_BITS_ARB 0x2017
        #define WGL_BLUE_BITS_ARB  0x2019
        #define WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB 0x20A9
        const int bv_attribs[] = {
            WGL_DRAW_TO_WINDOW_ARB, 1,
            WGL_SUPPORT_OPENGL_ARB, 1,
            WGL_DOUBLE_BUFFER_ARB, 1,
            WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
            WGL_RED_BITS_ARB, 8,
            WGL_GREEN_BITS_ARB, 8,
            WGL_BLUE_BITS_ARB, 8,
            WGL_ALPHA_BITS_ARB, 8,
            WGL_DEPTH_BITS_ARB, 24,
            WGL_STENCIL_BITS_ARB, 8,
            WGL_SAMPLE_BUFFERS_ARB, 0,
            WGL_SAMPLES_ARB, 0,
            WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, 1,
            0
        };
        int f = 0; UINT n = 0;
        BOOL ok = choose_fmt( child_dc, bv_attribs, NULL, 1, &f, &n );
        P( "baseview-attribs choose -> ok=%d count=%u fmt=%d\n", ok, n, f );
        /* same without sRGB */
        {
            int attrs2[64]; int i;
            for (i = 0; i < (int)(sizeof(bv_attribs)/sizeof(int)); i++) attrs2[i] = bv_attribs[i];
            attrs2[24] = 0; /* truncate before SRGB pair */
            ok = choose_fmt( child_dc, attrs2, NULL, 1, &f, &n );
            P( "baseview-attribs-noSRGB choose -> ok=%d count=%u fmt=%d\n", ok, n, f );
        }
    }

    ReleaseDC( child, child_dc );
    P( "done\n" );
    CloseHandle( g_out );
    ExitProcess( 0 );
}
