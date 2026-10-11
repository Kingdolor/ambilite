// Host-side tests for colour engine + AWA framing (run on the build machine, not the TV)
#define _GNU_SOURCE
#include "ledproc.h"
#include "serial_out.h"
#include "utils.h"
#include <assert.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fill(uint8_t* img, int w, int h __attribute__((unused)), int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint8_t* p = img + (y * w + x) * 3;
            p[0] = r; p[1] = g; p[2] = b;
        }
}

int main(int argc __attribute__((unused)), char** argv)
{
    ledproc_t lp;
    ledproc_init(&lp);
    int n = ledproc_load_layout(&lp, argv[1]);
    printf("layout: %d LEDs\n", n);
    assert(n == 323);

    ledproc_params_t p = { .saturation = 1.0f, .gamma = 1.0f, .brightness = 100, .smoothing_mode = SMOOTH_DIRECT,
        .blackborder = true, .blackborder_threshold = 5, .color_order = "grb", .adaptive_strength = 50, .release_ms = 150 };
    ledproc_set_params(&lp, &p);

    int w = 320, h = 180;
    uint8_t* img = calloc(w * h, 3);
    uint8_t* out = malloc(n * 3);
    // left third red, right third blue, middle green
    fill(img, w, h, 0, 0, w / 3, h, 200, 0, 0);
    fill(img, w, h, w / 3, 0, 2 * w / 3, h, 0, 200, 0);
    fill(img, w, h, 2 * w / 3, 0, w, h, 0, 0, 200);
    ledproc_process(&lp, w, h, img, out, 1000);

    // classify each LED by its rect centre
    int errors = 0;
    for (int i = 0; i < n; i++) {
        float cx = (lp.rects[i].hmin + lp.rects[i].hmax) / 2;
        uint8_t g = out[i * 3], r = out[i * 3 + 1], b = out[i * 3 + 2]; // GRB order
        int expect = cx < 1.0f / 3 - 0.02f ? 0 : (cx > 2.0f / 3 + 0.02f ? 2 : (cx > 0.36f && cx < 0.63f ? 1 : -1));
        if (expect == 0 && !(r > 150 && g < 30 && b < 30)) errors++;
        if (expect == 1 && !(g > 150 && r < 30 && b < 30)) errors++;
        if (expect == 2 && !(b > 150 && r < 30 && g < 30)) errors++;
    }
    printf("colour/order check: %d errors\n", errors);
    assert(errors == 0);

    // gamma + brightness + saturation
    p.gamma = 1.5f; p.brightness = 50; p.saturation = 1.5f;
    ledproc_set_params(&lp, &p);
    memset(img, 128, w * h * 3);
    ledproc_process(&lp, w, h, img, out, 2000);
    printf("grey128 gamma1.5 bri50 -> %d %d %d (expect ~ %d)\n", out[0], out[1], out[2], (int)(255 * 0.5 * 0.3556 + 0.5));
    assert(abs(out[0] - 45) <= 1);

    // black border: 30 px letterbox, content white
    p.gamma = 1.0f; p.brightness = 100; p.saturation = 1.0f;
    ledproc_set_params(&lp, &p);
    memset(img, 0, w * h * 3);
    fill(img, w, h, 0, 30, w, h - 30, 255, 255, 255);
    for (int f = 0; f < 60; f++)
        ledproc_process(&lp, w, h, img, out, 3000 + f * 16000);
    printf("border detected: top=%d left=%d\n", lp.bt, lp.bl);
    assert(lp.bt == 30 && lp.bl == 0);
    int dark = 0;
    for (int i = 0; i < n; i++)
        if (out[i * 3] < 200) dark++;
    printf("LEDs not white with letterbox: %d\n", dark);
    assert(dark == 0);
    // back to full frame -> shrinks quickly
    memset(img, 255, w * h * 3);
    for (int f = 0; f < 4; f++)
        ledproc_process(&lp, w, h, img, out, 999000 + f * 16000);
    assert(lp.bt == 0);
    printf("border reset ok\n");

    // timing
    uint64_t t0 = getticks_us();
    for (int f = 0; f < 2000; f++)
        ledproc_process(&lp, w, h, img, out, 2000000 + f * 16000);
    printf("process: %.1f us/frame (host)\n", (getticks_us() - t0) / 2000.0);

    // ---------------- adaptive smoothing
    p.smoothing_mode = SMOOTH_ADAPTIVE; p.blackborder = false; p.color_order[0]='r'; p.color_order[1]='g'; p.color_order[2]='b';
    ledproc_set_params(&lp, &p);
    uint64_t t = 10000000;
    memset(img, 120, w * h * 3);
    ledproc_process(&lp, w, h, img, out, t);
    // small noise: +-8 flicker on whole picture should be damped strongly
    int maxdev = 0;
    for (int f = 0; f < 60; f++) {
        t += 16667;
        memset(img, (f & 1) ? 128 : 112, w * h * 3);
        ledproc_process(&lp, w, h, img, out, t);
        int dv = abs(out[0] - 120); if (dv > maxdev) maxdev = dv;
    }
    printf("adaptive: input flicker +-8 -> output deviation %d\n", maxdev);
    assert(maxdev <= 3);
    unsigned long cuts0 = lp.scene_cuts;
    // scene cut: grey -> white everywhere -> immediate
    memset(img, 255, w * h * 3); t += 16667;
    ledproc_process(&lp, w, h, img, out, t);
    printf("adaptive: scene cut -> %d (cuts %lu)\n", out[0], lp.scene_cuts - cuts0);
    assert(out[0] == 255 && lp.scene_cuts == cuts0 + 1);
    // local brightening on left third (not a cut): should pass nearly at once
    memset(img, 30, w * h * 3); for (int f = 0; f < 30; f++) { t += 16667; ledproc_process(&lp, w, h, img, out, t); }
    fill(img, w, h, 0, 0, w / 8, h, 255, 255, 255);
    t += 16667; ledproc_process(&lp, w, h, img, out, t);
    int left_led = 10; // a LED on the left side
    printf("adaptive: local brighten 30->255 after 1 frame: %d\n", out[left_led * 3]);
    assert(out[left_led * 3] >= 230);
    // local darkening: soft release
    fill(img, w, h, 0, 0, w / 8, h, 30, 30, 30);
    t += 16667; ledproc_process(&lp, w, h, img, out, t);
    int after1 = out[left_led * 3];
    for (int f = 0; f < 30; f++) { t += 16667; ledproc_process(&lp, w, h, img, out, t); }
    printf("adaptive: local darken 255->30: after 1 frame %d, after 0.5 s %d\n", after1, out[left_led * 3]);
    assert(after1 > 120 && out[left_led * 3] <= 40);
    // spatial blend
    p.smoothing_mode = SMOOTH_DIRECT; p.spatial = 100; ledproc_set_params(&lp, &p);
    memset(img, 0, w * h * 3);
    // light only LED 10's rectangle
    int x0 = (int)(lp.rects[10].hmin * w), x1 = (int)(lp.rects[10].hmax * w + 1), y0 = (int)(lp.rects[10].vmin * h), y1 = (int)(lp.rects[10].vmax * h + 1);
    fill(img, w, h, x0, y0, x1, y1, 255, 255, 255);
    t += 16667; ledproc_process(&lp, w, h, img, out, t);
    printf("spatial: LED9=%d LED10=%d LED11=%d\n", out[9 * 3], out[10 * 3], out[11 * 3]);
    assert(out[9 * 3] > 20 && out[10 * 3] > out[9 * 3]);

    // AWA frame through a pty
    int master, slave;
    char name[64];
    assert(openpty(&master, &slave, name, NULL, NULL) == 0);
    serial_out_t so;
    serial_out_init(&so, name, 2000000, true);
    so.esp_init = false;
    assert(serial_out_open(&so) == 0);
    for (int i = 0; i < n * 3; i++) out[i] = (uint8_t)(i * 7);
    assert(serial_out_send(&so, out, n) == 0);
    usleep(50000);
    uint8_t rx[2048];
    ssize_t got = 0, r;
    fcntl(master, F_SETFL, O_NONBLOCK);
    while ((r = read(master, rx + got, sizeof(rx) - got)) > 0) got += r;
    printf("pty received %zd bytes (expect %d)\n", got, 6 + n * 3 + 3);
    FILE* f = fopen("/tmp/awa_frame.bin", "wb"); fwrite(rx, 1, got, f); fclose(f);
    FILE* f2 = fopen("/tmp/awa_colors.bin", "wb"); fwrite(out, 1, n * 3, f2); fclose(f2);
    assert(got == 6 + n * 3 + 3);
    printf("ALL TESTS PASSED\n");
    return 0;
}
