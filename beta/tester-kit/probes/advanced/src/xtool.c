/* xtool — minimal XTEST injector for driving X clients under XWayland.
 * usage:
 *   xtool move X Y            warp pointer to (X,Y) physical X coords
 *   xtool click X Y [btn]     move + click (default button 1)
 *   xtool dblclick X Y        move + double click
 *   xtool drag X1 Y1 X2 Y2    press at (X1,Y1), glide to (X2,Y2), release
 *   xtool key KEYSYM_NAME     press+release a key by keysym name (e.g. Return)
 */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *dpy;

static void move( int x, int y ) { XTestFakeMotionEvent( dpy, -1, x, y, CurrentTime ); XFlush( dpy ); usleep( 80000 ); }
static void btn( int b, int down ) { XTestFakeButtonEvent( dpy, b, down, CurrentTime ); XFlush( dpy ); usleep( 80000 ); }

int main( int argc, char **argv )
{
    if (argc < 2) return 1;
    if (!(dpy = XOpenDisplay( NULL ))) { fprintf( stderr, "no display\n" ); return 1; }

    if (!strcmp( argv[1], "move" ) && argc == 4)
        move( atoi( argv[2] ), atoi( argv[3] ) );
    else if (!strcmp( argv[1], "click" ) && argc >= 4)
    {
        int b = argc > 4 ? atoi( argv[4] ) : 1;
        move( atoi( argv[2] ), atoi( argv[3] ) );
        btn( b, 1 ); btn( b, 0 );
    }
    else if (!strcmp( argv[1], "dblclick" ) && argc == 4)
    {
        move( atoi( argv[2] ), atoi( argv[3] ) );
        btn( 1, 1 ); btn( 1, 0 );
        usleep( 60000 );
        btn( 1, 1 ); btn( 1, 0 );
    }
    else if (!strcmp( argv[1], "drag" ) && argc == 6)
    {
        int x1 = atoi( argv[2] ), y1 = atoi( argv[3] ), x2 = atoi( argv[4] ), y2 = atoi( argv[5] ), i;
        move( x1, y1 );
        btn( 1, 1 );
        for (i = 1; i <= 20; i++) { XTestFakeMotionEvent( dpy, -1, x1 + (x2 - x1) * i / 20, y1 + (y2 - y1) * i / 20, CurrentTime ); XFlush( dpy ); usleep( 25000 ); }
        btn( 1, 0 );
    }
    else if (!strcmp( argv[1], "key" ) && argc == 3)
    {
        KeySym ks = XStringToKeysym( argv[2] );
        KeyCode kc = XKeysymToKeycode( dpy, ks );
        if (!kc) { fprintf( stderr, "bad key\n" ); return 1; }
        XTestFakeKeyEvent( dpy, kc, True, CurrentTime ); XFlush( dpy ); usleep( 50000 );
        XTestFakeKeyEvent( dpy, kc, False, CurrentTime ); XFlush( dpy );
    }
    else { fprintf( stderr, "bad args\n" ); return 1; }

    XCloseDisplay( dpy );
    return 0;
}
