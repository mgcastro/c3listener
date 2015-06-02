#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <setjmp.h>
jmp_buf cleanup;
#include <signal.h>
void
sigint_handler (int signum)
{
  /* We may have been waiting for input when the signal arrived,
     but we are no longer waiting once we transfer control. */
  longjmp (cleanup, 0);
}

#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>

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
static int verbose_flag;

#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
#include "avahi.c"
#endif /* HAVE_LIBAVAHI_COMMON && HAVE_LIBAVAHI_CLIENT */

void log_stdout(const char *format, ...) {
  if (verbose_flag) {
    va_list argptr;
    va_start(argptr, format);
    vprintf(format, argptr);
    fflush(stdout);
    va_end(argptr);
  }
}

int main(int argc, char **argv) {
  signal (SIGINT, sigint_handler);
  if(setjmp(cleanup))
    goto cleanup;
  /* Initialize i18n */ 
#ifdef HAVE_SETLOCALE
  setlocale(LC_ALL, "");
#endif /* HAVE_SETLOCALE */
#ifdef HAVE_GETTEXT
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);
#endif /* HAVE_GETTEXT */

  /* Parse command line options */
  int c, logging = 0;
  while (1) {
      static struct option long_options[] =
        {
          /* These options set a flag. */
          {"verbose", no_argument,       &verbose_flag, 1},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"config",  required_argument, 0, 'c'},
	  {"log",  required_argument, 0, 'l'},
	  {0, 0, 0, 0}
        };
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "l:dvc:",
                       long_options, &option_index);

      /* Detect the end of the options. */
      if (c == -1)
        break;

      switch (c)
        {
        case 'c':
	  m_config.config_file = malloc(strlen(optarg)+1);
	  memcpy(m_config.config_file, optarg, strlen(optarg)+1);
          break;
	case 'l':
	  logging = 1;
	  freopen(optarg, "a", stdout);
	  freopen(optarg, "a", stderr);
          break;
	case 'v':
	  verbose_flag=1;
	  break;
        case '?':
          /* getopt_long already printed an error message. */
          break;
	case 'd':
	  /* Daemonize */
	  if (logging)
	    daemon(0,1);
	  else
	    daemon(0,0);
	  break;
        }
      if (c == -1)
	break;
  }
  
  /* Initialize Bluez */
  int dev_id = 0;
  int err, ret;
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
    ret = ERR_NO_BLUETOOTH_DEV;
    goto cleanup;
  }

  err = hci_le_set_scan_parameters(dd, scan_type, interval, window, own_type,
                                   filter_policy, 1000);
  if (err < 0) {
    perror(_("Set scan parameters failed"));
    ret = ERR_SCAN_ENABLE_FAIL;
    goto cleanup;
  }

  err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 1000);
  if (err < 0) {
    perror(_("Enable scan failed"));
    ret = ERR_SCAN_ENABLE_FAIL;
    goto cleanup;
  }

/* Parse config */  

  config_init(&cfg);
  if (!m_config.config_file) {
    char *default_config = SYSCONFDIR"/c3listener.conf";
    m_config.config_file = malloc(strlen(default_config)+1);
    memcpy(m_config.config_file, default_config, strlen(default_config)+1);
  }
  log_stdout("Using config file: %s\n", m_config.config_file);
  if (!config_read_file(&cfg, m_config.config_file)) {
    fprintf(stderr, _("Problem with config file: %s: %s:%d - %s\n"),
            m_config.config_file, config_error_file(&cfg), config_error_line(&cfg),
            config_error_text(&cfg));
    ret = ERR_BAD_CONFIG;
    goto cleanup;
  }

#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
  configure_via_avahi(&cfg);
#endif /* defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT) */

  if (!m_config.configured) {
    if (config_lookup_string(&cfg, "post_url", (const char**)&m_config.post_url)) {
      if (verbose_flag)
	printf(_("Using static url: %s\n"), m_config.post_url);
      m_config.configured = true;
    }
    else {
      fprintf(stderr, "No 'post_url' setting in configuration file.\n");
      ret = ERR_BAD_CONFIG;
      goto cleanup;
    }
  }
  
  /* Loop through scan results */
  err = ble_scan_loop(dd, filter_type);
  if (err < 0) {
    perror(_("Could not receive advertising events"));
    ret = ERR_SCAN_FAIL;
    goto cleanup;
  }
 cleanup:
  config_destroy(&cfg);
  free(m_config.config_file);
  curl_slist_free_all(headers);
  curl_global_cleanup();
  hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
  hci_close_dev(dd);
#if defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT)
  if (sb)
    avahi_service_browser_free(sb);

  if (client)
    avahi_client_free(client);

  if (simple_poll)
    avahi_simple_poll_free(simple_poll);
#endif /* defined(HAVE_LIBAVAHI_COMMON) && defined(HAVE_LIBAVAHI_CLIENT) */
  return ret;
}

int m_curl_init(void) {
  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (!curl) {
    perror(_("Couldn't initialize libcurl handle"));
    return -1;
  }
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, m_config.post_url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  gethostname(m_config.hostname, HOSTNAME_MAX_LEN);
  return 0;
}
