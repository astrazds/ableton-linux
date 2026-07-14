/* xgrid — sample an NxM grid over a window rect, report changing points */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
static double now(void){ struct timeval tv; gettimeofday(&tv,NULL); return tv.tv_sec+tv.tv_usec/1e6; }
int main(int argc,char**argv){
    /* xgrid 0xWIN x y w h seconds */
    Display*d=XOpenDisplay(NULL); if(!d)return 1;
    Window w=strtoul(argv[1],0,0);
    int rx=atoi(argv[2]),ry=atoi(argv[3]),rw=atoi(argv[4]),rh=atoi(argv[5]);
    double dur=atof(argv[6]);
    enum { GX=12, GY=8 };
    unsigned long prev[GX*GY]; int changes[GX*GY]; unsigned long vals[GX*GY][2];
    for(int i=0;i<GX*GY;i++){prev[i]=0xdeadbeef;changes[i]=-1;vals[i][0]=vals[i][1]=0xdeadbeef;}
    double t0=now();
    while(now()-t0<dur){
        XImage*img=XGetImage(d,w,rx,ry,rw,rh,AllPlanes,ZPixmap);
        if(img){
            for(int gy=0;gy<GY;gy++)for(int gx=0;gx<GX;gx++){
                int i=gy*GX+gx;
                unsigned long px=XGetPixel(img,gx*rw/GX+rw/GX/2,gy*rh/GY+rh/GY/2)&0xffffff;
                if(px!=prev[i]){
                    changes[i]++;
                    if(vals[i][0]==0xdeadbeef)vals[i][0]=px;
                    else if(px!=vals[i][0]&&vals[i][1]==0xdeadbeef)vals[i][1]=px;
                    prev[i]=px;
                }
            }
            XDestroyImage(img);
        }
        usleep(15000);
    }
    for(int gy=0;gy<GY;gy++){
        for(int gx=0;gx<GX;gx++){
            int i=gy*GX+gx;
            printf("%4d",changes[i]);
        }
        printf("\n");
    }
    printf("hot cells (changes, two states):\n");
    for(int i=0;i<GX*GY;i++)
        if(changes[i]>3)
            printf("  cell (%d,%d) px(%d,%d): %d changes, %06lx <-> %06lx\n",
                   i%GX,i/GX, rx+(i%GX)*rw/GX+rw/GX/2, ry+(i/GX)*rh/GY+rh/GY/2,
                   changes[i], vals[i][0], vals[i][1]);
    return 0;
}
