/* xsettle — drive + observe the Ableton main window settle bug.
 * usage:
 *   xsettle find                     print the Live toplevel X window id + geometry
 *   xsettle moveresize X Y W H       send _NET_MOVERESIZE_WINDOW (WM-side, like a tile/snap)
 *   xsettle poll SECONDS             poll geometry every 100ms, print only on change
 * build: gcc -O2 -o xsettle xsettle.c -lX11
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

static Display *dpy;

static double now(void)
{
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static const char *match_str(void)
{
    const char *m = getenv("XSETTLE_MATCH");
    return m && *m ? m : "bleton";
}

static Window find_live(void)
{
    Atom clist = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    Atom type; int fmt; unsigned long n, after; unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), clist, 0, 4096, False,
                           XA_WINDOW, &type, &fmt, &n, &after, &data) != Success || !data)
        return 0;
    Window *wins = (Window *)data, found = 0;
    for (unsigned long i = 0; i < n; i++)
    {
        XClassHint ch = {0};
        if (XGetClassHint(dpy, wins[i], &ch))
        {
            if ((ch.res_name && strstr(ch.res_name, match_str())) ||
                (ch.res_class && strstr(ch.res_class, match_str())))
            {
                /* prefer the biggest Ableton window (main, not splash) */
                XWindowAttributes wa;
                if (XGetWindowAttributes(dpy, wins[i], &wa) && wa.width > 600)
                    found = wins[i];
            }
            if (ch.res_name) XFree(ch.res_name);
            if (ch.res_class) XFree(ch.res_class);
        }
    }
    XFree(data);
    return found;
}

static void geom(Window w, int *x, int *y, unsigned *ww, unsigned *wh)
{
    Window root, child; unsigned bw, d; int rx, ry;
    XGetGeometry(dpy, w, &root, x, y, ww, wh, &bw, &d);
    XTranslateCoordinates(dpy, w, root, 0, 0, &rx, &ry, &child);
    *x = rx; *y = ry;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "no display\n"); return 1; }

    Window w = find_live();
    if (!w) { fprintf(stderr, "no Ableton window in _NET_CLIENT_LIST\n"); return 2; }

    if (!strcmp(argv[1], "find"))
    {
        int x, y; unsigned ww, wh;
        geom(w, &x, &y, &ww, &wh);
        printf("0x%lx %d %d %u %u\n", w, x, y, ww, wh);
    }
    else if (!strcmp(argv[1], "moveresize") && argc == 6)
    {
        Atom mr = XInternAtom(dpy, "_NET_MOVERESIZE_WINDOW", False);
        XEvent e = {0};
        e.xclient.type = ClientMessage;
        e.xclient.window = w;
        e.xclient.message_type = mr;
        e.xclient.format = 32;
        /* gravity=static(10) | x|y|w|h flags | source=pager(2) */
        e.xclient.data.l[0] = 10 | (0xf << 8) | (2 << 12);
        e.xclient.data.l[1] = atol(argv[2]);
        e.xclient.data.l[2] = atol(argv[3]);
        e.xclient.data.l[3] = atol(argv[4]);
        e.xclient.data.l[4] = atol(argv[5]);
        XSendEvent(dpy, DefaultRootWindow(dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &e);
        XFlush(dpy);
        printf("sent moveresize %s,%s %sx%s to 0x%lx\n",
               argv[2], argv[3], argv[4], argv[5], w);
    }
    else if (!strcmp(argv[1], "poll") && argc == 3)
    {
        double t0 = now(), dur = atof(argv[2]);
        int px = -99999, py = -99999; unsigned pw = 0, ph = 0;
        while (now() - t0 < dur)
        {
            int x, y; unsigned ww, wh;
            geom(w, &x, &y, &ww, &wh);
            if (x != px || y != py || ww != pw || wh != ph)
            {
                printf("%9.3f  %d,%d %ux%u\n", now() - t0, x, y, ww, wh);
                fflush(stdout);
                px = x; py = y; pw = ww; ph = wh;
            }
            usleep(100000);
        }
    }
    else return 1;
    return 0;
}
