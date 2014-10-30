#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <config.h>
#include <c3listener.h>

#ifdef HAVE_GETTEXT
#include "gettext.h"
#define _(string) gettext(string)
#else
#define _(string) string
#endif /* HAVE_GETTEXT */
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif /* HAVE_LOCALE_H */

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
int dd = 0;
uint8_t filter_dup = 1;

#include <json-c/json.h>

#include <curl/curl.h>
CURL *curl;
struct curl_slist *headers = NULL;

#include <libconfig.h>
config_t cfg;

/* Global config */
c3_config_t m_config = {.configured = false }; 

#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
#include "avahi.c"
#endif /* HAVE_LIBAVAHI_COMMON && HAVE_LIBAVAHI_CLIENT */

int main() {

  /* Initialize i18n */ 
#ifdef HAVE_SETLOCALE
  setlocale(LC_ALL, "");
#endif /* HAVE_SETLOCALE */
#ifdef HAVE_GETTEXT
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
#endif /* HAVE_GETTEXT */

  /* Initialize Bluez */
  int dev_id = 0;
  int err;
  uint8_t own_type = 0x00;
  uint8_t scan_type = 0x01;
  uint8_t filter_type = 0;
  uint8_t filter_policy = 0x00;
  uint16_t interval = htobs(0x0010);
  uint16_t window = htobs(0x0010);
  interval = htobs(0x0012);
  window = htobs(0x0012);

  if (dev_id < 0)
    dev_id = hci_get_route(NULL);

  dd = hci_open_dev(dev_id);
  if (dd < 0) {
    perror(_("Could not open bluetooth device"));
    m_cleanup(ERR_NO_BLUETOOTH_DEV);
  }

  err = hci_le_set_scan_parameters(dd, scan_type, interval, window, own_type,
                                   filter_policy, 1000);
  if (err < 0) {
    perror(_("Set scan parameters failed"));
    m_cleanup(ERR_BLE_NOT_SUPPORTED);
  }

  err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 1000);
  if (err < 0) {
    perror(_("Enable scan failed"));
    m_cleanup(ERR_SCAN_ENABLE_FAIL);
  }

/* Parse config */  

  config_init(&cfg);
  if (!config_read_file(&cfg, SYSCONFDIR"/c3listener.conf")) {
    fprintf(stderr, _("Problem with config file: %s: %s:%d - %s\n"),
            SYSCONFDIR"/c3listener.conf", config_error_file(&cfg), config_error_line(&cfg),
            config_error_text(&cfg));
    m_cleanup(ERR_BAD_CONFIG);
  }

#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
  configure_via_avahi(&cfg);
#endif /* defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT) */

  if (!m_config.configured) {
    if (config_lookup_string(&cfg, "post_url", (const char**)&m_config.post_url)) {
      printf(_("Using static url: %s\n"), m_config.post_url);
    }
    else {
      fprintf(stderr, _("No 'post_url' setting in configuration file.\n"));
      m_cleanup(ERR_BAD_CONFIG);
    }
  }

  /* Loop through scan results */
  err = ble_scan_loop(dd, filter_type);
  if (err < 0) {
    perror(_("Could not receive advertising events"));
    m_cleanup(ERR_SCAN_FAIL);
  }

cleanup:
  m_cleanup(0);
}

void m_curl_init(void) {
  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (!curl) {
    perror(_("Couldn't initialize libcurl handle"));
    exit(ERR_CURL_INIT);
  }
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, m_config.post_url);
  curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  gethostname(m_config.hostname, HOSTNAME_MAX_LEN);
}

int m_cleanup_curl(void) {
  curl_slist_free_all(headers);
  curl_global_cleanup();
  return 0;
}

int m_cleanup_bluez(void) {
  int err = hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
  if (err < 0) {
    perror(_("Disable scan failed"));
    return ERR_SCAN_DISABLE_FAIL;
  }
  hci_close_dev(dd);
  return ERR_SUCCESS;
}

void m_cleanup(int ret) {
  config_destroy(&cfg);
#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
  ret += cleanup_avahi();
#endif /* defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT) */
  ret += m_cleanup_curl();
  ret += m_cleanup_bluez();
  exit(ret);
}
