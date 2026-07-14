/* uclick.c — real-input injector via /dev/uinput (Linux side).
 *
 * Creates a virtual pen-tablet (absolute coords, INPUT_PROP_DIRECT — mutter
 * maps it 1:1 onto the built-in display, no pointer acceleration) and a
 * virtual keyboard. Events travel the REAL input path:
 *   kernel → libinput → mutter (Wayland) → XWayland → winex11 → Wine apps
 * unlike Wine-internal SendInput (liveinject.c) which bypasses X delivery.
 *
 * Needs rw on /dev/uinput (theo has an ACL for it).
 *
 * Commands on stdin (one per line); coordinates are FRACTIONS 0..1 of the
 * screen (scale-agnostic; multiply physical X-space px by 1/4096, 1/2560):
 *   move <fx> <fy>         move pen (in proximity, hover)
 *   down / up              pen tip press / release at current pos
 *   click <fx> <fy>        move + tap
 *   dblclick <fx> <fy>     move + double tap
 *   key <spec>             e.g. "key ctrl+q", "key f", "key enter"
 *   keydown <name> / keyup <name>
 *   sleep <ms>
 *   quit                   destroy devices and exit (EOF works too)
 *
 * build: gcc -O2 -o uclick uclick.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/uinput.h>

static int pen_fd = -1, kbd_fd = -1, mouse_fd = -1;

static void emit_ev( int fd, int type, int code, int val )
{
    struct input_event ev;
    memset( &ev, 0, sizeof(ev) );
    ev.type = type; ev.code = code; ev.value = val;
    if (write( fd, &ev, sizeof(ev) ) != sizeof(ev) )
        perror( "write ev" );
}
static void syn( int fd ) { emit_ev( fd, EV_SYN, SYN_REPORT, 0 ); }

#define AXMAX 65535

static int make_pen( void )
{
    struct uinput_setup us;
    struct uinput_abs_setup as;
    int fd = open( "/dev/uinput", O_WRONLY | O_NONBLOCK );
    if (fd < 0) { perror( "open /dev/uinput" ); return -1; }

    ioctl( fd, UI_SET_EVBIT, EV_KEY );
    ioctl( fd, UI_SET_KEYBIT, BTN_TOOL_PEN );
    ioctl( fd, UI_SET_KEYBIT, BTN_TOUCH );
    ioctl( fd, UI_SET_KEYBIT, BTN_STYLUS );
    ioctl( fd, UI_SET_EVBIT, EV_ABS );
    ioctl( fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT );

    memset( &as, 0, sizeof(as) );
    as.code = ABS_X; as.absinfo.minimum = 0; as.absinfo.maximum = AXMAX; as.absinfo.resolution = 300;
    ioctl( fd, UI_ABS_SETUP, &as );
    as.code = ABS_Y;
    ioctl( fd, UI_ABS_SETUP, &as );
    memset( &as, 0, sizeof(as) );
    as.code = ABS_PRESSURE; as.absinfo.maximum = 4095;
    ioctl( fd, UI_ABS_SETUP, &as );

    memset( &us, 0, sizeof(us) );
    us.id.bustype = BUS_USB; us.id.vendor = 0x1234; us.id.product = 0x5678;
    strcpy( us.name, "uclick virtual pen" );
    if (ioctl( fd, UI_DEV_SETUP, &us ) < 0 || ioctl( fd, UI_DEV_CREATE ) < 0)
    { perror( "pen UI_DEV_CREATE" ); close( fd ); return -1; }
    return fd;
}

static int make_kbd( void )
{
    struct uinput_setup us;
    int i;
    int fd = open( "/dev/uinput", O_WRONLY | O_NONBLOCK );
    if (fd < 0) { perror( "open /dev/uinput" ); return -1; }

    ioctl( fd, UI_SET_EVBIT, EV_KEY );
    for (i = 1; i < 128; i++) ioctl( fd, UI_SET_KEYBIT, i );  /* all common keys */

    memset( &us, 0, sizeof(us) );
    us.id.bustype = BUS_USB; us.id.vendor = 0x1234; us.id.product = 0x5679;
    strcpy( us.name, "uclick virtual keyboard" );
    if (ioctl( fd, UI_DEV_SETUP, &us ) < 0 || ioctl( fd, UI_DEV_CREATE ) < 0)
    { perror( "kbd UI_DEV_CREATE" ); close( fd ); return -1; }
    return fd;
}

static int make_mouse( void )
{
    struct uinput_setup us;
    int fd = open( "/dev/uinput", O_WRONLY | O_NONBLOCK );
    if (fd < 0) { perror( "open /dev/uinput" ); return -1; }

    ioctl( fd, UI_SET_EVBIT, EV_KEY );
    ioctl( fd, UI_SET_KEYBIT, BTN_LEFT );
    ioctl( fd, UI_SET_KEYBIT, BTN_RIGHT );
    ioctl( fd, UI_SET_KEYBIT, BTN_MIDDLE );
    ioctl( fd, UI_SET_EVBIT, EV_REL );
    ioctl( fd, UI_SET_RELBIT, REL_X );
    ioctl( fd, UI_SET_RELBIT, REL_Y );
    ioctl( fd, UI_SET_RELBIT, REL_WHEEL );

    memset( &us, 0, sizeof(us) );
    us.id.bustype = BUS_USB; us.id.vendor = 0x1234; us.id.product = 0x567a;
    strcpy( us.name, "uclick virtual mouse" );
    if (ioctl( fd, UI_DEV_SETUP, &us ) < 0 || ioctl( fd, UI_DEV_CREATE ) < 0)
    { perror( "mouse UI_DEV_CREATE" ); close( fd ); return -1; }
    return fd;
}

static void pen_move( double fx, double fy )
{
    emit_ev( pen_fd, EV_KEY, BTN_TOOL_PEN, 1 );
    emit_ev( pen_fd, EV_ABS, ABS_X, (int)(fx * AXMAX) );
    emit_ev( pen_fd, EV_ABS, ABS_Y, (int)(fy * AXMAX) );
    emit_ev( pen_fd, EV_ABS, ABS_PRESSURE, 0 );
    syn( pen_fd );
}
static void pen_down( void )
{
    emit_ev( pen_fd, EV_KEY, BTN_TOUCH, 1 );
    emit_ev( pen_fd, EV_ABS, ABS_PRESSURE, 3000 );
    syn( pen_fd );
}
static void pen_up( void )
{
    emit_ev( pen_fd, EV_KEY, BTN_TOUCH, 0 );
    emit_ev( pen_fd, EV_ABS, ABS_PRESSURE, 0 );
    syn( pen_fd );
}
static void msleep( int ms ) { usleep( ms * 1000 ); }

struct keymap { const char *name; int code; };
static const struct keymap KEYS[] = {
    { "ctrl", KEY_LEFTCTRL }, { "shift", KEY_LEFTSHIFT }, { "alt", KEY_LEFTALT },
    { "super", KEY_LEFTMETA }, { "enter", KEY_ENTER }, { "esc", KEY_ESC },
    { "tab", KEY_TAB }, { "space", KEY_SPACE }, { "backspace", KEY_BACKSPACE },
    { "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT },
    { "f1", KEY_F1 }, { "f2", KEY_F2 }, { "f4", KEY_F4 }, { "f10", KEY_F10 },
    { "a", KEY_A }, { "b", KEY_B }, { "c", KEY_C }, { "d", KEY_D }, { "e", KEY_E },
    { "f", KEY_F }, { "g", KEY_G }, { "h", KEY_H }, { "i", KEY_I }, { "j", KEY_J },
    { "k", KEY_K }, { "l", KEY_L }, { "m", KEY_M }, { "n", KEY_N }, { "o", KEY_O },
    { "p", KEY_P }, { "q", KEY_Q }, { "r", KEY_R }, { "s", KEY_S }, { "t", KEY_T },
    { "u", KEY_U }, { "v", KEY_V }, { "w", KEY_W }, { "x", KEY_X }, { "y", KEY_Y },
    { "z", KEY_Z },
    { "0", KEY_0 }, { "1", KEY_1 }, { "2", KEY_2 }, { "3", KEY_3 }, { "4", KEY_4 },
    { "5", KEY_5 }, { "6", KEY_6 }, { "7", KEY_7 }, { "8", KEY_8 }, { "9", KEY_9 },
    { NULL, 0 }
};
static int keycode( const char *name )
{
    int i;
    for (i = 0; KEYS[i].name; i++)
        if (!strcmp( KEYS[i].name, name )) return KEYS[i].code;
    return -1;
}

/* "ctrl+shift+t" → press mods+key, release in reverse */
static void key_combo( char *spec )
{
    int codes[8], n = 0, i;
    char *tok = strtok( spec, "+" );
    while (tok && n < 8)
    {
        int c = keycode( tok );
        if (c < 0) { fprintf( stderr, "unknown key '%s'\n", tok ); return; }
        codes[n++] = c;
        tok = strtok( NULL, "+" );
    }
    for (i = 0; i < n; i++) { emit_ev( kbd_fd, EV_KEY, codes[i], 1 ); syn( kbd_fd ); msleep( 30 ); }
    msleep( 50 );
    for (i = n - 1; i >= 0; i--) { emit_ev( kbd_fd, EV_KEY, codes[i], 0 ); syn( kbd_fd ); msleep( 30 ); }
}

int main( void )
{
    char line[256];

    pen_fd = make_pen();
    kbd_fd = make_kbd();
    mouse_fd = make_mouse();
    if (pen_fd < 0 || kbd_fd < 0 || mouse_fd < 0) return 1;

    /* give the compositor time to hotplug-detect the new devices */
    msleep( 700 );
    printf( "ready\n" ); fflush( stdout );

    while (fgets( line, sizeof(line), stdin ))
    {
        char cmd[32] = "", arg1[64] = "", arg2[64] = "";
        double fx, fy;
        line[strcspn( line, "\n" )] = 0;
        if (sscanf( line, "%31s %63s %63s", cmd, arg1, arg2 ) < 1) continue;

        if (!strcmp( cmd, "move" ) && arg2[0])
        {
            fx = atof( arg1 ); fy = atof( arg2 );
            pen_move( fx, fy ); msleep( 60 );
        }
        else if (!strcmp( cmd, "down" )) { pen_down(); msleep( 60 ); }
        else if (!strcmp( cmd, "up" ))   { pen_up();   msleep( 60 ); }
        else if ((!strcmp( cmd, "click" ) || !strcmp( cmd, "dblclick" )) && arg2[0])
        {
            fx = atof( arg1 ); fy = atof( arg2 );
            pen_move( fx, fy ); msleep( 150 );
            pen_down(); msleep( 90 ); pen_up();
            if (cmd[0] == 'd') { msleep( 90 ); pen_down(); msleep( 90 ); pen_up(); }
            msleep( 60 );
        }
        else if (!strcmp( cmd, "rel" ) && arg2[0])
        {   /* relative mouse move in device units (accel applies) */
            int dx = atoi( arg1 ), dy = atoi( arg2 );
            /* chunk large moves so libinput doesn't discard them as jumps */
            while (dx || dy)
            {
                int sx = dx > 40 ? 40 : dx < -40 ? -40 : dx;
                int sy = dy > 40 ? 40 : dy < -40 ? -40 : dy;
                emit_ev( mouse_fd, EV_REL, REL_X, sx );
                emit_ev( mouse_fd, EV_REL, REL_Y, sy );
                syn( mouse_fd );
                dx -= sx; dy -= sy;
                usleep( 4000 );
            }
            msleep( 30 );
        }
        else if (!strcmp( cmd, "btn" ) && arg1[0])
        {   /* btn down|up|click [right] */
            int code = !strcmp( arg2, "right" ) ? BTN_RIGHT : BTN_LEFT;
            if (!strcmp( arg1, "down" )) { emit_ev( mouse_fd, EV_KEY, code, 1 ); syn( mouse_fd ); }
            else if (!strcmp( arg1, "up" )) { emit_ev( mouse_fd, EV_KEY, code, 0 ); syn( mouse_fd ); }
            else if (!strcmp( arg1, "click" ))
            {
                emit_ev( mouse_fd, EV_KEY, code, 1 ); syn( mouse_fd ); msleep( 70 );
                emit_ev( mouse_fd, EV_KEY, code, 0 ); syn( mouse_fd );
            }
            msleep( 50 );
        }
        else if (!strcmp( cmd, "key" ) && arg1[0])   key_combo( arg1 );
        else if (!strcmp( cmd, "keydown" ) && arg1[0])
        { int c = keycode( arg1 ); if (c >= 0) { emit_ev( kbd_fd, EV_KEY, c, 1 ); syn( kbd_fd ); } }
        else if (!strcmp( cmd, "keyup" ) && arg1[0])
        { int c = keycode( arg1 ); if (c >= 0) { emit_ev( kbd_fd, EV_KEY, c, 0 ); syn( kbd_fd ); } }
        else if (!strcmp( cmd, "sleep" ) && arg1[0]) msleep( atoi( arg1 ) );
        else if (!strcmp( cmd, "quit" )) break;
        else fprintf( stderr, "bad cmd: %s\n", line );
        printf( "ok %s\n", cmd ); fflush( stdout );
    }

    /* release pen proximity before teardown */
    emit_ev( pen_fd, EV_KEY, BTN_TOOL_PEN, 0 ); syn( pen_fd );
    ioctl( pen_fd, UI_DEV_DESTROY ); close( pen_fd );
    ioctl( kbd_fd, UI_DEV_DESTROY ); close( kbd_fd );
    ioctl( mouse_fd, UI_DEV_DESTROY ); close( mouse_fd );
    return 0;
}
