#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Adalight / AWA (HyperSerialESP32) output over USB serial
typedef struct {
    int fd;
    char device[128]; // configured: "auto" or path
    char path[128]; // currently opened path
    int baudrate;
    bool awa;
    bool esp_init;

    uint8_t* buf;
    size_t buf_cap;
    int led_count;

    uint64_t last_open_attempt_us;
    unsigned long frames_ok;
    unsigned long frames_err;
} serial_out_t;

void serial_out_init(serial_out_t* s, const char* device, int baudrate, bool awa);
int serial_out_open(serial_out_t* s);
// colors: led_count*3 bytes, already in device colour order. Reopens on failure (rate-limited).
int serial_out_send(serial_out_t* s, const uint8_t* colors, int led_count);
// Writes black, sends the HyperSerial "going sleep" command and closes the port.
void serial_out_close(serial_out_t* s, int led_count);
void serial_out_free(serial_out_t* s);

// Builds one Adalight/AWA frame into buf; returns length. (exposed for tests)
size_t serial_out_build_frame(uint8_t* buf, const uint8_t* colors, int led_count, bool awa);
