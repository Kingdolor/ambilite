#pragma once
#include <pbnjson.h>
#include <stdbool.h>

#define CONFIG_DIR "/media/developer/ambilite"
#define CONFIG_PATH CONFIG_DIR "/config.json"
#define LEDS_USER_PATH CONFIG_DIR "/leds.json"

typedef struct {
    int brightness; // 0..100
    double saturation;
    double gamma;
} color_profile_t;

typedef struct {
    // general
    bool autostart; // start service at TV boot (init.d hook)
    bool enabled; // ambilight on/off (persisted)
    char* mode; // "video" | "color" | "effect"

    // colour profiles
    color_profile_t sdr;
    color_profile_t hdr;
    bool hdr_auto; // switch to HDR profile when the TV reports HDR/DV/HLG
    bool blackborder;
    int blackborder_threshold;

    // smoothing
    int smoothing_mode; // 0 direct, 1 classic, 2 adaptive
    int smoothing_ms; // classic
    int adaptive_strength; // 0..100
    int release_ms;
    int spatial; // 0..100

    // colour / effect modes
    int color; // 0xRRGGBB
    char* effect; // "rainbow" | "breathe" | "candle" | "aurora"
    int effect_speed; // 1..10

    // output
    char* serial_device; // "auto" or /dev/ttyUSB0
    int baudrate;
    bool awa_mode;
    char* color_order;

    // capture
    int width;
    int height;
    int fps;
    bool vsync;
    char* video_backend;
    char* ui_backend;
    bool no_gui;
    int quirks;

    // remote / network
    int hotkey; // linux input key code toggling the ambilight, 0 = off
    bool api_enabled;
    int api_port;
    char* api_key; // pairing code for the iPhone app
    char* github_repo; // "owner/ambilite" for updates
} settings_t;

void settings_init(settings_t*);
void settings_free(settings_t*);
void settings_copy(settings_t* dst, const settings_t* src);
int settings_load_json(settings_t* settings, jvalue_ref source);
// include_secret: also write api_key (file persistence), not for API replies
int settings_save_json(settings_t* settings, jvalue_ref target, bool include_secret);
int settings_load_file(settings_t* settings, const char* path);
int settings_save_file(settings_t* settings, const char* path);
