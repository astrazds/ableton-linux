/* midihot.c — winmm MIDI-in hotplug listener (PE, CRT-free).
 *
 * Lists midi-in devices, opens the first whose name contains "FakeCtl"
 * (or the substring given as arg 1), starts recording and prints every
 * MIM_DATA until killed. Used with fakectl (Linux side) to verify that
 * winealsa re-subscribes after a controller unplug/replug.
 *
 * build: see build_midihot.sh; run: run_in_prefix.sh midihot.exe
 */
#include <windows.h>
#include <mmsystem.h>

static HANDLE g_out;
static char buf[1024];

static void emit( const char *s ){ DWORD n; WriteFile( g_out, s, lstrlenA(s), &n, NULL ); }
#define P(...) do { wsprintfA( buf, __VA_ARGS__ ); emit( buf ); } while (0)

static int contains( const char *hay, const char *needle )
{
    int nlen = lstrlenA( needle ), hlen = lstrlenA( hay ), i, j;
    for (i = 0; i + nlen <= hlen; i++) {
        for (j = 0; j < nlen && hay[i + j] == needle[j]; j++);
        if (j == nlen) return 1;
    }
    return 0;
}

static void CALLBACK midi_cb( HMIDIIN h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2 )
{
    switch (msg) {
        case MIM_OPEN:  P( "MIM_OPEN\r\n" ); break;
        case MIM_CLOSE: P( "MIM_CLOSE\r\n" ); break;
        case MIM_DATA:  P( "MIM_DATA %08x t=%u\r\n", (UINT)p1, (UINT)p2 ); break;
        default:        P( "MIM msg %u\r\n", msg ); break;
    }
}

void mainCRTStartup( void )
{
    const char *cmd = GetCommandLineA();
    const char *match = "FakeCtl";
    MIDIINCAPSA caps;
    HMIDIIN hin;
    UINT i, n, dev = ~0u, rc;

    g_out = GetStdHandle( STD_OUTPUT_HANDLE );

    /* optional arg: substring to match (skip past exe name, handle quotes) */
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"') cmd++; if (*cmd) cmd++; }
    else while (*cmd && *cmd != ' ') cmd++;
    while (*cmd == ' ') cmd++;
    if (*cmd) match = cmd;

    n = midiInGetNumDevs();
    P( "%u midi-in device(s)\r\n", n );
    for (i = 0; i < n; i++) {
        caps.szPname[0] = 0;
        midiInGetDevCapsA( i, &caps, sizeof(caps) );
        P( "  %u: '%s'\r\n", i, caps.szPname );
        if (dev == ~0u && contains( caps.szPname, match )) dev = i;
    }
    if (dev == ~0u) { P( "no device matching '%s'\r\n", match ); ExitProcess( 1 ); }

    P( "opening device %u\r\n", dev );
    rc = midiInOpen( &hin, dev, (DWORD_PTR)midi_cb, 0, CALLBACK_FUNCTION );
    if (rc != MMSYSERR_NOERROR) { P( "midiInOpen failed %u\r\n", rc ); ExitProcess( 1 ); }
    rc = midiInStart( hin );
    if (rc != MMSYSERR_NOERROR) { P( "midiInStart failed %u\r\n", rc ); ExitProcess( 1 ); }
    P( "recording; kill me to stop\r\n" );

    for (;;) Sleep( 1000 );
}
