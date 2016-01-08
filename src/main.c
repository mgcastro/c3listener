#include <getopt.h>
#include <pwd.h>
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

#include <sys/types.h>
#include <sys/wait.h>

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
  int ret, c, logging = 0;
  while (1) {
      static struct option long_options[] =
        {
          /* These options set a flag. */
          {"verbose", no_argument,       &verbose_flag, 1},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"config",  required_argument, 0, 'c'},
	  {"log",  required_argument, 0, 'l'},
	  {"user", required_argument, 0, 'u'},
	  {0, 0, 0, 0}
        };
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "l:dvc:u:",
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
	case 'u':
	  m_config.user = malloc(strlen(optarg)+1);
	  memset(m_config.user, 0, strlen(optarg)+1);
	  memcpy(m_config.user, optarg, strlen(optarg)+1);
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

  log_init();
  /* Parse config */
#ifdef GIT_REVISION
  log_notice("Starting ble-udp-bridge (%s)\n", GIT_REVISION);
#else
  log_notice("Starting ble-udp-bridge v%s\n", PACKAGE_VERSION);
#endif
  config_init(&cfg);
  if (!m_config.config_file) {
    char *default_config = SYSCONFDIR"/c3listener.conf";
    m_config.config_file = malloc(strlen(default_config)+1);
    memset(m_config.config_file, 0, strlen(default_config)+1);
    memcpy(m_config.config_file, default_config, strlen(default_config));
  }
  log_notice("Using config file: %s\n", m_config.config_file);
  if (!config_read_file(&cfg, m_config.config_file)) {
    log_error(_("Problem with config file: %s: %s:%d - %s\n"),
            m_config.config_file, config_error_file(&cfg), config_error_line(&cfg),
            config_error_text(&cfg));
    ret = ERR_BAD_CONFIG;
    goto cleanup;
  }

  if (config_lookup_string(&cfg, "server", (const char**)&m_config.server)) {
    log_notice(_("Using host: %s\n"), m_config.server);
    m_config.configured = true;
  }
  else {
    log_warn("No 'server' setting in configuration file, using 127.0.0.1.\n");
    m_config.server = "127.0.0.1";
  }
  if (config_lookup_string(&cfg, "port", (const char **)&m_config.port)) {
    log_notice(_("Using port: %s\n"), m_config.port);
    m_config.configured = true;
  }
  else {
    log_warn("No 'port' setting in configuration file, using 9999.\n");
    m_config.port = "9999";
  }
  /* Parse path_loss */
  const char *path_loss_buf;
  if (config_lookup_string(&cfg, "path_loss", &path_loss_buf)) {
    m_config.path_loss = strtof(path_loss_buf, NULL);
  } else {
    m_config.path_loss = 0;
  }
  if (m_config.path_loss == 0) {
    m_config.path_loss = DEFAULT_PATH_LOSS_EXP;
    log_warn(_("RSSI Path Loss invalid or not provided\n"));
  }
  log_notice(_("Using Path Loss constant: %f\n"), m_config.path_loss);
  /* Parse HAAB */
  const char *haab_buf;
  if (config_lookup_string(&cfg, "haab", &haab_buf)) {
    m_config.haab = strtof(haab_buf, NULL);
  } else {
    m_config.haab = 0;
  }
  if (m_config.haab == 0) {
    m_config.haab = DEFAULT_HAAB;
    log_warn(_("HAAB invalid or not provided\n"));
  }
  log_notice(_("Using HAAB constant: %fm\n"), m_config.haab);
  /* Parse Antenna correction */
  const char *antenna_buf;
  if (config_lookup_string(&cfg, "antenna_correction", &antenna_buf)) {
  m_config.antenna_cor = strtol(antenna_buf, NULL, 10);
  } else {
    m_config.antenna_cor = 0;
  }
  if (m_config.antenna_cor == 0) {
    m_config.antenna_cor = DEFAULT_ANTENNA_COR;
    log_warn(_("Antenna correction invalid or not provided\n"));
  }
  log_notice(_("Correcting Antenna gain by %ddBm\n"), m_config.antenna_cor);

  /* Parse report interval */
  const char *interval_buf;
  if (config_lookup_string(&cfg, "report_interval", &interval_buf)) {
  m_config.report_interval = strtol(interval_buf, NULL, 10);
  } else {
    m_config.report_interval = 0;
  }
  if (m_config.report_interval == 0) {
    m_config.report_interval = REPORT_INTERVAL_MSEC;
    log_warn(_("Report interval invalid or not provided\n"));
  }
  log_notice(_("Setting report inteval to %dms\n"), m_config.report_interval);

  gethostname(m_config.hostname, HOSTNAME_MAX_LEN);
  report_init();

  int dd = ble_init();
  uint8_t filter_type = 0, filter_dup = 0;

  /* Who do we run as? */
  char *user = NULL;
  if (!m_config.user) {
    config_lookup_string(&cfg, "user", (const char **) &user);
  } else {
    user = m_config.user;
  }
  int child_pid = fork();
  if (child_pid < 0) {
    log_error(_("Failed to spawn child"), strerror(errno));
    goto cleanup;
  }
  if (child_pid > 0) {
    /* In the parent */
    wait(NULL);
  } else {
    /* In the child */
    if (!user) {
      log_warn(_("No 'user' specified in config file; keeping root privleges\n"));
    } else {
      struct passwd *pw = getpwnam(user);
      if (pw == NULL) {
	log_error(_("Requested user '%s' not found"), user);
	goto cleanup;
      }
      if (setgid(pw->pw_gid) == -1) {
	log_error(_("Failed to drop group privileges: %s"), strerror(errno));
	goto cleanup;
      }
      if (setuid(pw->pw_uid) == -1) {
	log_error(_("Failed to drop user privileges: %s"), strerror(errno));
	goto cleanup;
      }
      log_notice(_("Dropped privileges to %s (%d:%d)\n)"), user, pw->pw_uid, pw->pw_gid);
    }
      /* Loop through scan results */
      ble_scan_loop(dd, filter_type);
  }
  
cleanup:
  config_destroy(&cfg);
  /* free(m_config.config_file); */
  /* free(m_config.user); */
  hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
  hci_close_dev(dd);
  kill(child_pid, SIGTERM);
  wait(NULL);
  return ret;
}
  

