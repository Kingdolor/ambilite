// Ambilite service: capture (unicapture backends) -> LED colours -> USB serial.
// Also: colour/effect modes, HDR profile switching, Luna bus API for the TV app,
// HTTP API for the iPhone app, power-state handling, remote hotkey, self-update.
#define _GNU_SOURCE
#include "service.h"
#include "effects.h"
#include "log.h"
#include "utils.h"
#include "version.h"
#include <errno.h>
#include <fcntl.h>
#include <luna-service2/lunaservice.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern bool LSRegisterPubPriv(const char* name, LSHandle** sh, bool public_bus, LSError* lserror) __attribute__((weak));

static LSHandle* g_handle = NULL;

static bool mode_is_video(service_t* s)
{
    return strcmp(s->settings->mode, "video") == 0;
}

// ================================================================ frame path

static int service_feed_frame(void* data, int width, int height, uint8_t* rgb)
{
    service_t* s = (service_t*)data;
    uint64_t t0 = getticks_us();
    int count = s->lp.count;
    if (count <= 0 || !s->colors)
        return 0;

    ledproc_process(&s->lp, width, height, rgb, s->colors, t0);
    serial_out_send(&s->serial, s->colors, count);

    double dt = (double)(getticks_us() - t0);
    s->proc_us_avg = s->proc_us_avg * 0.95 + dt * 0.05;
    s->frames++;
    return 0;
}

static void* effect_loop(void* data)
{
    service_t* s = (service_t*)data;
    uint64_t start = getticks_us();
    while (s->effect_running) {
        uint64_t t0 = getticks_us();
        int n = s->lp.count;
        effects_render(&s->lp, s->settings->mode, s->settings->effect, s->settings->color, s->settings->effect_speed,
            (double)(t0 - start) / 1e6, s->effect_rgb, s->effect_state);
        ledproc_finish_plain(&s->lp, s->effect_rgb, s->colors);
        serial_out_send(&s->serial, s->colors, n);
        s->frames++;
        uint64_t el = getticks_us() - t0;
        if (el < 33000)
            usleep((useconds_t)(33000 - el));
    }
    return NULL;
}

// ================================================================ setup

void service_apply_color_params(service_t* s)
{
    const color_profile_t* prof = (s->settings->hdr_auto && s->hdr_active) ? &s->settings->hdr : &s->settings->sdr;
    ledproc_params_t p;
    memset(&p, 0, sizeof(p));
    p.saturation = (float)prof->saturation;
    p.gamma = (float)prof->gamma;
    p.brightness = prof->brightness;
    p.blackborder = s->settings->blackborder;
    p.blackborder_threshold = s->settings->blackborder_threshold;
    snprintf(p.color_order, sizeof(p.color_order), "%s", s->settings->color_order ? s->settings->color_order : "rgb");
    p.smoothing_mode = s->settings->smoothing_mode;
    p.smoothing_ms = s->settings->smoothing_ms;
    p.adaptive_strength = s->settings->adaptive_strength;
    p.release_ms = s->settings->release_ms;
    p.spatial = s->settings->spatial;
    ledproc_set_params(&s->lp, &p);
}

static void load_layout(service_t* s)
{
    int n = ledproc_load_layout(&s->lp, LEDS_USER_PATH);
    if (n > 0) {
        snprintf(s->leds_source, sizeof(s->leds_source), "%s", LEDS_USER_PATH);
    } else {
        char path[300];
        snprintf(path, sizeof(path), "%s/leds.json", s->service_dir);
        n = ledproc_load_layout(&s->lp, path);
        snprintf(s->leds_source, sizeof(s->leds_source), "%s", path);
    }
    if (n <= 0) {
        ERR("No LED layout found (%d)", n);
        return;
    }
    free(s->colors);
    free(s->effect_rgb);
    free(s->effect_state);
    s->colors = calloc((size_t)n, 3);
    s->effect_rgb = calloc((size_t)n, 3);
    s->effect_state = calloc((size_t)n * 2, sizeof(float));
    INFO("LED layout: %d LEDs from %s", n, s->leds_source);
}

int service_init(service_t* s, settings_t* settings, const char* service_dir)
{
    s->settings = settings;
    snprintf(s->service_dir, sizeof(s->service_dir), "%s", service_dir);
    snprintf(s->hdr_type, sizeof(s->hdr_type), "sdr");
    ledproc_init(&s->lp);
    load_layout(s);
    service_apply_color_params(s);
    serial_out_init(&s->serial, settings->serial_device, settings->baudrate, settings->awa_mode);
    unicapture_init(&s->unicapture);
    s->http.listen_fd = -1;
    return 0;
}

static void init_backends(service_t* s)
{
    settings_t* st = s->settings;
    cap_backend_config_t config;
    memset(&config, 0, sizeof(config));
    config.resolution_width = st->width;
    config.resolution_height = st->height;
    config.fps = st->fps;
    config.quirks = st->quirks;

    const char* const ui_backends[] = { "libgm_backend.so", "libhalgal_backend.so", NULL };
    const char* const video_backends[] = { "libvtcapture_backend.so", "libdile_vt_backend.so", NULL };
    char name[FILENAME_MAX];

    if (!s->ui_backend_initialized) {
        s->unicapture.ui_capture = NULL;
        if (!st->no_gui) {
            int r;
            if (!st->ui_backend || !st->ui_backend[0] || strcmp(st->ui_backend, "auto") == 0) {
                r = unicapture_try_backends(&config, &s->ui_backend, ui_backends);
            } else {
                snprintf(name, sizeof(name), "%s_backend.so", st->ui_backend);
                r = unicapture_init_backend(&config, &s->ui_backend, name);
            }
            if (r == 0) {
                s->unicapture.ui_capture = &s->ui_backend;
                s->ui_backend_initialized = true;
            }
        }
    }
    if (!s->video_backend_initialized) {
        s->unicapture.video_capture = NULL;
        int r;
        if (!st->video_backend || !st->video_backend[0] || strcmp(st->video_backend, "auto") == 0) {
            r = unicapture_try_backends(&config, &s->video_backend, video_backends);
        } else {
            snprintf(name, sizeof(name), "%s_backend.so", st->video_backend);
            r = unicapture_init_backend(&config, &s->video_backend, name);
        }
        if (r == 0) {
            s->unicapture.video_capture = &s->video_backend;
            s->video_backend_initialized = true;
        }
    }
}

static void destroy_backends(service_t* s)
{
    if (s->ui_backend_initialized && s->ui_backend.cleanup) {
        s->ui_backend.cleanup(s->ui_backend.state);
        s->ui_backend_initialized = false;
    }
    if (s->video_backend_initialized && s->video_backend.cleanup) {
        s->video_backend.cleanup(s->video_backend.state);
        s->video_backend_initialized = false;
    }
}

int service_start(service_t* s)
{
    if (s->running)
        return 1;
    while (s->test_running)
        usleep(50000);

    serial_out_free(&s->serial);
    serial_out_init(&s->serial, s->settings->serial_device, s->settings->baudrate, s->settings->awa_mode);
    serial_out_open(&s->serial); // failure is fine, send() retries

    if (!mode_is_video(s)) {
        s->effect_running = true;
        if (pthread_create(&s->effect_thread, NULL, effect_loop, s) != 0) {
            s->effect_running = false;
            serial_out_close(&s->serial, s->lp.count);
            return -3;
        }
        s->running = true;
        s->video_active = false;
        INFO("Light mode started: %s", s->settings->mode);
        return 0;
    }

    s->unicapture.vsync = s->settings->vsync;
    s->unicapture.fps = s->settings->fps;
    s->unicapture.target_format = PIXFMT_RGB;
    s->unicapture.callback = &service_feed_frame;
    s->unicapture.callback_nv12 = NULL;
    s->unicapture.callback_data = s;
    s->unicapture.dump_frames = false;

    init_backends(s);
    if (!s->unicapture.video_capture && !s->unicapture.ui_capture) {
        ERR("No capture backend available (root missing?)");
        serial_out_close(&s->serial, s->lp.count);
        return -2;
    }

    s->running = true;
    int r = unicapture_start(&s->unicapture);
    if (r != 0) {
        s->running = false;
        destroy_backends(s);
        serial_out_close(&s->serial, s->lp.count);
        return r;
    }
    s->video_active = true;
    INFO("Ambilight started");
    return 0;
}

int service_stop(service_t* s)
{
    if (!s->running)
        return 1;
    if (s->video_active) {
        unicapture_stop(&s->unicapture);
        destroy_backends(s);
    }
    if (s->effect_running) {
        s->effect_running = false;
        pthread_join(s->effect_thread, NULL);
    }
    s->running = false;
    s->video_active = false;
    serial_out_close(&s->serial, s->lp.count);
    INFO("Output stopped");
    return 0;
}

static void service_restart(service_t* s)
{
    if (s->running) {
        service_stop(s);
        service_start(s);
    }
}

int service_destroy(service_t* s)
{
    httpapi_stop(&s->http);
    hotkey_stop(&s->hotkey);
    service_stop(s);
    while (s->test_running)
        usleep(50000);
    serial_out_free(&s->serial);
    ledproc_free(&s->lp);
    free(s->colors);
    free(s->effect_rgb);
    free(s->effect_state);
    s->colors = NULL;
    s->effect_rgb = NULL;
    s->effect_state = NULL;
    return 0;
}

void service_update_autostart(service_t* s)
{
    char script[320];
    snprintf(script, sizeof(script), "%s/ambiliteautostart", s->service_dir);
    if (unlink(AUTOSTART_LINK) != 0 && errno != ENOENT)
        WARN("Removing autostart link failed: %s", strerror(errno));
    if (s->settings->autostart) {
        mkdir("/var/lib/webosbrew", 0755);
        mkdir(AUTOSTART_DIR, 0755);
        if (symlink(script, AUTOSTART_LINK) != 0)
            WARN("Creating autostart link failed: %s", strerror(errno));
    }
}

static void save_settings(service_t* s)
{
    if (settings_save_file(s->settings, CONFIG_PATH) != 0)
        WARN("Settings save failed");
}

// ================================================================ test pattern

static void* test_pattern_thread(void* data)
{
    service_t* s = (service_t*)data;
    int n = s->lp.count;
    uint8_t* buf = malloc((size_t)n * 3);
    serial_out_t so;
    serial_out_init(&so, s->settings->serial_device, s->settings->baudrate, s->settings->awa_mode);
    if (serial_out_open(&so) == 0) {
        const uint8_t seq[4][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 255 } };
        for (int c = 0; c < 4; c++) {
            uint8_t ordered[3];
            for (int k = 0; k < 3; k++)
                ordered[k] = seq[c][s->lp.order_idx[k]];
            for (int i = 0; i < n; i++)
                memcpy(buf + i * 3, ordered, 3);
            for (int t = 0; t < 25; t++) {
                serial_out_send(&so, buf, n);
                usleep(40000);
            }
        }
        for (int i = 0; i < n; i += 2) {
            memset(buf, 0, (size_t)n * 3);
            for (int k = 0; k < 6 && i + k < n; k++)
                memset(buf + (i + k) * 3, 255, 3);
            serial_out_send(&so, buf, n);
            usleep(12000);
        }
        serial_out_close(&so, n);
    }
    serial_out_free(&so);
    free(buf);
    s->test_running = false;
    return NULL;
}

int service_run_test_pattern(service_t* s, bool wait)
{
    if (s->running || s->test_running || s->lp.count <= 0)
        return -1;
    s->test_running = true;
    if (pthread_create(&s->test_thread, NULL, test_pattern_thread, s) != 0) {
        s->test_running = false;
        return -1;
    }
    if (wait)
        pthread_join(s->test_thread, NULL);
    else
        pthread_detach(s->test_thread);
    return 0;
}

// ================================================================ update / install

static bool is_hex64(const char* h)
{
    if (!h || strlen(h) != 64)
        return false;
    for (int i = 0; i < 64; i++) {
        char c = h[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

static bool safe_url(const char* u)
{
    if (!u || strncmp(u, "https://github.com/", 19) != 0 || strlen(u) > 400)
        return false;
    for (const char* p = u; *p; p++)
        if (*p == '\'' || *p == '"' || *p == '\\' || *p == ' ' || *p == '`' || *p == '$')
            return false;
    return strstr(u, "/releases/download/") != NULL;
}

static const char* UPDATE_SCRIPT =
    "#!/bin/sh\n"
    "# Ambilite self-update: runs detached so it survives the service being killed during install\n"
    "URL=\"$1\"; HASH=\"$2\"; OUT=/tmp/ambilite-update.out; STATE=" UPDATE_STATE_FILE "\n"
    "echo running > $STATE; : > $OUT\n"
    "luna-send -i 'luna://org.webosbrew.hbchannel.service/install' \"{\\\"ipkUrl\\\":\\\"$URL\\\",\\\"ipkHash\\\":\\\"$HASH\\\"}\" > $OUT 2>&1 &\n"
    "PID=$!; OK=0; i=0\n"
    "while [ $i -lt 300 ]; do\n"
    "  sleep 1; i=$((i+1))\n"
    "  if grep -q '\"finished\":true' $OUT; then OK=1; break; fi\n"
    "  if grep -q '\"returnValue\":false' $OUT; then break; fi\n"
    "done\n"
    "kill $PID 2>/dev/null\n"
    "if [ $OK = 1 ]; then\n"
    "  " HBCHANNEL_DIR "/elevate-service " SERVICE_NAME " >/dev/null 2>&1\n"
    "  killall ambilite 2>/dev/null; sleep 2\n"
    "  luna-send -n 1 'luna://" SERVICE_NAME "/status' '{}' >/dev/null 2>&1\n"
    "  echo done > $STATE\n"
    "else\n"
    "  echo failed > $STATE\n"
    "fi\n";

static int service_install(service_t* s __attribute__((unused)), const char* url, const char* sha256)
{
    if (!safe_url(url) || !is_hex64(sha256))
        return -1;
    const char* path = "/tmp/ambilite-update.sh";
    FILE* f = fopen(path, "w");
    if (!f)
        return -2;
    fputs(UPDATE_SCRIPT, f);
    fclose(f);
    chmod(path, 0755);

    pid_t pid = fork();
    if (pid < 0)
        return -3;
    if (pid == 0) {
        // double fork + new session: the updater must survive our own SIGKILL during install
        setsid();
        if (fork() != 0)
            _exit(0);
        int devnull = open("/dev/null", O_RDWR);
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        for (int fd = 3; fd < 256; fd++)
            close(fd);
        execl("/bin/sh", "sh", path, url, sha256, (char*)NULL);
        _exit(127);
    }
    waitpid(pid, NULL, 0);
    INFO("Update started: %s", url);
    return 0;
}

static const char* read_update_state(char* buf, size_t len)
{
    FILE* f = fopen(UPDATE_STATE_FILE, "r");
    if (!f) {
        snprintf(buf, len, "idle");
        return buf;
    }
    if (!fgets(buf, (int)len, f))
        snprintf(buf, len, "idle");
    fclose(f);
    buf[strcspn(buf, "\r\n")] = 0;
    return buf;
}

// ================================================================ shared JSON logic

static jvalue_ref build_status(service_t* s)
{
    char upd[32];
    jvalue_ref o = jobject_create();
    jobject_set(o, j_cstr_to_buffer("returnValue"), jboolean_create(true));
    jobject_set(o, j_cstr_to_buffer("version"), jstring_create(AMBILITE_VERSION));
    jobject_set(o, j_cstr_to_buffer("elevated"), jboolean_create(getuid() == 0));
    jobject_set(o, j_cstr_to_buffer("enabled"), jboolean_create(s->settings->enabled));
    jobject_set(o, j_cstr_to_buffer("mode"), jstring_create(s->settings->mode));
    jobject_set(o, j_cstr_to_buffer("isRunning"), jboolean_create(s->running));
    jobject_set(o, j_cstr_to_buffer("videoActive"), jboolean_create(s->video_active));
    jobject_set(o, j_cstr_to_buffer("powerPaused"), jboolean_create(s->power_paused));
    jobject_set(o, j_cstr_to_buffer("videoBackend"), s->video_backend.name ? jstring_create(s->video_backend.name) : jnull());
    jobject_set(o, j_cstr_to_buffer("uiBackend"), s->ui_backend.name ? jstring_create(s->ui_backend.name) : jnull());
    jobject_set(o, j_cstr_to_buffer("framerate"), jnumber_create_f64(s->video_active ? s->unicapture.metrics.framerate : 0));
    jobject_set(o, j_cstr_to_buffer("procUs"), jnumber_create_f64(s->proc_us_avg));
    jobject_set(o, j_cstr_to_buffer("ledCount"), jnumber_create_i32(s->lp.count));
    jobject_set(o, j_cstr_to_buffer("ledsSource"), jstring_create(s->leds_source));
    jobject_set(o, j_cstr_to_buffer("serialOpen"), jboolean_create(s->serial.fd >= 0));
    jobject_set(o, j_cstr_to_buffer("serialPath"), jstring_create(s->serial.path));
    jobject_set(o, j_cstr_to_buffer("serialErrors"), jnumber_create_i32((int)s->serial.frames_err));
    jobject_set(o, j_cstr_to_buffer("borderTop"), jnumber_create_i32(s->lp.bt));
    jobject_set(o, j_cstr_to_buffer("borderLeft"), jnumber_create_i32(s->lp.bl));
    jobject_set(o, j_cstr_to_buffer("sceneCuts"), jnumber_create_i32((int)s->lp.scene_cuts));
    jobject_set(o, j_cstr_to_buffer("hdrType"), jstring_create(s->hdr_type));
    jobject_set(o, j_cstr_to_buffer("hdrActive"), jboolean_create(s->hdr_active));
    jobject_set(o, j_cstr_to_buffer("profile"), jstring_create(s->settings->hdr_auto && s->hdr_active ? "hdr" : "sdr"));
    jobject_set(o, j_cstr_to_buffer("hotkey"), jnumber_create_i32(s->settings->hotkey));
    jobject_set(o, j_cstr_to_buffer("hotkeyLearning"), jboolean_create(s->hotkey.learning));
    jobject_set(o, j_cstr_to_buffer("lastKey"), jnumber_create_i32(s->hotkey.last_key));
    jobject_set(o, j_cstr_to_buffer("apiPort"), jnumber_create_i32(s->http.running ? s->http.port : 0));
    jobject_set(o, j_cstr_to_buffer("updateState"), jstring_create(read_update_state(upd, sizeof(upd))));
    return o;
}

static bool set_enabled(service_t* s, bool on)
{
    s->settings->enabled = on;
    s->power_paused = false;
    save_settings(s);
    return on ? service_start(s) >= 0 : service_stop(s) >= 0;
}

static bool needs_restart(const settings_t* a, const settings_t* b)
{
    return a->width != b->width || a->height != b->height || a->fps != b->fps || a->vsync != b->vsync
        || a->no_gui != b->no_gui || a->quirks != b->quirks || a->baudrate != b->baudrate || a->awa_mode != b->awa_mode
        || strcmp(a->serial_device, b->serial_device) != 0 || strcmp(a->video_backend, b->video_backend) != 0
        || strcmp(a->ui_backend, b->ui_backend) != 0 || strcmp(a->mode, b->mode) != 0;
}

static void hotkey_cb(void* data, int code, bool learned);

static void apply_settings_patch(service_t* s, jvalue_ref patch)
{
    settings_t before;
    settings_copy(&before, s->settings);

    // the pairing code can only be changed locally (luna), see m_set_settings
    char* keep_key = strdup(s->settings->api_key);
    bool keep_require = s->settings->api_require_key;
    settings_load_json(s->settings, patch);
    free(s->settings->api_key);
    s->settings->api_key = keep_key;
    s->settings->api_require_key = keep_require;
    save_settings(s);

    if (before.autostart != s->settings->autostart)
        service_update_autostart(s);
    s->hotkey.hotkey = s->settings->hotkey;
    if (s->settings->hotkey != 0)
        hotkey_start(&s->hotkey, s->settings->hotkey, hotkey_cb, s);

    service_apply_color_params(s);

    if (needs_restart(&before, s->settings) && s->running) {
        INFO("Capture/output/mode settings changed – restarting");
        service_restart(s);
    }
    if (before.enabled != s->settings->enabled) {
        if (s->settings->enabled)
            service_start(s);
        else
            service_stop(s);
    }
    settings_free(&before);
}

static jvalue_ref parse_json(const char* str)
{
    JSchemaInfo schema;
    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    return jdom_parse(j_cstr_to_buffer(str ? str : ""), DOMOPT_NOOPT, &schema);
}

static char* json_get_str(jvalue_ref obj, const char* key)
{
    jvalue_ref v = jobject_get(obj, j_cstr_to_buffer(key));
    if (!jis_valid(v) || !jis_string(v))
        return NULL;
    raw_buffer b = jstring_get(v);
    char* r = strdup(b.m_str);
    jstring_free_buffer(b);
    return r;
}

static int do_install_json(service_t* s, jvalue_ref req)
{
    char* url = json_get_str(req, "url");
    char* sha = json_get_str(req, "sha256");
    int r = service_install(s, url, sha);
    free(url);
    free(sha);
    return r;
}

// ================================================================ HTTP API (runs on main loop)

typedef struct {
    service_t* s;
    const char* method;
    const char* path;
    const char* body;
    char* resp;
    int code;
    bool done;
    pthread_mutex_t m;
    pthread_cond_t c;
} http_call_t;

static char* obj_to_str(jvalue_ref o)
{
    char* r = strdup(jvalue_tostring_simple(o));
    j_release(&o);
    return r;
}

static char* ok_json(bool ok)
{
    return strdup(ok ? "{\"returnValue\":true}" : "{\"returnValue\":false}");
}

static gboolean http_on_main(gpointer data)
{
    http_call_t* c = (http_call_t*)data;
    service_t* s = c->s;
    bool get = strcmp(c->method, "GET") == 0;
    bool post = strcmp(c->method, "POST") == 0;
    c->code = 200;

    if (get && strcmp(c->path, "/api/status") == 0) {
        c->resp = obj_to_str(build_status(s));
    } else if (get && strcmp(c->path, "/api/settings") == 0) {
        jvalue_ref o = jobject_create();
        jobject_set(o, j_cstr_to_buffer("returnValue"), jboolean_create(true));
        settings_save_json(s->settings, o, false);
        c->resp = obj_to_str(o);
    } else if (post && strcmp(c->path, "/api/settings") == 0) {
        jvalue_ref p = parse_json(c->body);
        if (jis_null(p) || !jis_object(p)) {
            c->code = 400;
            c->resp = strdup("{\"error\":\"invalid json\"}");
        } else {
            apply_settings_patch(s, p);
            jvalue_ref o = jobject_create();
            jobject_set(o, j_cstr_to_buffer("returnValue"), jboolean_create(true));
            settings_save_json(s->settings, o, false);
            c->resp = obj_to_str(o);
        }
        j_release(&p);
    } else if (post && strcmp(c->path, "/api/toggle") == 0) {
        c->resp = ok_json(set_enabled(s, !s->settings->enabled));
    } else if (post && strcmp(c->path, "/api/on") == 0) {
        c->resp = ok_json(set_enabled(s, true));
    } else if (post && strcmp(c->path, "/api/off") == 0) {
        c->resp = ok_json(set_enabled(s, false));
    } else if (post && strcmp(c->path, "/api/test") == 0) {
        bool ok = service_run_test_pattern(s, false) == 0;
        c->code = ok ? 200 : 409;
        c->resp = ok_json(ok);
    } else if (post && strcmp(c->path, "/api/install") == 0) {
        jvalue_ref p = parse_json(c->body);
        int r = jis_null(p) ? -1 : do_install_json(s, p);
        j_release(&p);
        c->code = r == 0 ? 200 : 400;
        c->resp = ok_json(r == 0);
    } else {
        c->code = 404;
        c->resp = strdup("{\"error\":\"not found\"}");
    }

    pthread_mutex_lock(&c->m);
    c->done = true;
    pthread_cond_signal(&c->c);
    pthread_mutex_unlock(&c->m);
    return G_SOURCE_REMOVE;
}

static int http_handler(void* ctx, const char* method, const char* path, const char* key, const char* body, char** resp)
{
    service_t* s = (service_t*)ctx;
    if (strcmp(path, "/api/ping") == 0) {
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"name\":\"Ambilite\",\"version\":\"%s\",\"ledCount\":%d,\"needsKey\":%s}", AMBILITE_VERSION,
            s->lp.count, s->settings->api_require_key ? "true" : "false");
        *resp = strdup(buf);
        return 200;
    }
    if (s->settings->api_require_key && (!s->settings->api_key[0] || strcmp(key, s->settings->api_key) != 0)) {
        *resp = strdup("{\"error\":\"wrong or missing pairing code\"}");
        return 401;
    }

    http_call_t c;
    memset(&c, 0, sizeof(c));
    c.s = s;
    c.method = method;
    c.path = path;
    c.body = body;
    pthread_mutex_init(&c.m, NULL);
    pthread_cond_init(&c.c, NULL);
    g_idle_add(http_on_main, &c);
    pthread_mutex_lock(&c.m);
    while (!c.done)
        pthread_cond_wait(&c.c, &c.m);
    pthread_mutex_unlock(&c.m);
    pthread_mutex_destroy(&c.m);
    pthread_cond_destroy(&c.c);
    *resp = c.resp;
    return c.code;
}

// ================================================================ luna methods

static void reply_obj(LSHandle* sh, LSMessage* msg, jvalue_ref obj)
{
    LSError err;
    LSErrorInit(&err);
    LSMessageReply(sh, msg, jvalue_tostring_simple(obj), &err);
    LSErrorFree(&err);
}

static void reply_ok(LSHandle* sh, LSMessage* msg, bool ok)
{
    jvalue_ref o = jobject_create();
    jobject_set(o, j_cstr_to_buffer("returnValue"), jboolean_create(ok));
    reply_obj(sh, msg, o);
    j_release(&o);
}

static bool m_start(LSHandle* sh, LSMessage* msg, void* data)
{
    reply_ok(sh, msg, set_enabled((service_t*)data, true));
    return true;
}

static bool m_stop(LSHandle* sh, LSMessage* msg, void* data)
{
    reply_ok(sh, msg, set_enabled((service_t*)data, false));
    return true;
}

static bool m_toggle(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* s = (service_t*)data;
    reply_ok(sh, msg, set_enabled(s, !s->settings->enabled));
    return true;
}

static bool m_status(LSHandle* sh, LSMessage* msg, void* data)
{
    jvalue_ref o = build_status((service_t*)data);
    reply_obj(sh, msg, o);
    j_release(&o);
    return true;
}

static bool m_get_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* s = (service_t*)data;
    jvalue_ref o = jobject_create();
    jobject_set(o, j_cstr_to_buffer("returnValue"), jboolean_create(true));
    settings_save_json(s->settings, o, true); // local TV app may show the pairing code
    reply_obj(sh, msg, o);
    j_release(&o);
    return true;
}

static bool m_set_settings(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* s = (service_t*)data;
    jvalue_ref parsed = parse_json(LSMessageGetPayload(msg));
    if (jis_null(parsed)) {
        j_release(&parsed);
        reply_ok(sh, msg, false);
        return true;
    }
    char* new_key = json_get_str(parsed, "api_key");
    jvalue_ref req = jobject_get(parsed, j_cstr_to_buffer("api_require_key"));
    apply_settings_patch(s, parsed);
    if (jis_valid(req) && jis_boolean(req)) { // only the TV app may switch the code requirement
        jboolean_get(req, &s->settings->api_require_key);
        save_settings(s);
    }
    if (new_key) { // only the TV app may (re)set the pairing code
        free(s->settings->api_key);
        s->settings->api_key = new_key;
        save_settings(s);
    }
    j_release(&parsed);
    reply_ok(sh, msg, true);
    return true;
}

static bool m_test_pattern(LSHandle* sh, LSMessage* msg, void* data)
{
    reply_ok(sh, msg, service_run_test_pattern((service_t*)data, false) == 0);
    return true;
}

static bool m_learn_hotkey(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* s = (service_t*)data;
    hotkey_start(&s->hotkey, s->settings->hotkey, hotkey_cb, s);
    s->hotkey.learning = true;
    reply_ok(sh, msg, true);
    return true;
}

static bool m_reload_layout(LSHandle* sh, LSMessage* msg, void* data)
{
    service_t* s = (service_t*)data;
    bool was = s->running;
    if (was)
        service_stop(s);
    load_layout(s);
    service_apply_color_params(s);
    if (was)
        service_start(s);
    reply_ok(sh, msg, s->lp.count > 0);
    return true;
}

static bool m_install(LSHandle* sh, LSMessage* msg, void* data)
{
    jvalue_ref p = parse_json(LSMessageGetPayload(msg));
    int r = jis_null(p) ? -1 : do_install_json((service_t*)data, p);
    j_release(&p);
    reply_ok(sh, msg, r == 0);
    return true;
}

static LSMethod methods[] = {
    { "start", m_start, LUNA_METHOD_FLAGS_NONE },
    { "stop", m_stop, LUNA_METHOD_FLAGS_NONE },
    { "toggle", m_toggle, LUNA_METHOD_FLAGS_NONE },
    { "status", m_status, LUNA_METHOD_FLAGS_NONE },
    { "getSettings", m_get_settings, LUNA_METHOD_FLAGS_NONE },
    { "setSettings", m_set_settings, LUNA_METHOD_FLAGS_NONE },
    { "testPattern", m_test_pattern, LUNA_METHOD_FLAGS_NONE },
    { "learnHotkey", m_learn_hotkey, LUNA_METHOD_FLAGS_NONE },
    { "reloadLayout", m_reload_layout, LUNA_METHOD_FLAGS_NONE },
    { "install", m_install, LUNA_METHOD_FLAGS_NONE },
    { 0, 0, 0 }
};

// ================================================================ hotkey -> main loop

typedef struct {
    service_t* s;
    int code;
    bool learned;
} hotkey_evt_t;

static gboolean hotkey_idle(gpointer data)
{
    hotkey_evt_t* e = (hotkey_evt_t*)data;
    service_t* s = e->s;
    if (e->learned) {
        s->settings->hotkey = e->code;
        save_settings(s);
    } else {
        INFO("Hotkey %d pressed – toggling", e->code);
        set_enabled(s, !s->settings->enabled);
    }
    free(e);
    return G_SOURCE_REMOVE;
}

static void hotkey_cb(void* data, int code, bool learned)
{
    hotkey_evt_t* e = malloc(sizeof(*e));
    e->s = (service_t*)data;
    e->code = code;
    e->learned = learned;
    g_idle_add(hotkey_idle, e);
}

// ================================================================ luna subscriptions

static jvalue_ref parse_msg(LSMessage* msg)
{
    return parse_json(LSMessageGetPayload(msg));
}

static bool power_callback(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    service_t* s = (service_t*)data;
    jvalue_ref parsed = parse_msg(msg);
    if (jis_null(parsed)) {
        j_release(&parsed);
        return true;
    }
    char* state = json_get_str(parsed, "state");
    j_release(&parsed);
    if (!state)
        return true;
    bool active = strcmp(state, "Active") == 0;
    INFO("Power state: %s", state);
    free(state);

    if (active) {
        if (s->power_paused && s->settings->enabled && !s->running)
            service_start(s);
        s->power_paused = false;
    } else if (s->settings->enabled && !s->power_paused) {
        s->power_paused = true;
        service_stop(s);
    }
    return true;
}

static void set_hdr(service_t* s, const char* type)
{
    bool hdr = strcasestr(type, "hdr") != NULL || strcasestr(type, "dolby") != NULL || strcasestr(type, "hlg") != NULL;
    if (strcmp(type, "none") == 0 || strcasecmp(type, "sdr") == 0)
        hdr = false;
    if (hdr != s->hdr_active || strcmp(s->hdr_type, type) != 0) {
        INFO("Dynamic range: %s -> profile %s", type, hdr ? "HDR" : "SDR");
        snprintf(s->hdr_type, sizeof(s->hdr_type), "%s", type);
        s->hdr_active = hdr;
        service_apply_color_params(s);
    }
}

static bool videooutput_callback(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    jvalue_ref parsed = parse_msg(msg);
    if (jis_null(parsed)) {
        j_release(&parsed);
        return true;
    }
    jvalue_ref video = jobject_get(parsed, j_cstr_to_buffer("video"));
    if (jis_valid(video) && jis_array(video)) {
        jvalue_ref v0 = jarray_get(video, 0);
        jvalue_ref info = jis_valid(v0) ? jobject_get(v0, j_cstr_to_buffer("videoInfo")) : jinvalid();
        if (jis_valid(info)) {
            char* t = json_get_str(info, "hdrType");
            if (t) {
                set_hdr((service_t*)data, t);
                free(t);
            }
        }
    }
    j_release(&parsed);
    return true;
}

static bool picture_callback(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    jvalue_ref parsed = parse_msg(msg);
    if (jis_null(parsed)) {
        j_release(&parsed);
        return true;
    }
    jvalue_ref dim = jobject_get(parsed, j_cstr_to_buffer("dimension"));
    if (jis_valid(dim)) {
        char* t = json_get_str(dim, "dynamicRange");
        if (t) {
            set_hdr((service_t*)data, t);
            free(t);
        }
    }
    j_release(&parsed);
    return true;
}

// If we were (re)started without root – e.g. after an update – ask the Homebrew
// Channel to elevate us and start again. Rate-limited to avoid loops.
static bool elevate_reply(LSHandle* sh __attribute__((unused)), LSMessage* msg, void* data)
{
    service_t* s = (service_t*)data;
    const char* payload = LSMessageGetPayload(msg);
    INFO("Elevation reply: %s", payload);
    if (payload && strstr(payload, "\"returnValue\":true") && s->loop) {
        INFO("Elevated – exiting so we get restarted as root");
        g_main_loop_quit(s->loop);
    }
    return true;
}

static void try_self_elevate(service_t* s, LSHandle* h)
{
    const char* marker = "/tmp/ambilite-elevate-attempt";
    struct stat st;
    if (stat(marker, &st) == 0 && time(NULL) - st.st_mtime < 120) {
        WARN("Not root, elevation attempted recently – waiting for TV app");
        return;
    }
    FILE* f = fopen(marker, "w");
    if (f)
        fclose(f);
    LSError err;
    LSErrorInit(&err);
    const char* payload = "{\"command\":\"" HBCHANNEL_DIR "/elevate-service " SERVICE_NAME
                          "; (sleep 3; luna-send -n 1 luna://" SERVICE_NAME "/status '{}') >/dev/null 2>&1 &\"}";
    if (!LSCall(h, "luna://org.webosbrew.hbchannel.service/exec", payload, elevate_reply, s, NULL, &err))
        WARN("Self-elevation call failed: %s", err.message);
    LSErrorFree(&err);
}

static void ensure_api_key(service_t* s)
{
    if (s->settings->api_key && s->settings->api_key[0])
        return;
    unsigned int r = 0;
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&r, sizeof(r), 1, f) != 1)
            r = (unsigned int)getticks_us();
        fclose(f);
    } else {
        r = (unsigned int)getticks_us();
    }
    char key[8];
    snprintf(key, sizeof(key), "%06u", r % 1000000u);
    free(s->settings->api_key);
    s->settings->api_key = strdup(key);
    save_settings(s);
    INFO("New pairing code generated");
}

int service_register(service_t* s, GMainLoop* loop)
{
    LSHandle* handle = NULL;
    LSHandle* legacy = NULL;
    LSError err;
    LSErrorInit(&err);
    s->loop = loop;

    bool ok = LSRegister(SERVICE_NAME, &handle, &err);
    bool ok_legacy = false;
    if (&LSRegisterPubPriv != NULL)
        ok_legacy = LSRegisterPubPriv(SERVICE_NAME, &legacy, true, &err);
    if (!ok && !ok_legacy) {
        ERR("Unable to register on Luna bus: %s", err.message);
        LSErrorFree(&err);
        return -1;
    }

    LSHandle* handles[2] = { ok ? handle : NULL, ok_legacy ? legacy : NULL };
    for (int i = 0; i < 2; i++) {
        if (!handles[i])
            continue;
        LSRegisterCategory(handles[i], "/", methods, NULL, NULL, &err);
        LSCategorySetData(handles[i], "/", s, &err);
        LSGmainAttach(handles[i], loop, &err);
    }
    // subscriptions only once (first working handle)
    g_handle = handles[0] ? handles[0] : handles[1];
    if (!LSCall(g_handle, "luna://com.webos.service.tvpower/power/getPowerState", "{\"subscribe\":true}",
            power_callback, s, NULL, &err))
        WARN("Power state subscription failed: %s", err.message);
    if (!LSCall(g_handle, "luna://com.webos.service.videooutput/getStatus", "{\"subscribe\":true}",
            videooutput_callback, s, NULL, &err))
        WARN("videooutput subscription failed: %s", err.message);
    if (!LSCall(g_handle, "luna://com.webos.settingsservice/getSystemSettings",
            "{\"category\":\"picture\",\"subscribe\":true}", picture_callback, s, NULL, &err))
        WARN("picture settings subscription failed: %s", err.message);
    LSErrorFree(&err);

    if (getuid() != 0)
        try_self_elevate(s, g_handle);

    if (getuid() == 0)
        ensure_api_key(s);
    snprintf(s->http.docroot, sizeof(s->http.docroot), "%s/www", s->service_dir);
    if (s->settings->api_enabled && httpapi_start(&s->http, s->settings->api_port, http_handler, s) != 0)
        WARN("HTTP API could not be started");

    if (s->settings->hotkey != 0)
        hotkey_start(&s->hotkey, s->settings->hotkey, hotkey_cb, s);
    return 0;
}
