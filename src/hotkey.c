#include "hotkey.h"
#include "log.h"
#include "utils.h"
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#define MAX_DEVS 16
#define RESCAN_US 30000000ULL
#define DEBOUNCE_US 400000ULL

static int open_devices(struct pollfd* fds)
{
    glob_t g;
    int n = 0;
    if (glob("/dev/input/event*", 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && n < MAX_DEVS; i++) {
            int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                fds[n].fd = fd;
                fds[n].events = POLLIN;
                n++;
            }
        }
    }
    globfree(&g);
    return n;
}

static void close_devices(struct pollfd* fds, int n)
{
    for (int i = 0; i < n; i++)
        close(fds[i].fd);
}

static void* hotkey_loop(void* data)
{
    hotkey_t* hk = (hotkey_t*)data;
    struct pollfd fds[MAX_DEVS];
    int n = open_devices(fds);
    uint64_t last_scan = getticks_us();
    uint64_t last_fire = 0;
    INFO("Hotkey listener on %d input devices", n);

    while (hk->running) {
        if (getticks_us() - last_scan > RESCAN_US) {
            close_devices(fds, n);
            n = open_devices(fds);
            last_scan = getticks_us();
        }
        if (n == 0) {
            usleep(500000);
            continue;
        }
        int r = poll(fds, n, 250);
        if (r <= 0)
            continue;
        for (int i = 0; i < n; i++) {
            if (!(fds[i].revents & POLLIN))
                continue;
            struct input_event ev[16];
            ssize_t rd = read(fds[i].fd, ev, sizeof(ev));
            if (rd <= 0)
                continue;
            for (size_t k = 0; k < (size_t)rd / sizeof(struct input_event); k++) {
                if (ev[k].type != EV_KEY || ev[k].value != 1)
                    continue;
                int code = ev[k].code;
                hk->last_key = code;
                if (hk->learning) {
                    hk->learning = false;
                    hk->hotkey = code;
                    INFO("Hotkey learned: %d", code);
                    if (hk->cb)
                        hk->cb(hk->cb_data, code, true);
                } else if (hk->hotkey != 0 && code == hk->hotkey) {
                    uint64_t now = getticks_us();
                    if (now - last_fire > DEBOUNCE_US) {
                        last_fire = now;
                        if (hk->cb)
                            hk->cb(hk->cb_data, code, false);
                    }
                }
            }
        }
    }
    close_devices(fds, n);
    return NULL;
}

int hotkey_start(hotkey_t* hk, int code, hotkey_cb_t cb, void* data)
{
    if (hk->running)
        return 0;
    hk->hotkey = code;
    hk->cb = cb;
    hk->cb_data = data;
    hk->running = true;
    if (pthread_create(&hk->thread, NULL, hotkey_loop, hk) != 0) {
        hk->running = false;
        return -1;
    }
    return 0;
}

void hotkey_stop(hotkey_t* hk)
{
    if (!hk->running)
        return;
    hk->running = false;
    pthread_join(hk->thread, NULL);
}
