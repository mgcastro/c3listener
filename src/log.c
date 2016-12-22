#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "config.h"

extern int debug_flag;

void log_init(void) {
    if (config_debug()) {
        openlog("c3listener", LOG_PERROR | LOG_CONS, LOG_DAEMON);
    } else {
        openlog("c3listener", 0, LOG_DAEMON);
    }
}

static void log_main(int pri, const char *format, va_list argptr) {
    vsyslog(pri, format, argptr);
}

void log_debug(const char *format, ...) {
    va_list argptr;
    va_start(argptr, format);
    log_main(LOG_MAKEPRI(LOG_DAEMON, LOG_DEBUG), format, argptr);
    va_end(argptr);
}

void log_warn(const char *format, ...) {
    va_list argptr;
    va_start(argptr, format);
    log_main(LOG_MAKEPRI(LOG_DAEMON, LOG_WARNING), format, argptr);
    va_end(argptr);
}

void log_error(const char *format, ...) {
    va_list argptr;
    va_start(argptr, format);
    log_main(LOG_MAKEPRI(LOG_DAEMON, LOG_ERR), format, argptr);
    va_end(argptr);
}

void log_notice(const char *format, ...) {
    va_list argptr;
    va_start(argptr, format);
    log_main(LOG_MAKEPRI(LOG_DAEMON, LOG_NOTICE), format, argptr);
    va_end(argptr);
}
