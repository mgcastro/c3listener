#include <getopt.h>
#include <pwd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#include <string.h>
#include <errno.h>
#include <assert.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "c3listener.h"
#include "beacon.h"
#include "log.h"
#include "report.h"

#include <libconfig.h>
config_t cfg;

/* Config and other globals */
c3_config_t m_config = {.configured = false };
int debug_flag = 0;
int dev_id = 0, dd = 0, child_pid = 0;
const uint8_t filter_type = 0, filter_dup = 0;

#include <signal.h>



void sigint_handler(int signum) {
  log_notice("Parent got signal: %d\n", signum);
  if (child_pid > 0) {
    /* If the child has been started, we need to kill it */
    log_notice("Killing scanning process\n");
    kill(child_pid, SIGTERM);
  }
  config_destroy(&cfg);
  if (hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000) < 0){
    log_error("Disable scan failed", strerror(errno));
  } else {
    log_notice("Scan disabled\n");
  }
  if (hci_close_dev(dd) < 0) {
    log_error("Closing HCI Socket Failed\n");
  } else {
    log_notice("HCI Socket Closed\n");
  }
  fflush(stderr);
  exit(errno);
}

int main(int argc, char **argv) {
  /* Parse command line options */
  int c;
  while (1) {
      static struct option long_options[] =
        {
          /* These options set a flag. */
          {"debug", no_argument,       &debug_flag, 1},
          /* These options don’t set a flag.
             We distinguish them by their indices. */
          {"config",  required_argument, 0, 'c'},
	  {"user", required_argument, 0, 'u'},
	  {"interface", required_argument, 0, 'i'},
	  {0, 0, 0, 0}
        };
      /* getopt_long stores the option index here. */
      int option_index = 0;

      c = getopt_long (argc, argv, "dc:u:i:",
                       long_options, &option_index);

      /* Detect the end of the options. */
      if (c == -1)
        break;

      switch (c)
        {
	 case 'i':
	   dev_id = hci_devid(optarg);
	   if (dev_id < 0) {
	     fprintf(stderr, "Error opening device %s: %s\n", optarg, strerror(errno));
	     fflush(stderr);
	     exit(errno);
	   }
	   break;
        case 'c':
	  m_config.config_file = malloc(strlen(optarg)+1);
	  memcpy(m_config.config_file, optarg, strlen(optarg)+1);
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
	  /* Debug, keep in foreground */
	  debug_flag=1;
	  break;
        }
      if (c == -1)
	break;
  }

  log_init();
  log_notice("Starting ble-udp-bridge %s\n", PACKAGE_VERSION);
  
  /* Parse config */
  config_init(&cfg);
  if (!m_config.config_file) {
    char *default_config = SYSCONFDIR"/c3listener.conf";
    m_config.config_file = malloc(strlen(default_config)+1);
    memset(m_config.config_file, 0, strlen(default_config)+1);
    memcpy(m_config.config_file, default_config, strlen(default_config));
  }
  log_notice("Using config file: %s\n", m_config.config_file);
  if (!config_read_file(&cfg, m_config.config_file)) {
    log_error("Problem with config file: %s: %s:%d - %s\n",
            m_config.config_file, config_error_file(&cfg), config_error_line(&cfg),
            config_error_text(&cfg));
    exit(1);
  }
  
  if (config_lookup_string(&cfg, "interface", (const char**)&m_config.interface)){
    if (dev_id == 0) {
      /* CLI should override config file */
      dev_id = hci_devid(m_config.interface);
      if (dev_id < 0) {
	log_error("Error opening device %s: %s", m_config.interface, strerror(errno));
	exit(errno);
      }
    }
  }

  dd = ble_init(dev_id);

  if (config_lookup_string(&cfg, "server", (const char**)&m_config.server)) {
    log_notice("Using host: %s\n", m_config.server);
    m_config.configured = true;
  }
  else {
    log_warn("No 'server' setting in configuration file, using 127.0.0.1.\n");
    m_config.server = "127.0.0.1";
  }
  if (config_lookup_string(&cfg, "port", (const char **)&m_config.port)) {
    log_notice("Using port: %s\n", m_config.port);
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
    log_warn("RSSI Path Loss invalid or not provided\n");
  }
  log_notice("Using Path Loss constant: %f\n", m_config.path_loss);
  /* Parse HAAB */
  const char *haab_buf;
  if (config_lookup_string(&cfg, "haab", &haab_buf)) {
    m_config.haab = strtof(haab_buf, NULL);
  } else {
    m_config.haab = 0;
  }
  if (m_config.haab == 0) {
    m_config.haab = DEFAULT_HAAB;
    log_warn("HAAB invalid or not provided\n");
  }
  log_notice("Using HAAB constant: %fm\n", m_config.haab);
  /* Parse Antenna correction */
  const char *antenna_buf;
  if (config_lookup_string(&cfg, "antenna_correction", &antenna_buf)) {
  m_config.antenna_cor = strtol(antenna_buf, NULL, 10);
  } else {
    m_config.antenna_cor = 0;
  }
  if (m_config.antenna_cor == 0) {
    m_config.antenna_cor = DEFAULT_ANTENNA_COR;
    log_warn("Antenna correction invalid or not provided\n");
  }
  log_notice("Correcting Antenna gain by %ddBm\n", m_config.antenna_cor);

  /* Parse report interval */
  const char *interval_buf;
  if (config_lookup_string(&cfg, "report_interval", &interval_buf)) {
  m_config.report_interval = strtol(interval_buf, NULL, 10);
  } else {
    m_config.report_interval = 0;
  }
  if (m_config.report_interval == 0) {
    m_config.report_interval = REPORT_INTERVAL_MSEC;
    log_warn("Report interval invalid or not provided\n");
  }
  log_notice("Setting report inteval to %dms\n", m_config.report_interval);

  /* Daemonize */
  if (!debug_flag) {
    if(daemon(0,0)) {
      perror("Daemonizing failed");
      exit(errno);
    }
  }

  gethostname(m_config.hostname, HOSTNAME_MAX_LEN);
  report_init();

  /* Who do we run as? */
  char *user = NULL;
  if (!m_config.user) {
    config_lookup_string(&cfg, "user", (const char **) &user);
  } else {
    user = m_config.user;
  }
  child_pid = fork();
  if (child_pid < 0) {
    log_error("Failed to spawn child", strerror(errno));
    raise(SIGTERM); /* Cleanup BLE and Config */
    exit(errno);
  }
  if (child_pid > 0) {
    /* We are parent */
    /* Set the signal handlers to cleanup BLE priv. socket */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    signal(SIGHUP, sigint_handler);
    int status;
    while (true) {
      /* Loop for child events, if the child exits; then cleanup */
      pid_t pid = waitpid(-1, &status, 0);
      /* If the parent was signaled, the signal handler will never
	 give back control. So we should only reach this code if the
	 child exits or receives a signal */
      if (WIFSIGNALED(status) || WIFEXITED(status)) {
	/* A child has ended */
	if (WIFSIGNALED(status)) {
	  log_notice("Child %d exited by signal: %s", pid, strsignal(WTERMSIG(status)));
	} else {
	  log_notice("Child %d exited status: %s\n", pid, strerror(errno));
	}
	/* Clear child_pid so the signal handler doesn't try to kill
	   it again */
	child_pid = 0;
	/* Kill the gibson */
	raise(SIGTERM);
      }
    }
  } else {
    /* In the child */
    if (!user) {
      log_warn("No 'user' specified in config file; keeping root privleges\n");
    } else {
      struct passwd *pw = getpwnam(user);
      if (pw == NULL) {
	log_error("Requested user '%s' not found", user);
	raise(SIGTERM);
	exit(EINVAL);
      }
      if (setgid(pw->pw_gid) == -1) {
	log_error("Failed to drop group privileges: %s", strerror(errno));
	raise(SIGTERM);
	exit(errno);
      }
      if (setuid(pw->pw_uid) == -1) {
	log_error("Failed to drop user privileges: %s", strerror(errno));
	raise(SIGTERM);
	exit(errno);
      }
      log_notice("Dropped privileges to %s (%d:%d)\n)", user, pw->pw_uid, pw->pw_gid);
    }
    /* Loop through scan results */
    ble_scan_loop(dd, filter_type);
  }
  return errno;
}

  

