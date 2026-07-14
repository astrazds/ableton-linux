/* xsamp — sample one pixel of a window over time, log changes
 * usage: xsamp 0xWINDOWID X Y SECONDS [intervalms]
 * prints a line whenever the sampled pixel changes: time  rrggbb
 * build: gcc -O2 -o xsamp xsamp.c -lX11
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
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
    if (argc < 5) { fprintf(stderr, "usage: xsamp 0xWIN X Y SECONDS [ms]\n"); return 1; }
    Display *d = XOpenDisplay(NULL);
    if (!d) return 1;
    Window w = strtoul(argv[1], NULL, 0);
    int x = atoi(argv[2]), y = atoi(argv[3]);
    double dur = atof(argv[4]);
    int ms = argc > 5 ? atoi(argv[5]) : 20;

    unsigned long prev = 0xdeadbeef;
    double t0 = now();
    while (now() - t0 < dur)
    {
        XImage *img = XGetImage(d, w, x, y, 1, 1, AllPlanes, ZPixmap);
        if (img)
        {
            unsigned long px = XGetPixel(img, 0, 0) & 0xffffff;
            if (px != prev)
            {
                printf("%8.3f  %06lx\n", now() - t0, px);
                fflush(stdout);
                prev = px;
            }
            XDestroyImage(img);
        }
        usleep(ms * 1000);
    }
    return 0;
}
