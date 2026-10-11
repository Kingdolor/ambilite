#pragma once
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

// One LED = rectangle on the picture, relative coordinates 0..1 (HyperHDR layout format)
typedef struct {
    float hmin, hmax, vmin, vmax;
} led_rect_t;

enum {
    SMOOTH_DIRECT = 0, // no temporal filter
    SMOOTH_CLASSIC = 1, // exponential filter with fixed time constant
    SMOOTH_ADAPTIVE = 2, // change-dependent filter + scene-cut bypass + soft release
};

typedef struct {
    float saturation; // 1.0 = unchanged
    float gamma; // 1.0 = linear
    int brightness; // 0..100 %
    bool blackborder;
    int blackborder_threshold; // percent, HyperHDR default 5
    char color_order[4]; // e.g. "grb"

    int smoothing_mode; // SMOOTH_*
    int smoothing_ms; // classic time constant
    int adaptive_strength; // 0..100: time constant for small changes (x3 ms)
    int release_ms; // adaptive: extra fade-out time when getting darker
    int spatial; // 0..100: blend with neighbouring LEDs
} ledproc_params_t;

typedef struct {
    int count;
    led_rect_t* rects;

    ledproc_params_t params;
    pthread_mutex_t lock;

    // derived
    uint8_t lut[256];
    uint8_t lut_lin[256]; // brightness only (for colour/effect modes)
    int order_idx[3];

    // pixel sampling map (CSR layout), rebuilt on size/border change
    int map_w, map_h, map_bt, map_bl;
    int* starts; // count + 1
    int* offsets;

    // black border
    int bt, bl;
    int cand_bt, cand_bl, cand_cnt;

    // per-LED working buffers (count*3 floats each)
    float* avg;
    float* target;
    float* prev_target;
    float* smooth;
    bool have_prev;
    uint64_t last_us;

    // metrics
    unsigned long scene_cuts;
} ledproc_t;

int ledproc_init(ledproc_t* lp);
void ledproc_free(ledproc_t* lp);
int ledproc_load_layout(ledproc_t* lp, const char* path);
void ledproc_set_params(ledproc_t* lp, const ledproc_params_t* params);

// rgb: packed RGB24, stride = width*3. out: count*3 bytes in device colour order.
void ledproc_process(ledproc_t* lp, int width, int height, const uint8_t* rgb, uint8_t* out, uint64_t now_us);

// For colour/effect modes: rgb (count*3, plain RGB) -> brightness + colour order, no gamma.
void ledproc_finish_plain(ledproc_t* lp, const uint8_t* rgb, uint8_t* out);
