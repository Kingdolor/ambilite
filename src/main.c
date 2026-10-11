#include "log.h"
#include "service.h"
#include "settings.h"
#include "version.h"
#include <errno.h>
#include <getopt.h>
#include <glib.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static GMainLoop* loop = NULL;
static settings_t settings;
static service_t service;

static void quit_handler(int signum __attribute__((unused)))
{
    if (loop)
        g_main_loop_quit(loop);
}

static void print_usage(void)
{
    printf("Usage: ambilite [OPTION]...\n\n");
    printf("Grabs the TV picture and drives Adalight/AWA LEDs over USB serial.\n\n");
    printf("  -c, --config=PATH   Load settings from PATH (default " CONFIG_PATH ")\n");
    printf("  -t, --test          Show a test pattern on the LEDs and exit\n");
    printf("  -v, --verbose       Verbose logging\n");
    printf("  -h, --help          This help\n");
    printf("      --version       Print version\n");
}

int main(int argc, char* argv[])
{
    static int print_version = 0;
    bool test = false;
    const char* config_override = NULL;
    static const struct option opts[] = {
        { "config", required_argument, 0, 'c' },
        { "test", no_argument, 0, 't' },
        { "verbose", no_argument, 0, 'v' },
        { "help", no_argument, 0, 'h' },
        { "version", no_argument, &print_version, 1 },
        { 0, 0, 0, 0 },
    };

    log_init();
    int opt, idx;
    while ((opt = getopt_long(argc, argv, "c:tvh", opts, &idx)) != -1) {
        switch (opt) {
        case 'c': config_override = optarg; break;
        case 't': test = true; break;
        case 'v': log_set_level(Debug); break;
        case 'h': print_usage(); return 0;
        case 0: break;
        default: print_usage(); return 1;
        }
    }
    if (print_version) {
        printf("%s\n", AMBILITE_VERSION);
        return 0;
    }

    INFO("Ambilite %s starting (uid %d)", AMBILITE_VERSION, (int)getuid());

    // directory of our executable = service dir (contains defaults + backends)
    char exe[PATH_MAX] = { 0 };
    char service_dir[PATH_MAX] = ".";
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        snprintf(service_dir, sizeof(service_dir), "%s", dirname(exe));
    }

    settings_init(&settings);
    char default_cfg[PATH_MAX + 32];
    snprintf(default_cfg, sizeof(default_cfg), "%s/config.default.json", service_dir);
    settings_load_file(&settings, default_cfg);
    const char* cfg = config_override ? config_override : CONFIG_PATH;
    if (settings_load_file(&settings, cfg) != 0)
        INFO("No user config at %s yet – using defaults", cfg);

    service_init(&service, &settings, service_dir);

    if (test) {
        int r = service_run_test_pattern(&service, true);
        service_destroy(&service);
        settings_free(&settings);
        return r == 0 ? 0 : 1;
    }

    signal(SIGINT, quit_handler);
    signal(SIGTERM, quit_handler);

    loop = g_main_loop_new(NULL, false);
    if (service_register(&service, loop) != 0)
        WARN("Running without Luna bus registration");

    if (getuid() == 0)
        service_update_autostart(&service);

    if (settings.enabled) {
        int r = service_start(&service);
        if (r < 0)
            WARN("Start failed (%d) – waiting for commands", r);
    }

    g_main_loop_run(loop);

    INFO("Shutting down");
    service_destroy(&service);
    g_main_loop_unref(loop);
    settings_free(&settings);
    return 0;
}
