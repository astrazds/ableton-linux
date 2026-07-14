/* jacklinkd.c — JACK/PipeWire link guardian for audio device hotplug.
 *
 * The Ableton prefix's audio path is WineASIO -> PipeWire-JACK: Live's ASIO
 * "device" is the JACK graph, which survives hardware changes — but the
 * links between wineasio's ports and the hardware ports are destroyed when
 * a device unplugs, and PipeWire/WirePlumber never restores JACK links on
 * replug, so Live goes silent until audio is restarted.
 *
 * This daemon watches the graph. When a port unregisters (device death) it
 * remembers, by port NAME, every link that port held at that moment; when a
 * port with a remembered name registers again it re-creates those links.
 * Manual disconnects are left alone: a link only enters the graveyard if
 * its port unregisters within a few seconds of the disconnect.
 *
 * Same caveat as the winealsa MIDI hotplug fix: it restores what existed;
 * it cannot invent links for a device Live has never been wired to.
 *
 * build: gcc -O2 -o jacklinkd jacklinkd.c -ljack -lpthread
 * run:   started by the ableton-live launcher; safe to run standalone.
 */
#include <jack/jack.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define NAME_MAX_LEN   256
#define MAX_LINKS      512
#define MAX_GRAVE      256
#define MAX_RECENT     128
#define RECENT_MS      5000   /* disconnect->unregister window = device death */

struct link { char src[NAME_MAX_LEN], dst[NAME_MAX_LEN]; };
struct stamped_link { struct link l; uint64_t t_ms; };

static jack_client_t *client;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake_cond = PTHREAD_COND_INITIALIZER;
static int wake, quit;

static struct link live[MAX_LINKS];            /* links currently up */
static int num_live;
static struct stamped_link recent[MAX_RECENT]; /* recently torn down */
static int num_recent;
static struct link grave[MAX_GRAVE];           /* awaiting replug */
static int num_grave;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* --- callbacks: registry bookkeeping only, no graph calls allowed --- */

static void on_connect(jack_port_id_t a, jack_port_id_t b, int yes, void *arg)
{
    jack_port_t *pa = jack_port_by_id(client, a);
    jack_port_t *pb = jack_port_by_id(client, b);
    const char *na, *nb;
    int i;

    if (!pa || !pb) return;
    na = jack_port_name(pa);
    nb = jack_port_name(pb);

    pthread_mutex_lock(&lock);
    if (yes) {
        for (i = 0; i < num_live; i++)
            if (!strcmp(live[i].src, na) && !strcmp(live[i].dst, nb)) break;
        if (i == num_live && num_live < MAX_LINKS) {
            snprintf(live[num_live].src, NAME_MAX_LEN, "%s", na);
            snprintf(live[num_live].dst, NAME_MAX_LEN, "%s", nb);
            num_live++;
        }
        /* a restored/manual connect cancels any graveyard copy */
        for (i = 0; i < num_grave; i++)
            if (!strcmp(grave[i].src, na) && !strcmp(grave[i].dst, nb))
                grave[i] = grave[--num_grave], i--;
    } else {
        for (i = 0; i < num_live; i++)
            if (!strcmp(live[i].src, na) && !strcmp(live[i].dst, nb)) {
                live[i] = live[--num_live];
                break;
            }
        if (num_recent < MAX_RECENT) {
            snprintf(recent[num_recent].l.src, NAME_MAX_LEN, "%s", na);
            snprintf(recent[num_recent].l.dst, NAME_MAX_LEN, "%s", nb);
            recent[num_recent].t_ms = now_ms();
            num_recent++;
        }
    }
    pthread_mutex_unlock(&lock);
}

static void on_registration(jack_port_id_t id, int registered, void *arg)
{
    jack_port_t *p = jack_port_by_id(client, id);
    const char *name;
    uint64_t t = now_ms();
    int i, g;

    if (!p) return;
    name = jack_port_name(p);

    pthread_mutex_lock(&lock);
    if (!registered) {
        /* device death: adopt this port's recently-killed links */
        for (i = 0; i < num_recent; i++) {
            if (t - recent[i].t_ms > RECENT_MS ||
                (strcmp(recent[i].l.src, name) && strcmp(recent[i].l.dst, name)))
                continue;
            for (g = 0; g < num_grave; g++)
                if (!strcmp(grave[g].src, recent[i].l.src) &&
                    !strcmp(grave[g].dst, recent[i].l.dst)) break;
            if (g == num_grave && num_grave < MAX_GRAVE) {
                grave[num_grave++] = recent[i].l;
                fprintf(stderr, "jacklinkd: remembering %s -> %s\n",
                        recent[i].l.src, recent[i].l.dst);
            }
            recent[i] = recent[--num_recent];
            i--;
        }
        /* links torn down with no disconnect callback (teardown race) */
        for (i = 0; i < num_live; i++) {
            if (strcmp(live[i].src, name) && strcmp(live[i].dst, name)) continue;
            for (g = 0; g < num_grave; g++)
                if (!strcmp(grave[g].src, live[i].src) &&
                    !strcmp(grave[g].dst, live[i].dst)) break;
            if (g == num_grave && num_grave < MAX_GRAVE)
                grave[num_grave++] = live[i];
            live[i] = live[--num_live];
            i--;
        }
    } else if (num_grave) {
        /* possible replug: let the main thread try to restore */
        for (i = 0; i < num_grave; i++) {
            if (strcmp(grave[i].src, name) && strcmp(grave[i].dst, name)) continue;
            wake = 1;
            pthread_cond_signal(&wake_cond);
            break;
        }
    }
    pthread_mutex_unlock(&lock);
}

static void on_shutdown(void *arg)
{
    pthread_mutex_lock(&lock);
    quit = 1;
    wake = 1;
    pthread_cond_signal(&wake_cond);
    pthread_mutex_unlock(&lock);
}

/* --- main thread: the only place that calls into the graph --- */

static void restore_links(void)
{
    struct link todo[MAX_GRAVE];
    int i, n = 0;

    pthread_mutex_lock(&lock);
    for (i = 0; i < num_grave; i++) todo[n++] = grave[i];
    pthread_mutex_unlock(&lock);

    for (i = 0; i < n; i++) {
        if (!jack_port_by_name(client, todo[i].src) ||
            !jack_port_by_name(client, todo[i].dst))
            continue; /* other end not back yet; a later registration retries */
        if (!jack_connect(client, todo[i].src, todo[i].dst))
            fprintf(stderr, "jacklinkd: restored %s -> %s\n",
                    todo[i].src, todo[i].dst);
        /* success or already-connected: on_connect() clears the graveyard */
    }
}

/* record links that already exist when we attach, so a later device death
 * still knows what to restore */
static void seed_live_links(void)
{
    const char **ports = jack_get_ports(client, NULL, NULL, JackPortIsOutput);
    const char **peers;
    int i, j;

    if (!ports) return;
    pthread_mutex_lock(&lock);
    for (i = 0; ports[i] && num_live < MAX_LINKS; i++) {
        jack_port_t *p = jack_port_by_name(client, ports[i]);
        if (!p || !(peers = jack_port_get_all_connections(client, p))) continue;
        for (j = 0; peers[j] && num_live < MAX_LINKS; j++) {
            snprintf(live[num_live].src, NAME_MAX_LEN, "%s", ports[i]);
            snprintf(live[num_live].dst, NAME_MAX_LEN, "%s", peers[j]);
            num_live++;
        }
        jack_free(peers);
    }
    pthread_mutex_unlock(&lock);
    jack_free(ports);
}

int main(void)
{
    for (;;) {
        client = jack_client_open("ableton-linkd", JackNoStartServer, NULL);
        if (!client) {
            sleep(2); /* pipewire not up yet; keep trying */
            continue;
        }
        pthread_mutex_lock(&lock);
        quit = 0;
        num_live = num_recent = 0; /* graveyard survives server restarts */
        pthread_mutex_unlock(&lock);

        jack_set_port_connect_callback(client, on_connect, NULL);
        jack_set_port_registration_callback(client, on_registration, NULL);
        jack_on_shutdown(client, on_shutdown, NULL);
        if (jack_activate(client)) {
            jack_client_close(client);
            sleep(2);
            continue;
        }
        seed_live_links();
        fprintf(stderr, "jacklinkd: watching the graph\n");

        for (;;) {
            pthread_mutex_lock(&lock);
            while (!wake) pthread_cond_wait(&wake_cond, &lock);
            wake = 0;
            if (quit) { pthread_mutex_unlock(&lock); break; }
            pthread_mutex_unlock(&lock);

            usleep(300000); /* let the device's ports settle */
            restore_links();
        }
        jack_client_close(client);
        sleep(2);
    }
}
