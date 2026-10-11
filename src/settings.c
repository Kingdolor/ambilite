#include "settings.h"
#include "log.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void settings_init(settings_t* s)
{
    memset(s, 0, sizeof(*s));
    s->autostart = true;
    s->enabled = true;
    s->mode = strdup("video");

    s->sdr.brightness = 100;
    s->sdr.saturation = 1.5;
    s->sdr.gamma = 1.5;
    s->hdr.brightness = 100;
    s->hdr.saturation = 1.9;
    s->hdr.gamma = 1.2;
    s->hdr_auto = true;
    s->blackborder = true;
    s->blackborder_threshold = 5;

    s->smoothing_mode = 2;
    s->smoothing_ms = 80;
    s->adaptive_strength = 50;
    s->release_ms = 150;
    s->spatial = 30;

    s->color = 0xFF8A00;
    s->effect = strdup("rainbow");
    s->effect_speed = 4;

    s->serial_device = strdup("auto");
    s->baudrate = 2000000;
    s->awa_mode = true;
    s->color_order = strdup("grb");
    s->width = 320;
    s->height = 180;
    s->fps = 0;
    s->vsync = true;
    s->video_backend = strdup("auto");
    s->ui_backend = strdup("auto");
    s->no_gui = false;
    s->quirks = 0;
    s->hotkey = 0;
    s->api_enabled = true;
    s->api_port = 8780;
    s->api_require_key = false;
    s->api_key = strdup("");
    s->github_repo = strdup("");
}

void settings_free(settings_t* s)
{
    free(s->mode);
    free(s->effect);
    free(s->serial_device);
    free(s->color_order);
    free(s->video_backend);
    free(s->ui_backend);
    free(s->api_key);
    free(s->github_repo);
    s->mode = s->effect = s->serial_device = s->color_order = NULL;
    s->video_backend = s->ui_backend = s->api_key = s->github_repo = NULL;
}

void settings_copy(settings_t* d, const settings_t* s)
{
    *d = *s;
    d->mode = strdup(s->mode);
    d->effect = strdup(s->effect);
    d->serial_device = strdup(s->serial_device);
    d->color_order = strdup(s->color_order);
    d->video_backend = strdup(s->video_backend);
    d->ui_backend = strdup(s->ui_backend);
    d->api_key = strdup(s->api_key);
    d->github_repo = strdup(s->github_repo);
}

static void get_str(jvalue_ref src, const char* key, char** dst)
{
    jvalue_ref v = jobject_get(src, j_cstr_to_buffer(key));
    if (jis_valid(v) && jis_string(v)) {
        raw_buffer b = jstring_get(v);
        free(*dst);
        *dst = strdup(b.m_str);
        jstring_free_buffer(b);
    }
}

static void get_int(jvalue_ref src, const char* key, int* dst)
{
    jvalue_ref v = jobject_get(src, j_cstr_to_buffer(key));
    if (jis_valid(v) && jis_number(v)) {
        double d;
        if (jnumber_get_f64(v, &d) == CONV_OK)
            *dst = (int)(d >= 0 ? d + 0.5 : d - 0.5);
    }
}

static void get_double(jvalue_ref src, const char* key, double* dst)
{
    jvalue_ref v = jobject_get(src, j_cstr_to_buffer(key));
    if (jis_valid(v) && jis_number(v))
        jnumber_get_f64(v, dst);
}

static void get_bool(jvalue_ref src, const char* key, bool* dst)
{
    jvalue_ref v = jobject_get(src, j_cstr_to_buffer(key));
    if (jis_valid(v) && jis_boolean(v))
        jboolean_get(v, dst);
}

static int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static double clampd(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }

static void sanitize_profile(color_profile_t* p)
{
    p->brightness = clampi(p->brightness, 0, 100);
    p->saturation = clampd(p->saturation, 0, 4);
    p->gamma = clampd(p->gamma, 0.2, 4);
}

int settings_load_json(settings_t* s, jvalue_ref src)
{
    get_bool(src, "autostart", &s->autostart);
    get_bool(src, "enabled", &s->enabled);
    get_str(src, "mode", &s->mode);

    // v0.1 keys (flat) map to the SDR profile
    get_int(src, "brightness", &s->sdr.brightness);
    get_double(src, "saturation", &s->sdr.saturation);
    get_double(src, "gamma", &s->sdr.gamma);
    get_int(src, "hdr_brightness", &s->hdr.brightness);
    get_double(src, "hdr_saturation", &s->hdr.saturation);
    get_double(src, "hdr_gamma", &s->hdr.gamma);
    get_bool(src, "hdr_auto", &s->hdr_auto);
    get_bool(src, "blackborder", &s->blackborder);
    get_int(src, "blackborder_threshold", &s->blackborder_threshold);

    get_int(src, "smoothing_mode", &s->smoothing_mode);
    get_int(src, "smoothing_ms", &s->smoothing_ms);
    get_int(src, "adaptive_strength", &s->adaptive_strength);
    get_int(src, "release_ms", &s->release_ms);
    get_int(src, "spatial", &s->spatial);

    get_int(src, "color", &s->color);
    get_str(src, "effect", &s->effect);
    get_int(src, "effect_speed", &s->effect_speed);

    get_str(src, "serial_device", &s->serial_device);
    get_int(src, "baudrate", &s->baudrate);
    get_bool(src, "awa_mode", &s->awa_mode);
    get_str(src, "color_order", &s->color_order);
    get_int(src, "width", &s->width);
    get_int(src, "height", &s->height);
    get_int(src, "fps", &s->fps);
    get_bool(src, "vsync", &s->vsync);
    get_str(src, "backend", &s->video_backend);
    get_str(src, "uibackend", &s->ui_backend);
    get_bool(src, "no_gui", &s->no_gui);
    get_int(src, "quirks", &s->quirks);
    get_int(src, "hotkey", &s->hotkey);
    get_bool(src, "api_enabled", &s->api_enabled);
    get_int(src, "api_port", &s->api_port);
    get_bool(src, "api_require_key", &s->api_require_key);
    get_str(src, "api_key", &s->api_key);
    get_str(src, "github_repo", &s->github_repo);

    sanitize_profile(&s->sdr);
    sanitize_profile(&s->hdr);
    if (strcmp(s->mode, "video") != 0 && strcmp(s->mode, "color") != 0 && strcmp(s->mode, "effect") != 0) {
        free(s->mode);
        s->mode = strdup("video");
    }
    s->smoothing_mode = clampi(s->smoothing_mode, 0, 2);
    s->smoothing_ms = clampi(s->smoothing_ms, 0, 2000);
    s->adaptive_strength = clampi(s->adaptive_strength, 0, 100);
    s->release_ms = clampi(s->release_ms, 0, 2000);
    s->spatial = clampi(s->spatial, 0, 100);
    s->color &= 0xFFFFFF;
    s->effect_speed = clampi(s->effect_speed, 1, 10);
    s->blackborder_threshold = clampi(s->blackborder_threshold, 1, 50);
    if (s->width < 32) s->width = 32;
    if (s->height < 18) s->height = 18;
    s->api_port = clampi(s->api_port, 1024, 65535);
    return 0;
}

#define SET(k, v) jobject_set(t, j_cstr_to_buffer(k), v)

int settings_save_json(settings_t* s, jvalue_ref t, bool include_secret)
{
    SET("autostart", jboolean_create(s->autostart));
    SET("enabled", jboolean_create(s->enabled));
    SET("mode", jstring_create(s->mode));
    SET("brightness", jnumber_create_i32(s->sdr.brightness));
    SET("saturation", jnumber_create_f64(s->sdr.saturation));
    SET("gamma", jnumber_create_f64(s->sdr.gamma));
    SET("hdr_brightness", jnumber_create_i32(s->hdr.brightness));
    SET("hdr_saturation", jnumber_create_f64(s->hdr.saturation));
    SET("hdr_gamma", jnumber_create_f64(s->hdr.gamma));
    SET("hdr_auto", jboolean_create(s->hdr_auto));
    SET("blackborder", jboolean_create(s->blackborder));
    SET("blackborder_threshold", jnumber_create_i32(s->blackborder_threshold));
    SET("smoothing_mode", jnumber_create_i32(s->smoothing_mode));
    SET("smoothing_ms", jnumber_create_i32(s->smoothing_ms));
    SET("adaptive_strength", jnumber_create_i32(s->adaptive_strength));
    SET("release_ms", jnumber_create_i32(s->release_ms));
    SET("spatial", jnumber_create_i32(s->spatial));
    SET("color", jnumber_create_i32(s->color));
    SET("effect", jstring_create(s->effect));
    SET("effect_speed", jnumber_create_i32(s->effect_speed));
    SET("serial_device", jstring_create(s->serial_device));
    SET("baudrate", jnumber_create_i32(s->baudrate));
    SET("awa_mode", jboolean_create(s->awa_mode));
    SET("color_order", jstring_create(s->color_order));
    SET("width", jnumber_create_i32(s->width));
    SET("height", jnumber_create_i32(s->height));
    SET("fps", jnumber_create_i32(s->fps));
    SET("vsync", jboolean_create(s->vsync));
    SET("backend", jstring_create(s->video_backend));
    SET("uibackend", jstring_create(s->ui_backend));
    SET("no_gui", jboolean_create(s->no_gui));
    SET("quirks", jnumber_create_i32(s->quirks));
    SET("hotkey", jnumber_create_i32(s->hotkey));
    SET("api_enabled", jboolean_create(s->api_enabled));
    SET("api_port", jnumber_create_i32(s->api_port));
    SET("api_require_key", jboolean_create(s->api_require_key));
    SET("github_repo", jstring_create(s->github_repo));
    if (include_secret)
        SET("api_key", jstring_create(s->api_key));
    return 0;
}

int settings_load_file(settings_t* s, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 1024 * 1024) {
        fclose(f);
        return -2;
    }
    char* json = calloc(fsize + 1, 1);
    size_t rd = fread(json, 1, fsize, f);
    fclose(f);
    json[rd] = 0;

    JSchemaInfo schema;
    jschema_info_init(&schema, jschema_all(), NULL, NULL);
    jvalue_ref parsed = jdom_parse(j_cstr_to_buffer(json), DOMOPT_NOOPT, &schema);
    free(json);
    if (jis_null(parsed)) {
        j_release(&parsed);
        return -3;
    }
    int r = settings_load_json(s, parsed);
    j_release(&parsed);
    return r;
}

int settings_save_file(settings_t* s, const char* path)
{
    mkdir(CONFIG_DIR, 0755);
    jvalue_ref obj = jobject_create();
    settings_save_json(s, obj, true);
    const char* str = jvalue_tostring_simple(obj);

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "w");
    if (!f) {
        WARN("Cannot write %s: %s", tmp, strerror(errno));
        j_release(&obj);
        return -1;
    }
    fputs(str, f);
    fclose(f);
    j_release(&obj);
    chmod(tmp, 0600);
    if (rename(tmp, path) != 0) {
        WARN("Cannot rename %s: %s", tmp, strerror(errno));
        return -2;
    }
    return 0;
}
