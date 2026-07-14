/* fakectl.c — fake ALSA-seq MIDI controller for hotplug testing.
 *
 * Creates a seq client "FakeCtl" with one duplex port and sends a
 * note-on/note-off pair to its subscribers every 500 ms. Kill + restart it
 * to simulate unplugging/replugging a USB MIDI controller: the client id
 * changes, exactly like a hardware replug, so a subscriber holding the old
 * address goes silent unless it re-subscribes on the announce event.
 *
 * build: gcc -o fakectl fakectl.c -lasound
 */
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    snd_seq_t *seq;
    int port, n = 0;

    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        fprintf(stderr, "cannot open ALSA sequencer\n");
        return 1;
    }
    snd_seq_set_client_name(seq, "FakeCtl");
    port = snd_seq_create_simple_port(seq, "FakeCtl MIDI 1",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ |
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_HARDWARE | SND_SEQ_PORT_TYPE_PORT);
    if (port < 0) {
        fprintf(stderr, "cannot create port\n");
        return 1;
    }
    printf("FakeCtl up: client %d port %d\n", snd_seq_client_id(seq), port);
    fflush(stdout);

    for (;;) {
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_noteon(&ev, 0, 60, 100);
        snd_seq_event_output_direct(seq, &ev);
        snd_seq_ev_set_noteoff(&ev, 0, 60, 0);
        snd_seq_event_output_direct(seq, &ev);
        printf("sent note pair #%d\n", ++n);
        fflush(stdout);
        usleep(500000);
    }
}
