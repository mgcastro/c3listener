#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

extern int verbose_flag;

void log_init(void) {
  if (verbose_flag) {
    openlog(NULL, LOG_PERROR | LOG_CONS, LOG_USER);
  } else {
    openlog(NULL, 0, LOG_USER);
  }
}

static void log_main(int pri, const char *format, void *argptr) {
  vsyslog(pri, format, argptr);
}

void log_debug(const char *format, ...) {
  va_list argptr;
  va_start(argptr, format);
  log_main(LOG_MAKEPRI(LOG_USER, LOG_DEBUG), format, argptr);
  va_end(argptr);
}

void log_warn(const char *format, ...) {
  va_list argptr;
  va_start(argptr, format);
  log_main(LOG_MAKEPRI(LOG_USER, LOG_WARNING), format, argptr);
  va_end(argptr);
}

void log_error(const char *format, ...) {
  va_list argptr;
  va_start(argptr, format);
  log_main(LOG_MAKEPRI(LOG_USER, LOG_ERR), format, argptr);
  va_end(argptr);
}
 
void log_notice(const char *format, ...) {
  va_list argptr;
  va_start(argptr, format);
  log_main(LOG_MAKEPRI(LOG_USER, LOG_NOTICE), format, argptr);
  va_end(argptr);
}

