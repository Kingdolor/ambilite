#pragma once
#include "ledproc.h"
#include <stdint.h>

// Renders one frame of a picture-independent mode into rgb (count*3, plain RGB).
// mode: "color" or "effect"; t_s = seconds since start.
void effects_render(const ledproc_t* lp, const char* mode, const char* effect, int color, int speed, double t_s,
    uint8_t* rgb, float* state);
