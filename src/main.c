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

#include <json-c/json.h>

#include <curl/curl.h>

#include <libconfig.h>

/* Global config */
c3_config_t g_config = {.configured = false }; 

#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
#include "avahi.c"
#endif /* HAVE_LIBAVAHI_COMMON && HAVE_LIBAVAHI_CLIENT */

CURL *curl;
struct curl_slist *headers = NULL;

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
  int err, dd = 0;
  uint8_t own_type = 0x00;
  uint8_t scan_type = 0x01;
  uint8_t filter_type = 0;
  uint8_t filter_policy = 0x00;
  uint16_t interval = htobs(0x0010);
  uint16_t window = htobs(0x0010);
  uint8_t filter_dup = 1;
  interval = htobs(0x0012);
  window = htobs(0x0012);

  if (dev_id < 0)
    dev_id = hci_get_route(NULL);

  dd = hci_open_dev(dev_id);
  if (dd < 0) {
    perror(_("Could not open bluetooth device"));
    ret = ERR_NO_BLUETOOTH_DEV;
    goto cleanup;
  }

  err = hci_le_set_scan_parameters(dd, scan_type, interval, window, own_type,
                                   filter_policy, 1000);
  if (err < 0) {
    perror(_("Set scan parameters failed"));
    ret = ERR_BLE_NOT_SUPPORTED;
    goto cleanup;
  }

  err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 1000);
  if (err < 0) {
    perror(_("Enable scan failed"));
    ret = ERR_SCAN_ENABLE_FAIL;
    goto cleanup;
  }

/* Parse config */  

  config_t cfg;
  int ret = ERR_UNKNOWN;

  config_init(&cfg);
  if (!config_read_file(&cfg, CONFIG_FILE)) {
    fprintf(stderr, _("Problem with config file: %s: %s:%d - %s\n"),
            CONFIG_FILE, config_error_file(&cfg), config_error_line(&cfg),
            config_error_text(&cfg));
    ret = ERR_BAD_CONFIG;
    goto cleanup;
  }

#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
  configure_via_avahi(&cfg);
#endif /* defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT) */

  if (!gconfig.configured) {
    if (config_lookup_string(&cfg, "post_url", &config_post_url)) {
      post_url = (char *)config_post_url;
      printf(_("Using static url: %s\n"), post_url);
    }
    else {
      fprintf(stderr, _("No 'post_url' setting in configuration file.\n"));
      ret = ERR_BAD_CONFIG;
      goto cleanup;
    }
  }

  /* Loop through scan results */
  err = scan_loop(dd, filter_type);
  if (err < 0) {
    perror(_("Could not receive advertising events"));
    ret = ERR_SCAN_FAIL;
    goto cleanup;
  }

cleanup:
  config_destroy(&cfg);
#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
  cleanup_avahi();
#endif /* defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT) */
  m_cleanup_curl();
  return ret;
}

void m_init_curl(void) {
  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (!curl) {
    perror(_("Couldn't initialize libcurl handle"));
    exit(1);
  }
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, post_url);
  curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  gethostname(hostname, HOSTNAME_MAX_LEN);
  }

void m_cleanup_curl(void) {
  curl_slist_free_all(headers);
  curl_global_cleanup();
}

void m_cleanup_bluez(void) {
  err = hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
  if (err < 0) {
    perror(_("Disable scan failed"));
    exit(1);
  }
  hci_close_dev(dd);
}
