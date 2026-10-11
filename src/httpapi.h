#pragma once
#include <pthread.h>
#include <stdbool.h>

// Minimal single-threaded HTTP/1.1 JSON server for the iPhone app.
// handler returns the HTTP status code and sets *resp to a malloc'd JSON string.
typedef int (*httpapi_handler_t)(void* ctx, const char* method, const char* path, const char* key,
    const char* body, char** resp);

typedef struct {
    int port;
    int listen_fd;
    volatile bool running;
    pthread_t thread;
    httpapi_handler_t handler;
    void* ctx;
} httpapi_t;

int httpapi_start(httpapi_t* api, int port, httpapi_handler_t handler, void* ctx);
void httpapi_stop(httpapi_t* api);
