#pragma once
#include <pthread.h>
#include <stdbool.h>

// Listens (read-only, non-exclusive) on /dev/input/event* for one remote key.
// The key keeps its normal TV function; we only additionally react to it.
typedef void (*hotkey_cb_t)(void* data, int code, bool learned);

typedef struct {
    volatile int hotkey; // 0 = off
    volatile bool learning;
    volatile int last_key;
    volatile bool running;
    pthread_t thread;
    hotkey_cb_t cb;
    void* cb_data;
} hotkey_t;

int hotkey_start(hotkey_t* hk, int code, hotkey_cb_t cb, void* data);
void hotkey_stop(hotkey_t* hk);
