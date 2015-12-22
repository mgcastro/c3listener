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

#include <config.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "c3listener.h"
#include "beacon.h"
#include "log.h"
#include "report.h"

#ifdef HAVE_GETTEXT
#include "gettext.h"
#define _(string) gettext(string)
#else
#define _(string) string
#endif /* HAVE_GETTEXT */
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif /* HAVE_LOCALE_H */

#include <libconfig.h>
config_t cfg;

/* Config and other globals */
c3_config_t m_config = {.configured = false };
int verbose_flag;

int main(int argc, char **argv) {
  signal (SIGINT, sigint_handler);
  signal (SIGTERM, sigint_handler);
  signal (SIGHUP, sigint_handler);
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
  int ret, err, c, logging = 0;
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
  
  /* Parse config */
#ifdef GIT_REVISION
  log_stdout("Starting ble-udp-bridge (%s)\n", GIT_REVISION);
#else
  log_stdout("Starting c3listener v%s\n", PACKAGE_VERSION);
#endif
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

  if (!m_config.configured) {
    if (config_lookup_string(&cfg, "server", (const char**)&m_config.server)) {
      if (verbose_flag)
	printf(_("Using host: %s\n"), m_config.server);
      m_config.configured = true;
    }
    else {
      fprintf(stderr, "No 'server' setting in configuration file, using 127.0.0.1.\n");
      m_config.server = "127.0.0.1";
    }
    if (config_lookup_string(&cfg, "port", (const char **)&m_config.port)) {
      if (verbose_flag)
	printf(_("Using port: %s\n"), m_config.port);
      m_config.configured = true;
    }
    else {
      fprintf(stderr, "No 'port' setting in configuration file, using 9999.\n");
      m_config.port = "9999";
    }
    const char *path_loss_buf;
    if (config_lookup_string(&cfg, "path_loss", &path_loss_buf)) {
      m_config.path_loss = strtof(path_loss_buf, NULL);
    } else {
      m_config.path_loss = 0;
    }
    if (m_config.path_loss == 0) {
      m_config.path_loss = DEFAULT_PATH_LOSS_EXP;
      if (verbose_flag) {
	printf(_("RSSI Path Loss invalid or not provided\n"));
      }
    }
    if (verbose_flag) {
      printf(_("Using Path Loss constant: %1.4f\n"), m_config.path_loss);
    }
    
  }
  gethostname(m_config.hostname, HOSTNAME_MAX_LEN);
  report_init();

  int dd = ble_init();
  uint8_t filter_type = 0, filter_dup = 0;
  
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
  hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
  hci_close_dev(dd);
  return ret;
}

