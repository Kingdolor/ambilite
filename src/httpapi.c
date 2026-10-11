#include "httpapi.h"
#include "log.h"
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_REQ 65536
#define IO_TIMEOUT_MS 3000

static int read_some(int fd, char* buf, size_t cap, size_t* len)
{
    struct pollfd p = { .fd = fd, .events = POLLIN };
    if (poll(&p, 1, IO_TIMEOUT_MS) <= 0)
        return -1;
    ssize_t r = recv(fd, buf + *len, cap - *len, 0);
    if (r <= 0)
        return -1;
    *len += (size_t)r;
    return 0;
}

static void send_all(int fd, const char* data, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t w = send(fd, data + done, len - done, MSG_NOSIGNAL);
        if (w <= 0)
            return;
        done += (size_t)w;
    }
}

static const char* status_text(int code)
{
    switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    default: return "Error";
    }
}

static void respond(int fd, int code, const char* json)
{
    char hdr[512];
    size_t blen = json ? strlen(json) : 0;
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Headers: Content-Type, X-Ambilite-Key\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Connection: close\r\n\r\n",
        code, status_text(code), blen);
    send_all(fd, hdr, (size_t)n);
    if (blen)
        send_all(fd, json, blen);
}

static void handle_client(httpapi_t* api, int fd)
{
    char* buf = malloc(MAX_REQ + 1);
    size_t len = 0;
    char* hdr_end = NULL;
    while (!hdr_end) {
        if (len >= MAX_REQ || read_some(fd, buf, MAX_REQ, &len) != 0) {
            free(buf);
            return;
        }
        buf[len] = 0;
        hdr_end = strstr(buf, "\r\n\r\n");
    }
    *hdr_end = 0;
    char* body = hdr_end + 4;

    char method[8] = { 0 }, path[256] = { 0 };
    if (sscanf(buf, "%7s %255s", method, path) != 2) {
        respond(fd, 400, "{\"error\":\"bad request\"}");
        free(buf);
        return;
    }

    size_t content_length = 0;
    char key[64] = { 0 };
    for (char* line = strstr(buf, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        char* l = line + 2;
        if (strncasecmp(l, "Content-Length:", 15) == 0)
            content_length = (size_t)strtoul(l + 15, NULL, 10);
        else if (strncasecmp(l, "X-Ambilite-Key:", 15) == 0) {
            char* v = l + 15;
            while (*v == ' ')
                v++;
            size_t k = 0;
            while (v[k] && v[k] != '\r' && k < sizeof(key) - 1) {
                key[k] = v[k];
                k++;
            }
        }
    }
    if (content_length > MAX_REQ / 2) {
        respond(fd, 413, "{\"error\":\"too large\"}");
        free(buf);
        return;
    }
    size_t have = len - (size_t)(body - buf);
    while (have < content_length) {
        size_t before = len;
        if (read_some(fd, buf, MAX_REQ, &len) != 0)
            break;
        have += len - before;
    }
    body[have < content_length ? have : content_length] = 0;

    char* q = strchr(path, '?');
    if (q)
        *q = 0;

    if (strcmp(method, "OPTIONS") == 0) {
        respond(fd, 204, NULL);
    } else {
        char* resp = NULL;
        int code = api->handler(api->ctx, method, path, key, body, &resp);
        respond(fd, code, resp ? resp : "{}");
        free(resp);
    }
    free(buf);
}

static void* server_loop(void* data)
{
    httpapi_t* api = (httpapi_t*)data;
    INFO("HTTP API listening on port %d", api->port);
    while (api->running) {
        struct pollfd p = { .fd = api->listen_fd, .events = POLLIN };
        if (poll(&p, 1, 500) <= 0)
            continue;
        int fd = accept(api->listen_fd, NULL, NULL);
        if (fd < 0)
            continue;
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        handle_client(api, fd);
        close(fd);
    }
    return NULL;
}

int httpapi_start(httpapi_t* api, int port, httpapi_handler_t handler, void* ctx)
{
    if (api->running)
        return 0;
    api->port = port;
    api->handler = handler;
    api->ctx = ctx;
    api->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (api->listen_fd < 0)
        return -1;
    int one = 1;
    setsockopt(api->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(api->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(api->listen_fd, 8) != 0) {
        WARN("HTTP API bind/listen on %d failed: %s", port, strerror(errno));
        close(api->listen_fd);
        api->listen_fd = -1;
        return -2;
    }
    api->running = true;
    if (pthread_create(&api->thread, NULL, server_loop, api) != 0) {
        api->running = false;
        close(api->listen_fd);
        return -3;
    }
    return 0;
}

void httpapi_stop(httpapi_t* api)
{
    if (!api->running)
        return;
    api->running = false;
    pthread_join(api->thread, NULL);
    close(api->listen_fd);
    api->listen_fd = -1;
}
