/* xmon.c — passive X focus/property monitor (Linux side, connects to XWayland).
 *
 * Watches, without disturbing anything:
 *  - FocusIn/FocusOut on a target window (any client may select FocusChangeMask)
 *  - StructureNotify on the target (map/unmap/destroy)
 *  - PropertyNotify on the root (_NET_ACTIVE_WINDOW changes)
 *  - the current XGetInputFocus + _NET_ACTIVE_WINDOW once at startup
 *
 * usage: xmon <hex-window-id> [seconds]
 * build: gcc -O2 -o xmon xmon.c -lX11
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

static double now_ms( void )
{
    struct timeval tv; gettimeofday( &tv, NULL );
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static const char *focus_mode( int m )
{
    switch (m) {
        case NotifyNormal: return "Normal";
        case NotifyGrab: return "Grab";
        case NotifyUngrab: return "Ungrab";
        case NotifyWhileGrabbed: return "WhileGrabbed";
        default: return "?";
    }
}
static const char *focus_detail( int d )
{
    static const char *names[] = { "Ancestor", "Virtual", "Inferior", "Nonlinear",
                                   "NonlinearVirtual", "Pointer", "PointerRoot", "None" };
    return (d >= 0 && d <= 7) ? names[d] : "?";
}

int main( int argc, char **argv )
{
    Display *dpy = XOpenDisplay( NULL );
    Window target, root, active_ret;
    Atom net_active, actual;
    int seconds = 15, fmt, revert;
    unsigned long n, after;
    unsigned char *prop = NULL;
    double t0;

    if (!dpy) { fprintf( stderr, "no display\n" ); return 1; }
    if (argc < 2) { fprintf( stderr, "usage: xmon <hexwin> [secs]\n" ); return 1; }
    target = (Window)strtoul( argv[1], NULL, 16 );
    if (argc > 2) seconds = atoi( argv[2] );

    root = DefaultRootWindow( dpy );
    net_active = XInternAtom( dpy, "_NET_ACTIVE_WINDOW", False );

    XGetInputFocus( dpy, &active_ret, &revert );
    printf( "start: XGetInputFocus=0x%lx revert=%d\n", (unsigned long)active_ret, revert );
    if (XGetWindowProperty( dpy, root, net_active, 0, 1, False, XA_WINDOW,
                            &actual, &fmt, &n, &after, &prop ) == Success && prop)
    {
        printf( "start: _NET_ACTIVE_WINDOW=0x%lx\n", *(unsigned long *)prop );
        XFree( prop );
    }

    XSelectInput( dpy, target, FocusChangeMask | StructureNotifyMask | PropertyChangeMask );
    XSelectInput( dpy, root, PropertyChangeMask );
    XSync( dpy, False );
    printf( "monitoring 0x%lx for %d s...\n", (unsigned long)target, seconds );
    fflush( stdout );

    t0 = now_ms();
    while (now_ms() - t0 < seconds * 1000.0)
    {
        XEvent ev;
        while (XPending( dpy ))
        {
            XNextEvent( dpy, &ev );
            double t = now_ms() - t0;
            switch (ev.type)
            {
            case FocusIn:
                printf( "%7.1f  FocusIn  0x%lx mode=%s detail=%s\n", t,
                        (unsigned long)ev.xfocus.window, focus_mode( ev.xfocus.mode ),
                        focus_detail( ev.xfocus.detail ) );
                break;
            case FocusOut:
                printf( "%7.1f  FocusOut 0x%lx mode=%s detail=%s\n", t,
                        (unsigned long)ev.xfocus.window, focus_mode( ev.xfocus.mode ),
                        focus_detail( ev.xfocus.detail ) );
                break;
            case PropertyNotify:
                if (ev.xproperty.atom == net_active)
                {
                    unsigned long v = 0;
                    if (XGetWindowProperty( dpy, root, net_active, 0, 1, False, XA_WINDOW,
                                            &actual, &fmt, &n, &after, &prop ) == Success && prop)
                    { v = *(unsigned long *)prop; XFree( prop ); }
                    printf( "%7.1f  _NET_ACTIVE_WINDOW -> 0x%lx\n", t, v );
                }
                else if (ev.xproperty.window == target)
                {
                    char *name = XGetAtomName( dpy, ev.xproperty.atom );
                    printf( "%7.1f  Prop on target: %s %s\n", t, name ? name : "?",
                            ev.xproperty.state == PropertyDelete ? "(deleted)" : "" );
                    if (name) XFree( name );
                }
                break;
            case MapNotify:    printf( "%7.1f  MapNotify 0x%lx\n", t, (unsigned long)ev.xmap.window ); break;
            case UnmapNotify:  printf( "%7.1f  UnmapNotify 0x%lx\n", t, (unsigned long)ev.xunmap.window ); break;
            case DestroyNotify:printf( "%7.1f  DestroyNotify 0x%lx\n", t, (unsigned long)ev.xdestroywindow.window ); break;
            case ConfigureNotify: break; /* noisy, skip */
            default: break;
            }
            fflush( stdout );
        }
        usleep( 20000 );
    }

    XGetInputFocus( dpy, &active_ret, &revert );
    printf( "end: XGetInputFocus=0x%lx\n", (unsigned long)active_ret );
    XCloseDisplay( dpy );
    return 0;
}
