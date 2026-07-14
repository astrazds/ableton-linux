/* xrec.c — X protocol spy via the RECORD extension (Linux side, XWayland).
 *
 * Captures, across ALL clients:
 *  - core requests: SetInputFocus (opcode 42) with focus window + timestamp
 *  - delivered events: ClientMessage (33) [WM_TAKE_FOCUS etc.], FocusIn (9),
 *    FocusOut (10), ButtonPress (4), ButtonRelease (5), KeyPress (2)
 *
 * usage: xrec [seconds]     (default 10)
 * build: gcc -O2 -o xrec xrec.c -lX11 -lXtst
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/record.h>

static Display *ctl, *data_dpy;
static double t0;
static Atom wm_protocols, wm_take_focus;

static double now_ms( void )
{
    struct timeval tv; gettimeofday( &tv, NULL );
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* CARD32 fetch helpers (little-endian host, protocol byte order = host here) */
static unsigned long u32( const unsigned char *p ) { return *(const unsigned int *)p; }
static unsigned short u16( const unsigned char *p ) { return *(const unsigned short *)p; }

static void cb( XPointer closure, XRecordInterceptData *d )
{
    const unsigned char *p = (const unsigned char *)d->data;
    double t = now_ms() - t0;

    if (d->category == XRecordFromClient && d->data_len >= 3)
    {
        /* core request: opcode in byte 0 */
        if (p[0] == X_SetInputFocus)  /* 42: revert-to in byte 1, focus win @4, time @8 */
            printf( "%8.1f  REQ SetInputFocus win=0x%lx time=%lu revert=%u\n",
                    t, u32( p + 4 ), u32( p + 8 ), p[1] );
    }
    else if (d->category == XRecordFromServer && d->data_len >= 8)
    {
        int type = p[0] & 0x7f;
        switch (type)
        {
        case ClientMessage:  /* format @1, seq @2, window @4, type atom @8, data @12 */
        {
            Atom mtype = u32( p + 8 );
            if (mtype == wm_protocols)
            {
                Atom proto = u32( p + 12 );
                char *pn = XGetAtomName( ctl, proto );
                printf( "%8.1f  EVT ClientMessage win=0x%lx WM_PROTOCOLS %s time=%lu\n",
                        t, u32( p + 4 ), pn ? pn : "?", u32( p + 16 ) );
                if (pn) XFree( pn );
            }
            else
            {
                char *mn = XGetAtomName( ctl, mtype );
                printf( "%8.1f  EVT ClientMessage win=0x%lx type=%s\n",
                        t, u32( p + 4 ), mn ? mn : "?" );
                if (mn) XFree( mn );
            }
            break;
        }
        case FocusIn:
        case FocusOut:
            printf( "%8.1f  EVT %s win=0x%lx detail=%u mode=%u\n", t,
                    type == FocusIn ? "FocusIn " : "FocusOut", u32( p + 4 ), p[1], p[8] );
            break;
        case ButtonPress:
        case ButtonRelease:
            printf( "%8.1f  EVT %s btn=%u time=%lu root=(%d,%d) win=0x%lx child=0x%lx\n", t,
                    type == ButtonPress ? "ButtonPress  " : "ButtonRelease", p[1],
                    u32( p + 4 ), (short)u16( p + 20 ), (short)u16( p + 22 ),
                    u32( p + 12 ), u32( p + 16 ) );
            break;
        default: break;
        }
    }
    fflush( stdout );
    XRecordFreeData( d );
}

int main( int argc, char **argv )
{
    XRecordClientSpec spec = XRecordAllClients;
    XRecordRange *range;
    XRecordContext rc;
    int secs = argc > 1 ? atoi( argv[1] ) : 10;
    int major, minor;

    ctl = XOpenDisplay( NULL );
    data_dpy = XOpenDisplay( NULL );
    if (!ctl || !data_dpy) { fprintf( stderr, "no display\n" ); return 1; }
    if (!XRecordQueryVersion( ctl, &major, &minor ))
    { fprintf( stderr, "RECORD extension not available\n" ); return 1; }
    fprintf( stderr, "RECORD %d.%d\n", major, minor );

    wm_protocols = XInternAtom( ctl, "WM_PROTOCOLS", False );
    wm_take_focus = XInternAtom( ctl, "WM_TAKE_FOCUS", False );
    (void)wm_take_focus;

    range = XRecordAllocRange();
    memset( range, 0, sizeof(*range) );
    range->core_requests.first = X_SetInputFocus;
    range->core_requests.last  = X_SetInputFocus;
    range->delivered_events.first = KeyPress;      /* 2 */
    range->delivered_events.last  = ClientMessage; /* 33: covers all core events */

    rc = XRecordCreateContext( ctl, 0, &spec, 1, &range, 1 );
    if (!rc) { fprintf( stderr, "create context failed\n" ); return 1; }
    XSync( ctl, False );

    t0 = now_ms();
    if (!XRecordEnableContextAsync( data_dpy, rc, cb, NULL ))
    { fprintf( stderr, "enable failed\n" ); return 1; }

    while (now_ms() - t0 < secs * 1000.0)
    {
        XRecordProcessReplies( data_dpy );
        usleep( 15000 );
    }

    XRecordDisableContext( ctl, rc );
    XRecordFreeContext( ctl, rc );
    XCloseDisplay( ctl );
    XCloseDisplay( data_dpy );
    return 0;
}
