/* xdmg — XDamage monitor: log damage rects on a window (and children region)
 * usage: xdmg 0xWINDOWID SECONDS
 * prints one line per damage event: time  x,y wxh
 * build: gcc -O2 -o xdmg xdmg.c -lX11 -lXdamage -lXfixes
 */
#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

static double now(void)
{
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: xdmg 0xWIN SECONDS\n"); return 1; }
    Display *d = XOpenDisplay(NULL);
    if (!d) return 1;
    Window w = strtoul(argv[1], NULL, 0);
    double dur = atof(argv[2]);

    int ev_base, err_base;
    if (!XDamageQueryExtension(d, &ev_base, &err_base)) { fprintf(stderr, "no XDamage\n"); return 1; }
    XDamageCreate(d, w, XDamageReportRawRectangles);

    double t0 = now();
    while (now() - t0 < dur)
    {
        while (XPending(d))
        {
            XEvent e; XNextEvent(d, &e);
            if (e.type == ev_base + XDamageNotify)
            {
                XDamageNotifyEvent *de = (XDamageNotifyEvent *)&e;
                printf("%8.3f  %d,%d %dx%d\n", now() - t0,
                       de->area.x, de->area.y, de->area.width, de->area.height);
                fflush(stdout);
            }
        }
        usleep(5000);
    }
    return 0;
}
