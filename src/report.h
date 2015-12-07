#ifndef __REPORT_H
#define __REPORT_H

void *report_beacon(void *a, void *b);
void report_clear(void);
void report_send(void);
int report_header_length(void);
int report_length(void);

#endif
