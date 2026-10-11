// LED colour engine: picture -> per-LED colour, black border detection,
// saturation, gamma/brightness, optional smoothing, colour order.
#include "ledproc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BB_GROW_FRAMES 45 // frames a larger border must be stable before it is applied
#define BB_SHRINK_FRAMES 3 // frames a smaller border must be stable before it is applied
#define SPARSE_MIN_PIXELS 24 // above this many pixels per LED, sample every 2nd pixel

int ledproc_init(ledproc_t* lp)
{
    memset(lp, 0, sizeof(*lp));
    pthread_mutex_init(&lp->lock, NULL);
    ledproc_params_t p = {
        .saturation = 1.0f,
        .gamma = 1.0f,
        .brightness = 100,
        .blackborder = true,
        .blackborder_threshold = 5,
        .color_order = "rgb",
        .smoothing_mode = SMOOTH_DIRECT,
        .smoothing_ms = 40,
        .adaptive_strength = 50,
        .release_ms = 150,
        .spatial = 0,
    };
    ledproc_set_params(lp, &p);
    return 0;
}

void ledproc_free(ledproc_t* lp)
{
    free(lp->rects);
    free(lp->starts);
    free(lp->offsets);
    free(lp->smooth);
    free(lp->avg);
    free(lp->target);
    free(lp->prev_target);
    lp->rects = NULL;
    lp->starts = NULL;
    lp->offsets = NULL;
    lp->smooth = lp->avg = lp->target = lp->prev_target = NULL;
    lp->count = 0;
    pthread_mutex_destroy(&lp->lock);
}

// Minimal parser for our own leds.json – looks for the four keys of every object.
static int parse_key(const char* obj, const char* end, const char* key, float* out)
{
    char pattern[16];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = obj;
    size_t plen = strlen(pattern);
    while (p && p < end) {
        p = strstr(p, pattern);
        if (!p || p >= end)
            return -1;
        p += plen;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (*p != ':')
            continue;
        p++;
        *out = strtof(p, NULL);
        return 0;
    }
    return -1;
}

int ledproc_load_layout(ledproc_t* lp, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 4 * 1024 * 1024) {
        fclose(f);
        return -2;
    }
    char* buf = malloc(len + 1);
    if (fread(buf, 1, len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return -3;
    }
    fclose(f);
    buf[len] = 0;

    int cap = 64, n = 0;
    led_rect_t* rects = malloc(sizeof(led_rect_t) * cap);
    const char* p = buf;
    while ((p = strchr(p, '{')) != NULL) {
        const char* end = strchr(p, '}');
        if (!end)
            break;
        led_rect_t r;
        if (parse_key(p, end, "hmin", &r.hmin) == 0 && parse_key(p, end, "hmax", &r.hmax) == 0
            && parse_key(p, end, "vmin", &r.vmin) == 0 && parse_key(p, end, "vmax", &r.vmax) == 0) {
            if (n == cap) {
                cap *= 2;
                rects = realloc(rects, sizeof(led_rect_t) * cap);
            }
            rects[n++] = r;
        }
        p = end + 1;
    }
    free(buf);

    if (n == 0) {
        free(rects);
        return -4;
    }

    pthread_mutex_lock(&lp->lock);
    free(lp->rects);
    free(lp->smooth);
    free(lp->avg);
    free(lp->target);
    free(lp->prev_target);
    free(lp->starts);
    free(lp->offsets);
    lp->rects = rects;
    lp->count = n;
    lp->smooth = calloc(n * 3, sizeof(float));
    lp->avg = calloc(n * 3, sizeof(float));
    lp->target = calloc(n * 3, sizeof(float));
    lp->prev_target = calloc(n * 3, sizeof(float));
    lp->have_prev = false;
    lp->starts = NULL;
    lp->offsets = NULL;
    lp->map_w = lp->map_h = 0;
    pthread_mutex_unlock(&lp->lock);
    return n;
}

void ledproc_set_params(ledproc_t* lp, const ledproc_params_t* params)
{
    pthread_mutex_lock(&lp->lock);
    lp->params = *params;
    if (lp->params.gamma < 0.1f)
        lp->params.gamma = 0.1f;
    if (lp->params.brightness < 0)
        lp->params.brightness = 0;
    if (lp->params.brightness > 100)
        lp->params.brightness = 100;

    double scale = lp->params.brightness / 100.0;
    for (int i = 0; i < 256; i++) {
        double v = 255.0 * pow(i / 255.0, lp->params.gamma) * scale;
        lp->lut[i] = (uint8_t)(v + 0.5 > 255 ? 255 : v + 0.5);
        double l = i * scale;
        lp->lut_lin[i] = (uint8_t)(l + 0.5 > 255 ? 255 : l + 0.5);
    }

    // colour order: position k of output gets channel order_idx[k] (0=r,1=g,2=b)
    const char* o = lp->params.color_order;
    bool valid = strlen(o) == 3;
    for (int k = 0; k < 3 && valid; k++) {
        switch (o[k]) {
        case 'r': case 'R': lp->order_idx[k] = 0; break;
        case 'g': case 'G': lp->order_idx[k] = 1; break;
        case 'b': case 'B': lp->order_idx[k] = 2; break;
        default: valid = false;
        }
    }
    if (!valid) {
        lp->order_idx[0] = 0;
        lp->order_idx[1] = 1;
        lp->order_idx[2] = 2;
    }
    if (!lp->params.blackborder) {
        lp->bt = lp->bl = 0;
        lp->cand_cnt = 0;
    }
    pthread_mutex_unlock(&lp->lock);
}

static void build_map(ledproc_t* lp, int w, int h)
{
    int x0 = lp->bl, x1 = w - lp->bl;
    int y0 = lp->bt, y1 = h - lp->bt;
    int cw = x1 - x0, ch = y1 - y0;

    free(lp->starts);
    free(lp->offsets);
    lp->starts = malloc(sizeof(int) * (lp->count + 1));

    // first pass: count
    int total = 0;
    for (int i = 0; i < lp->count; i++) {
        led_rect_t* r = &lp->rects[i];
        int ax = x0 + (int)floorf(r->hmin * cw), bx = x0 + (int)ceilf(r->hmax * cw);
        int ay = y0 + (int)floorf(r->vmin * ch), by = y0 + (int)ceilf(r->vmax * ch);
        if (bx <= ax) bx = ax + 1;
        if (by <= ay) by = ay + 1;
        if (bx > x1) { bx = x1; if (ax >= bx) ax = bx - 1; }
        if (by > y1) { by = y1; if (ay >= by) ay = by - 1; }
        int step = ((bx - ax) * (by - ay) > SPARSE_MIN_PIXELS) ? 2 : 1;
        total += ((bx - ax + step - 1) / step) * ((by - ay + step - 1) / step);
    }
    lp->offsets = malloc(sizeof(int) * (total > 0 ? total : 1));

    int n = 0;
    for (int i = 0; i < lp->count; i++) {
        led_rect_t* r = &lp->rects[i];
        int ax = x0 + (int)floorf(r->hmin * cw), bx = x0 + (int)ceilf(r->hmax * cw);
        int ay = y0 + (int)floorf(r->vmin * ch), by = y0 + (int)ceilf(r->vmax * ch);
        if (bx <= ax) bx = ax + 1;
        if (by <= ay) by = ay + 1;
        if (bx > x1) { bx = x1; if (ax >= bx) ax = bx - 1; }
        if (by > y1) { by = y1; if (ay >= by) ay = by - 1; }
        int step = ((bx - ax) * (by - ay) > SPARSE_MIN_PIXELS) ? 2 : 1;
        lp->starts[i] = n;
        for (int y = ay; y < by; y += step)
            for (int x = ax; x < bx; x += step)
                lp->offsets[n++] = (y * w + x) * 3;
    }
    lp->starts[lp->count] = n;
    lp->map_w = w;
    lp->map_h = h;
    lp->map_bt = lp->bt;
    lp->map_bl = lp->bl;
}

static inline bool is_dark(const uint8_t* px, int thr)
{
    return px[0] < thr && px[1] < thr && px[2] < thr;
}

// Symmetric black border detection (letterbox / pillarbox), probing 3 lines each way.
static void detect_border(ledproc_t* lp, int w, int h, const uint8_t* rgb)
{
    int thr = (lp->params.blackborder_threshold * 255) / 100;
    if (thr < 1)
        thr = 1;
    const int xs[3] = { w / 4, w / 2, (3 * w) / 4 };
    const int ys[3] = { h / 4, h / 2, (3 * h) / 4 };

    int top = -1;
    for (int y = 0; y < h / 3 && top < 0; y++) {
        for (int k = 0; k < 3; k++) {
            if (!is_dark(rgb + (y * w + xs[k]) * 3, thr) || !is_dark(rgb + ((h - 1 - y) * w + xs[k]) * 3, thr)) {
                top = y;
                break;
            }
        }
    }
    if (top < 0)
        return; // (almost) black picture: unknown, keep current border

    int left = -1;
    for (int x = 0; x < w / 3 && left < 0; x++) {
        for (int k = 0; k < 3; k++) {
            if (!is_dark(rgb + (ys[k] * w + x) * 3, thr) || !is_dark(rgb + (ys[k] * w + (w - 1 - x)) * 3, thr)) {
                left = x;
                break;
            }
        }
    }
    if (left < 0)
        left = 0;

    // ignore tiny borders (overscan noise)
    if (top <= h / 100)
        top = 0;
    if (left <= w / 100)
        left = 0;

    if (top == lp->bt && left == lp->bl) {
        lp->cand_cnt = 0;
        return;
    }
    if (top == lp->cand_bt && left == lp->cand_bl) {
        lp->cand_cnt++;
    } else {
        lp->cand_bt = top;
        lp->cand_bl = left;
        lp->cand_cnt = 1;
    }
    bool shrinking = top <= lp->bt && left <= lp->bl;
    if (lp->cand_cnt >= (shrinking ? BB_SHRINK_FRAMES : BB_GROW_FRAMES)) {
        lp->bt = top;
        lp->bl = left;
        lp->cand_cnt = 0;
    }
}

static inline float clampf(float v, float a, float b)
{
    return v < a ? a : (v > b ? b : v);
}

static inline float luma(const float* c)
{
    return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
}

#define SCENE_CUT_DELTA 60.0f // per-LED change (0..255) that counts as "changed"
#define SCENE_CUT_FRACTION 0.6f // share of LEDs that must change for a scene cut
#define ADAPT_D0 20.0f // change size where adaptive smoothing has fallen to 1/e
#define RELEASE_D0 120.0f // same for the release tail when getting darker

void ledproc_process(ledproc_t* lp, int w, int h, const uint8_t* rgb, uint8_t* out, uint64_t now_us)
{
    pthread_mutex_lock(&lp->lock);
    const int n = lp->count;
    if (n <= 0 || w < 4 || h < 4) {
        pthread_mutex_unlock(&lp->lock);
        return;
    }
    const ledproc_params_t* P = &lp->params;

    if (P->blackborder)
        detect_border(lp, w, h, rgb);
    if (w != lp->map_w || h != lp->map_h || lp->bt != lp->map_bt || lp->bl != lp->map_bl || !lp->starts)
        build_map(lp, w, h);

    // 1) area average per LED
    for (int i = 0; i < n; i++) {
        int s = lp->starts[i], e = lp->starts[i + 1];
        uint32_t sr = 0, sg = 0, sb = 0;
        for (int j = s; j < e; j++) {
            const uint8_t* px = rgb + lp->offsets[j];
            sr += px[0];
            sg += px[1];
            sb += px[2];
        }
        int cnt = e - s;
        float* a = &lp->avg[i * 3];
        if (cnt > 0) {
            float inv = 1.0f / cnt;
            a[0] = sr * inv;
            a[1] = sg * inv;
            a[2] = sb * inv;
        } else {
            a[0] = a[1] = a[2] = 0;
        }
    }

    // 2) spatial blend with neighbours along the strip (no wrap: the strip has a gap)
    const float k = clampf(P->spatial / 100.0f, 0, 1) * 0.25f;
    const float sat = P->saturation;
    const bool do_sat = sat < 0.99f || sat > 1.01f;
    for (int i = 0; i < n; i++) {
        float c[3];
        const float* a = &lp->avg[i * 3];
        if (k > 0.0f) {
            const float* l = i > 0 ? &lp->avg[(i - 1) * 3] : a;
            const float* r = i < n - 1 ? &lp->avg[(i + 1) * 3] : a;
            for (int ch = 0; ch < 3; ch++)
                c[ch] = (1.0f - 2.0f * k) * a[ch] + k * (l[ch] + r[ch]);
        } else {
            c[0] = a[0];
            c[1] = a[1];
            c[2] = a[2];
        }
        // 3) saturation + gamma/brightness
        if (do_sat) {
            float L = luma(c);
            for (int ch = 0; ch < 3; ch++)
                c[ch] = clampf(L + (c[ch] - L) * sat, 0, 255);
        }
        float* t = &lp->target[i * 3];
        for (int ch = 0; ch < 3; ch++)
            t[ch] = lp->lut[(int)(clampf(c[ch], 0, 255) + 0.5f)];
    }

    // 4) temporal filter
    float dt = lp->last_us ? (float)(now_us - lp->last_us) / 1000.0f : 0.0f;
    if (dt > 100.0f)
        dt = 100.0f;
    lp->last_us = now_us;

    bool cut = !lp->have_prev;
    if (lp->have_prev && P->smoothing_mode == SMOOTH_ADAPTIVE) {
        // scene cut = most of the frame changed strongly at once
        int changed = 0;
        for (int i = 0; i < n; i++) {
            const float* a = &lp->target[i * 3];
            const float* b = &lp->prev_target[i * 3];
            float d = fmaxf(fabsf(a[0] - b[0]), fmaxf(fabsf(a[1] - b[1]), fabsf(a[2] - b[2])));
            if (d > SCENE_CUT_DELTA)
                changed++;
        }
        if (changed > n * SCENE_CUT_FRACTION) {
            cut = true;
            lp->scene_cuts++;
        }
    }

    if (P->smoothing_mode == SMOOTH_DIRECT || cut || dt <= 0.0f) {
        memcpy(lp->smooth, lp->target, sizeof(float) * (size_t)n * 3);
    } else if (P->smoothing_mode == SMOOTH_CLASSIC) {
        float alpha = P->smoothing_ms > 0 ? 1.0f - expf(-dt / (float)P->smoothing_ms) : 1.0f;
        for (int i = 0; i < n * 3; i++)
            lp->smooth[i] += (lp->target[i] - lp->smooth[i]) * alpha;
    } else { // adaptive
        const float tau_small = clampf((float)P->adaptive_strength, 0, 100) * 3.0f;
        const float release = (float)(P->release_ms > 0 ? P->release_ms : 0);
        for (int i = 0; i < n; i++) {
            float* sm = &lp->smooth[i * 3];
            const float* t = &lp->target[i * 3];
            float d = fmaxf(fabsf(t[0] - sm[0]), fmaxf(fabsf(t[1] - sm[1]), fabsf(t[2] - sm[2])));
            float tau = tau_small * expf(-d / ADAPT_D0);
            if (luma(t) < luma(sm)) // getting darker: soft release tail
                tau = fmaxf(tau, release * expf(-d / RELEASE_D0));
            float alpha = tau < 0.5f ? 1.0f : 1.0f - expf(-dt / tau);
            for (int ch = 0; ch < 3; ch++)
                sm[ch] += (t[ch] - sm[ch]) * alpha;
        }
    }
    memcpy(lp->prev_target, lp->target, sizeof(float) * (size_t)n * 3);
    lp->have_prev = true;

    // 5) colour order
    for (int i = 0; i < n; i++) {
        const float* o = &lp->smooth[i * 3];
        uint8_t* dst = out + i * 3;
        for (int ch = 0; ch < 3; ch++)
            dst[ch] = (uint8_t)(clampf(o[lp->order_idx[ch]], 0, 255) + 0.5f);
    }
    pthread_mutex_unlock(&lp->lock);
}

void ledproc_finish_plain(ledproc_t* lp, const uint8_t* rgb, uint8_t* out)
{
    pthread_mutex_lock(&lp->lock);
    for (int i = 0; i < lp->count; i++) {
        const uint8_t* c = rgb + i * 3;
        uint8_t* dst = out + i * 3;
        for (int ch = 0; ch < 3; ch++)
            dst[ch] = lp->lut_lin[c[lp->order_idx[ch]]];
    }
    lp->have_prev = false; // next video frame starts fresh
    pthread_mutex_unlock(&lp->lock);
}
