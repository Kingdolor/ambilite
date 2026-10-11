#include "log.h"
void log_init(void) {}
void log_set_level(LogLevel l) { (void)l; }
void log_printf(LogLevel level, const char* module, const char* fmt, ...) {
    if (level > Info) return;
    va_list a; va_start(a, fmt); fprintf(stderr, "[%s] ", module); vfprintf(stderr, fmt, a); fputc('\n', stderr); va_end(a);
}
