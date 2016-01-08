#ifndef C3LISTENER_H
#define C3LISTENER_H

#include <stdint.h>
#include <stdbool.h>

#include "kalman.h"

#define HOSTNAME_MAX_LEN 255
#define MAX_NET_PACKET 64

#define REPORT_INTERVAL_MSEC 500 /* How often to send reports */

#define MAX_BEACON_INACTIVE_SEC 10 /* Free memory for any beacons
				      quietfor this long */
#define MAX_ACK_INTERVAL_SEC 40 /* Reopen UDP socket if we haven't
				  heard from the server in for this
				  long */

#define DEFAULT_PATH_LOSS_EXP 4
#define DEFAULT_HAAB 0
#define DEFAULT_ANTENNA_COR 0


#define GC_INTERVAL_SEC (MAX_BEACON_INACTIVE_SEC / 2) /* How often to check for inactive beacons */
#define KEEP_ALIVE_SEC 30

#define MAX_HASH_CB 5

void ble_scan_loop(int, uint8_t);
int ble_init(void);
int m_cleanup(int);
void log_stdout(const char *, ...);
int udp_send(uint8_t *, uint8_t);
int udp_init(char *, char *);
void udp_cleanup(void);
char *hexlify(const uint8_t* src, size_t n);

typedef struct configuration {
  char *server;
  char *port;
  bool configured;
  char hostname[HOSTNAME_MAX_LEN], *config_file;
  double path_loss, haab;
  int8_t antenna_cor;
  int16_t report_interval;
  char *user;
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
