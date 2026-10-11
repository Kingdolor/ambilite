// Adalight / AWA serial output, protocol-compatible with HyperHDR's
// DriverSerialAdalight (awa_mode) and the HyperSerialESP32 firmware.
#include "serial_out.h"
#include "log.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define REOPEN_INTERVAL_US 2000000ULL

// HyperSerial control frames (see HyperHDR EspTools.h)
static const uint8_t ESP_INIT_CMD[] = { 0x41, 0x77, 0x41, 0x2a, 0xa2, 0x15, 0x68, 0x79, 0x70, 0x65, 0x72, 0x68, 0x64, 0x72 };
static const uint8_t ESP_SLEEP_CMD[] = { 0x41, 0x77, 0x41, 0x2a, 0xa2, 0x35, 0x68, 0x79, 0x70, 0x65, 0x72, 0x68, 0x64, 0x72 };

void serial_out_init(serial_out_t* s, const char* device, int baudrate, bool awa)
{
    memset(s, 0, sizeof(*s));
    s->fd = -1;
    snprintf(s->device, sizeof(s->device), "%s", device && device[0] ? device : "auto");
    s->baudrate = baudrate > 0 ? baudrate : 2000000;
    s->awa = awa;
    s->esp_init = awa;
}

void serial_out_free(serial_out_t* s)
{
    if (s->fd >= 0)
        close(s->fd);
    s->fd = -1;
    free(s->buf);
    s->buf = NULL;
    s->buf_cap = 0;
}

static speed_t baud_const(int baud)
{
    switch (baud) {
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 500000: return B500000;
    case 576000: return B576000;
    case 921600: return B921600;
    case 1000000: return B1000000;
    case 1152000: return B1152000;
    case 1500000: return B1500000;
    case 2000000: return B2000000;
    case 2500000: return B2500000;
    case 3000000: return B3000000;
    case 4000000: return B4000000;
    default: return 0;
    }
}

static int find_auto_device(char* out, size_t len)
{
    const char* patterns[] = { "/dev/ttyUSB*", "/dev/ttyACM*", NULL };
    for (int i = 0; patterns[i]; i++) {
        glob_t g;
        if (glob(patterns[i], 0, NULL, &g) == 0 && g.gl_pathc > 0) {
            snprintf(out, len, "%s", g.gl_pathv[0]);
            globfree(&g);
            return 0;
        }
        globfree(&g);
    }
    return -1;
}

static int write_all(int fd, const uint8_t* data, size_t len, int timeout_ms)
{
    size_t done = 0;
    while (done < len) {
        ssize_t w = write(fd, data + done, len - done);
        if (w > 0) {
            done += (size_t)w;
            continue;
        }
        if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            return -1;
        struct pollfd p = { .fd = fd, .events = POLLOUT };
        int r = poll(&p, 1, timeout_ms);
        if (r <= 0)
            return -2; // timeout or error
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
            return -3;
    }
    return 0;
}

static void set_modem_bit(int fd, int bit, bool on)
{
    ioctl(fd, on ? TIOCMBIS : TIOCMBIC, &bit);
}

// Waits up to timeout for the "Awa driver" banner of HyperSerial firmware (informational only)
static void read_banner(int fd, int timeout_ms)
{
    char buf[512];
    size_t n = 0;
    uint64_t start = getticks_us();
    while ((getticks_us() - start) / 1000 < (uint64_t)timeout_ms && n < sizeof(buf) - 1) {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, 50) > 0) {
            ssize_t r = read(fd, buf + n, sizeof(buf) - 1 - n);
            if (r > 0)
                n += (size_t)r;
        }
        buf[n] = 0;
        for (size_t i = 0; i < n; i++)
            if (buf[i] == 0)
                buf[i] = '*';
        if (strstr(buf, "Awa driver") || strstr(buf, "AWA driver")) {
            INFO("HyperSerial firmware detected");
            return;
        }
    }
    DBG("No HyperSerial banner received (ok for most setups)");
}

int serial_out_open(serial_out_t* s)
{
    if (s->fd >= 0)
        return 0;
    s->last_open_attempt_us = getticks_us();

    if (strcmp(s->device, "auto") == 0) {
        if (find_auto_device(s->path, sizeof(s->path)) != 0) {
            WARN("No USB serial device found (/dev/ttyUSB*, /dev/ttyACM*)");
            return -1;
        }
    } else {
        snprintf(s->path, sizeof(s->path), "%s", s->device);
    }

    int fd = open(s->path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        WARN("Opening %s failed: %s", s->path, strerror(errno));
        return -2;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        WARN("tcgetattr failed: %s", strerror(errno));
        close(fd);
        return -3;
    }
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CRTSCTS | CSTOPB | PARENB);
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    speed_t sp = baud_const(s->baudrate);
    if (sp == 0) {
        WARN("Unsupported baudrate %d, falling back to 2000000", s->baudrate);
        sp = B2000000;
    }
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        WARN("tcsetattr failed: %s", strerror(errno));
        close(fd);
        return -4;
    }
    tcflush(fd, TCIOFLUSH);

    if (s->esp_init) {
        // Same sequence HyperHDR uses with "forceSerialDetection": no hard reset,
        // just wake the HyperSerial firmware.
        write_all(fd, ESP_INIT_CMD, sizeof(ESP_INIT_CMD), 200);
        set_modem_bit(fd, TIOCM_DTR, true);
        set_modem_bit(fd, TIOCM_RTS, true);
        set_modem_bit(fd, TIOCM_RTS, false);
        read_banner(fd, 600);
        tcflush(fd, TCIFLUSH);
    }

    s->fd = fd;
    INFO("Serial output open: %s @ %d baud (%s)", s->path, s->baudrate, s->awa ? "AWA" : "Adalight");
    return 0;
}

size_t serial_out_build_frame(uint8_t* buf, const uint8_t* colors, int led_count, bool awa)
{
    uint16_t hdr_count = (uint16_t)(led_count - 1);
    buf[0] = 'A';
    buf[1] = awa ? 'w' : 'd';
    buf[2] = 'a';
    buf[3] = (uint8_t)(hdr_count >> 8);
    buf[4] = (uint8_t)(hdr_count & 0xff);
    buf[5] = buf[3] ^ buf[4] ^ 0x55;

    uint8_t* w = buf + 6;
    memcpy(w, colors, (size_t)led_count * 3);
    uint8_t* end = w + (size_t)led_count * 3;

    if (awa) {
        uint16_t f1 = 0, f2 = 0, fext = 0;
        uint8_t pos = 0;
        for (uint8_t* h = w; h < end; h++) {
            fext = (fext + (*h ^ (pos++))) % 255;
            f1 = (f1 + *h) % 255;
            f2 = (f2 + f1) % 255;
        }
        *end++ = (uint8_t)f1;
        *end++ = (uint8_t)f2;
        *end++ = (uint8_t)(fext != 0x41 ? fext : 0xaa);
    }
    return (size_t)(end - buf);
}

int serial_out_send(serial_out_t* s, const uint8_t* colors, int led_count)
{
    if (s->fd < 0) {
        if (getticks_us() - s->last_open_attempt_us < REOPEN_INTERVAL_US)
            return -1;
        if (serial_out_open(s) != 0)
            return -1;
    }

    size_t need = 6 + (size_t)led_count * 3 + 8;
    if (s->buf_cap < need) {
        uint8_t* nb = realloc(s->buf, need);
        if (!nb)
            return -1;
        s->buf = nb;
        s->buf_cap = need;
    }
    s->led_count = led_count;
    size_t len = serial_out_build_frame(s->buf, colors, led_count, s->awa);

    int r = write_all(s->fd, s->buf, len, 100);
    if (r != 0) {
        s->frames_err++;
        WARN("Serial write failed (%d): %s – reopening", r, strerror(errno));
        close(s->fd);
        s->fd = -1;
        return -1;
    }
    s->frames_ok++;
    return 0;
}

void serial_out_close(serial_out_t* s, int led_count)
{
    if (s->fd < 0)
        return;
    if (led_count > 0) {
        uint8_t* black = calloc((size_t)led_count, 3);
        for (int i = 0; i < 3; i++) {
            serial_out_send(s, black, led_count);
            usleep(20000);
        }
        free(black);
    }
    if (s->fd >= 0) {
        if (s->esp_init)
            write_all(s->fd, ESP_SLEEP_CMD, sizeof(ESP_SLEEP_CMD), 200);
        tcdrain(s->fd);
        close(s->fd);
        s->fd = -1;
    }
    INFO("Serial output closed");
}
