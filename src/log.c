#include <stdarg.h>
#include <stdio.h>

extern int verbose_flag;

void log_stdout(const char *format, ...) {
  if (verbose_flag) {
    va_list argptr;
    va_start(argptr, format);
    vprintf(format, argptr);
    fflush(stdout);
    va_end(argptr);
  }
}
