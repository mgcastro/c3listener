#ifndef C3LISTENER_H
#define C3LISTENER_H

#define HOSTNAME_MAX_LEN 255

int ble_scan_loop(int, uint8_t);
int m_cleanup(int);
int m_curl_init(void);
void log_stdout(const char *, ...);

typedef struct configuration {
  char *post_url;
  const char *post_url_template, *static_post_url;
  int max_events, max_wait;
  bool configured;
  char hostname[HOSTNAME_MAX_LEN], *config_file;
#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
  int use_avahi;
  const char *avahi_server;
#endif
} c3_config_t;

typedef struct advdata {
  char *mac,
    *data;
  int rssi;
  time_t timestamp;
} adv_data_t;

enum c3error {
  ERR_SUCCESS = 0,
  ERR_SCAN_ENABLE_FAIL,
  ERR_SCAN_DISABLE_FAIL,
  ERR_SCAN_FAIL,
  ERR_AVAHI_FAIL,
  ERR_NO_BLUETOOTH_DEV,
  ERR_BLE_NOT_SUPPORTED,
  ERR_BLUEZ_SOCKET,
  ERR_BAD_CONFIG,
  ERR_CURL_INIT,
  ERR_UNKNOWN
};

#endif /* C3LISTENER_H */
