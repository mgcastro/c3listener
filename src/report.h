#ifndef __REPORT_H
#define __REPORT_H

enum report_versions {
  REPORT_VERSION_0 = 0
};

enum report_packet_types {
  REPORT_PACKET_TYPE_KEEPALIVE = 0,
  REPORT_PACKET_TYPE_DATA = 1
};

void *report_beacon(void *a, void *b);
void report_clear(void);
void report_send(void);
int report_header_length(void);
int report_length(void);
void report_header(int, int);
void report_init(void);

#endif
