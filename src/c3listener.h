#ifndef C3LISTENER_H
#define C3LISTENER_H

#include "kalman.h"

#define HOSTNAME_MAX_LEN 255
#define MAX_NET_PACKET 64

#define REPORT_INTERVAL_MS 2000

int ble_scan_loop(int, uint8_t);
int m_cleanup(int);
void log_stdout(const char *, ...);
int udp_send(char *, uint8_t);
void init_udp(char *, int);

typedef struct configuration {
  char *ip;
  uint16_t port;
  bool configured;
  char hostname[HOSTNAME_MAX_LEN], *config_file;
} c3_config_t;

typedef struct advdata {
  char mac[6];
  char data[3];
  int data_len;
  int rssi;
} adv_data_t;


enum c3error {
  ERR_SUCCESS = 0,
  ERR_SCAN_ENABLE_FAIL,
  ERR_SCAN_DISABLE_FAIL,
  ERR_SCAN_FAIL,
  ERR_NO_BLUETOOTH_DEV,
  ERR_BLE_NOT_SUPPORTED,
  ERR_BLUEZ_SOCKET,
  ERR_BAD_CONFIG,
  ERR_UNKNOWN
};

#endif /* C3LISTENER_H */
