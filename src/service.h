#pragma once

#include "hotkey.h"
#include "httpapi.h"
#include "ledproc.h"
#include "serial_out.h"
#include "settings.h"
#include "unicapture.h"
#include <glib.h>
#include <pbnjson.h>
#include <pthread.h>

#define APP_ID "com.ambilite.tv"
#define SERVICE_NAME "com.ambilite.tv.service"
#define AUTOSTART_DIR "/var/lib/webosbrew/init.d"
#define AUTOSTART_LINK AUTOSTART_DIR "/ambiliteautostart"
#define HBCHANNEL_DIR "/media/developer/apps/usr/palm/services/org.webosbrew.hbchannel.service"
#define UPDATE_STATE_FILE "/tmp/ambilite-update.state"

typedef struct {
    bool running; // output active (video capture or colour/effect mode)
    bool video_active; // running with picture capture
    bool power_paused;

    capture_backend_t ui_backend;
    capture_backend_t video_backend;
    bool ui_backend_initialized;
    bool video_backend_initialized;
    unicapture_state_t unicapture;

    settings_t* settings;
    char service_dir[256];
    char leds_source[300];

    ledproc_t lp;
    serial_out_t serial;
    uint8_t* colors;

    // colour / effect modes
    pthread_t effect_thread;
    volatile bool effect_running;
    uint8_t* effect_rgb;
    float* effect_state;

    // HDR
    char hdr_type[32];
    bool hdr_active;

    hotkey_t hotkey;
    httpapi_t http;

    volatile bool test_running;
    pthread_t test_thread;

    volatile double proc_us_avg;
    volatile unsigned long frames;

    GMainLoop* loop;
} service_t;

int service_init(service_t* service, settings_t* settings, const char* service_dir);
int service_register(service_t* service, GMainLoop* loop);
int service_start(service_t* service);
int service_stop(service_t* service);
int service_destroy(service_t* service);
void service_apply_color_params(service_t* service);
void service_update_autostart(service_t* service);
int service_run_test_pattern(service_t* service, bool wait);
