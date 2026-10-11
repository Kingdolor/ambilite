// Picture-independent light modes: solid colour and a few calm effects.
#include "effects.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void hsv2rgb(float h, float s, float v, uint8_t* out)
{
    h = h - floorf(h);
    float r, g, b;
    int i = (int)(h * 6.0f);
    float f = h * 6.0f - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    out[0] = (uint8_t)(r * 255 + 0.5f);
    out[1] = (uint8_t)(g * 255 + 0.5f);
    out[2] = (uint8_t)(b * 255 + 0.5f);
}

// position of LED i around the screen (0..1), using its rectangle centre (angle around screen centre)
static float led_angle(const ledproc_t* lp, int i)
{
    const led_rect_t* r = &lp->rects[i];
    float cx = (r->hmin + r->hmax) * 0.5f - 0.5f;
    float cy = (r->vmin + r->vmax) * 0.5f - 0.5f;
    return (atan2f(cy, cx * 1.777f) + (float)M_PI) / (2.0f * (float)M_PI);
}

void effects_render(const ledproc_t* lp, const char* mode, const char* effect, int color, int speed, double t_s,
    uint8_t* rgb, float* state)
{
    const int n = lp->count;
    const uint8_t base[3] = { (uint8_t)(color >> 16), (uint8_t)(color >> 8), (uint8_t)color };
    const float sp = (float)speed / 4.0f; // 4 = "normal"
    const float t = (float)t_s;

    if (strcmp(mode, "color") == 0 || effect == NULL) {
        for (int i = 0; i < n; i++)
            memcpy(rgb + i * 3, base, 3);
        return;
    }

    if (strcmp(effect, "rainbow") == 0) {
        for (int i = 0; i < n; i++)
            hsv2rgb(led_angle(lp, i) + t * 0.05f * sp, 1.0f, 1.0f, rgb + i * 3);
    } else if (strcmp(effect, "breathe") == 0) {
        float phase = 0.5f - 0.5f * cosf(t * 0.6f * sp);
        float lvl = 0.12f + 0.88f * phase * phase;
        for (int i = 0; i < n; i++)
            for (int c = 0; c < 3; c++)
                rgb[i * 3 + c] = (uint8_t)(base[c] * lvl);
    } else if (strcmp(effect, "candle") == 0) {
        // warm flicker, every LED drifts towards a random target (state = n floats)
        for (int i = 0; i < n; i++) {
            if (state[i] <= 0.0f || (rand() & 31) < (int)(2 * sp))
                state[i] = 0.55f + 0.45f * (float)rand() / (float)RAND_MAX;
            float cur = state[n + i];
            cur += (state[i] - cur) * 0.25f;
            state[n + i] = cur;
            rgb[i * 3 + 0] = (uint8_t)(255 * cur);
            rgb[i * 3 + 1] = (uint8_t)(110 * cur * cur);
            rgb[i * 3 + 2] = (uint8_t)(18 * cur * cur);
        }
    } else { // "aurora": slow drifting green/blue/violet waves
        for (int i = 0; i < n; i++) {
            float a = led_angle(lp, i);
            float w1 = 0.5f + 0.5f * sinf(a * 12.566f + t * 0.35f * sp);
            float w2 = 0.5f + 0.5f * sinf(a * 6.283f - t * 0.22f * sp + 1.3f);
            float h = 0.33f + 0.12f * w1 + 0.30f * w2 * w2; // green -> blue -> violet
            hsv2rgb(h, 0.85f, 0.35f + 0.65f * w1, rgb + i * 3);
        }
    }
}
