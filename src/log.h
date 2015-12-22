#ifndef __LOG_H
#define __LOG_H

void log_init(void);
void log_debug(const char *format, ...);
void log_warn(const char *format, ...);
void log_error(const char *format, ...);
void log_notice(const char *format, ...);

#endif
