/* xact.c — X activation prods (Linux side, connects to XWayland).
 *
 * usage:
 *   xact activate <hexwin>    ask the WM to activate (root _NET_ACTIVE_WINDOW
 *                             ClientMessage, source=2/pager, fresh timestamp)
 *   xact takefocus <hexwin>   send WM_TAKE_FOCUS directly to the window with a
 *                             fresh timestamp (as the WM would)
 *   xact clients              walk root children, list windows + basic info
 *                             (identifies propertyless focus holders)
 *
 * build: gcc -O2 -o xact xact.c -lX11
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

/* get a fresh server timestamp via a property-change round trip */
static Time fresh_time( Display *dpy )
{
    Window w = XCreateSimpleWindow( dpy, DefaultRootWindow( dpy ), -100, -100, 1, 1, 0, 0, 0 );
    Atom a = XInternAtom( dpy, "XACT_TS", False );
    XEvent ev;
    XSelectInput( dpy, w, PropertyChangeMask );
    XChangeProperty( dpy, w, a, XA_STRING, 8, PropModeAppend, (unsigned char *)"", 0 );
    XWindowEvent( dpy, w, PropertyChangeMask, &ev );
    XDestroyWindow( dpy, w );
    return ev.xproperty.time;
}

int main( int argc, char **argv )
{
    Display *dpy = XOpenDisplay( NULL );
    if (!dpy) { fprintf( stderr, "no display\n" ); return 1; }
    if (argc < 2) { fprintf( stderr, "usage: xact activate|takefocus <hexwin> | clients\n" ); return 1; }

    if (!strcmp( argv[1], "clients" ))
    {
        Window root = DefaultRootWindow( dpy ), parent, *kids;
        unsigned int n, i;
        if (XQueryTree( dpy, root, &root, &parent, &kids, &n ))
        {
            for (i = 0; i < n; i++)
            {
                XWindowAttributes wa;
                char *name = NULL;
                if (!XGetWindowAttributes( dpy, kids[i], &wa )) continue;
                XFetchName( dpy, kids[i], &name );
                printf( "0x%07lx %-9s %-8s %5dx%-5d +%d+%d ovr=%d '%s'\n",
                        (unsigned long)kids[i],
                        wa.map_state == IsViewable ? "viewable" :
                        wa.map_state == IsUnmapped ? "unmapped" : "unviewable",
                        wa.class == InputOnly ? "IN-ONLY" : "in-out",
                        wa.width, wa.height, wa.x, wa.y, wa.override_redirect,
                        name ? name : "" );
                if (name) XFree( name );
            }
            XFree( kids );
        }
        XCloseDisplay( dpy );
        return 0;
    }

    if (!strcmp( argv[1], "grabtest" ))
    {
        int rp = XGrabPointer( dpy, DefaultRootWindow( dpy ), False, ButtonPressMask,
                               GrabModeAsync, GrabModeAsync, None, None, CurrentTime );
        int rk = XGrabKeyboard( dpy, DefaultRootWindow( dpy ), False,
                                GrabModeAsync, GrabModeAsync, CurrentTime );
        const char *names[] = { "Success", "AlreadyGrabbed", "InvalidTime", "NotViewable", "Frozen" };
        printf( "pointer grab: %s\nkeyboard grab: %s\n",
                rp <= 4 ? names[rp] : "?", rk <= 4 ? names[rk] : "?" );
        if (rp == GrabSuccess) XUngrabPointer( dpy, CurrentTime );
        if (rk == GrabSuccess) XUngrabKeyboard( dpy, CurrentTime );
        XSync( dpy, False );
        XCloseDisplay( dpy );
        return 0;
    }

    if (argc < 3) { fprintf( stderr, "need window id\n" ); return 1; }
    Window target = (Window)strtoul( argv[2], NULL, 16 );
    Time ts = fresh_time( dpy );
    printf( "fresh timestamp: %lu\n", (unsigned long)ts );

    if (!strcmp( argv[1], "activate" ))
    {
        XEvent ev;
        memset( &ev, 0, sizeof(ev) );
        ev.xclient.type = ClientMessage;
        ev.xclient.window = target;
        ev.xclient.message_type = XInternAtom( dpy, "_NET_ACTIVE_WINDOW", False );
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = 2;   /* source: pager */
        ev.xclient.data.l[1] = ts;
        ev.xclient.data.l[2] = 0;
        XSendEvent( dpy, DefaultRootWindow( dpy ), False,
                    SubstructureRedirectMask | SubstructureNotifyMask, &ev );
        printf( "sent _NET_ACTIVE_WINDOW request for 0x%lx\n", (unsigned long)target );
    }
    else if (!strcmp( argv[1], "takefocus" ))
    {
        XEvent ev;
        memset( &ev, 0, sizeof(ev) );
        ev.xclient.type = ClientMessage;
        ev.xclient.window = target;
        ev.xclient.message_type = XInternAtom( dpy, "WM_PROTOCOLS", False );
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = XInternAtom( dpy, "WM_TAKE_FOCUS", False );
        ev.xclient.data.l[1] = ts;
        XSendEvent( dpy, target, False, NoEventMask, &ev );
        printf( "sent WM_TAKE_FOCUS to 0x%lx\n", (unsigned long)target );
    }
    XSync( dpy, False );
    XCloseDisplay( dpy );
    return 0;
}
