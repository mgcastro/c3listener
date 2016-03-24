#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <libconfig.h>

#include "config.h"
#include "log.h"

/* Structure for libconfig */
static config_t cfg;

/* Structure holding local c3listener config */
static c3_cli_config_t cli_cfg = {
    .hci_dev_id = -1,
    .debug = false,
    .config_file = NULL,
    .user = NULL,
    .webroot = NULL
};

static void config_do_cli(int argc, char **argv) {
  int c;
  while (1) {
    static struct option long_options[] = {
        /* These options set a flag. */
        {"debug", no_argument, 0, 'd'},
        /* These options don’t set a flag.
           We distinguish them by their indices. */
        {"config", required_argument, 0, 'c'},
        {"user", required_argument, 0, 'u'},
        {"interface", required_argument, 0, 'i'},
	{"webroot", required_argument, 0, 'w'},
        {0, 0, 0, 0}
    };
    /* getopt_long stores the option index here. */
    int option_index = 0;

    c = getopt_long(argc, argv, "dc:u:i:w:", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c) {
    case 'i':
      cli_cfg.hci_dev_id = atoi(optarg + 3);
      break;
    case 'c':
      cli_cfg.config_file = calloc(strlen(optarg) + 1, 1);
      memcpy(cli_cfg.config_file, optarg, strlen(optarg) + 1);
      break;
    case 'u':
      cli_cfg.user = calloc(strlen(optarg) + 1, 1);
      memcpy(cli_cfg.user, optarg, strlen(optarg) + 1);
      break;
    case 'w':
      cli_cfg.webroot = calloc(strlen(optarg) + 1, 1);
      memcpy(cli_cfg.webroot, optarg, strlen(optarg));
      break;
    case '?':
      /* getopt_long already printed an error message. */
      break;
    case 'd':
      /* Debug, keep in foreground */
      cli_cfg.debug = true;
      break;
    }
    if (c == -1)
      break;
  }
  return;
}

static void config_do_file(void) {
  char *filename =
      cli_cfg.config_file ? cli_cfg.config_file : DEFAULT_CONFIG_FILE;
  if (!config_read_file(&cfg, filename)) {
    log_error("Problem with config file: %s: %s:%d - %s\n", filename,
              config_error_file(&cfg), config_error_line(&cfg),
              config_error_text(&cfg));
    exit(1);
  }
}

void config_start(int argc, char **argv) {
  config_init(&cfg);
  config_do_cli(argc, argv);
  log_init();
  config_do_file();
}

int config_get_hci_interface(void) {
  if (cli_cfg.hci_dev_id > 0) {
    return cli_cfg.hci_dev_id;
  } else {
    const char *buf;
    if (config_lookup_string(&cfg, "interface", &buf)) {
      if (strlen(buf) > 3) {
        return atoi(buf + 3);
      } else {
        log_warn("Bad interface in config file: %s", buf);
        return DEFAULT_HCI_INTERFACE;
      }
    } else {
      return DEFAULT_HCI_INTERFACE;
    }
  }
}

bool config_debug(void) {
  return cli_cfg.debug;
}

const char *config_get_remote_hostname(void) {
  const char *buf;
  if (config_lookup_string(&cfg, "server", &buf)) {
    return buf;
  } else {
    return DEFAULT_REMOTE_HOSTNAME;
  }
}

const char *config_get_remote_port(void) {
  const char *buf;
  if (config_lookup_string(&cfg, "port", &buf)) {
    return buf;
  } else {
    return DEFAULT_PORT;
  }
}

double config_get_path_loss(void) {
  double buf;
  if (config_lookup_float(&cfg, "path_loss", &buf)) {
    return buf;
  } else {
    return DEFAULT_PATH_LOSS;
  }
}

double config_get_haab(void) {
  double buf;
  if (config_lookup_float(&cfg, "haab", &buf)) {
    return buf;
  } else {
    return DEFAULT_HAAB;
  }
}

int config_get_antenna_correction(void) {
  int buf;
  if (config_lookup_int(&cfg, "antenna_correction", &buf)) {
    return buf;
  } else {
    return DEFAULT_ANTENNA_CORRECTION;
  }
}

struct timeval config_get_report_interval(void) {
  int buf;
  if (!config_lookup_int(&cfg, "report_interval", &buf)) {
    buf = DEFAULT_REPORT_INTERVAL_MSEC;
  }
  struct timeval r = { buf/1000, buf%1000*1000 };
  return r;
}

const char *config_get_user(void) {
  if (cli_cfg.user != NULL) {
    return cli_cfg.user;
  } else {
    const char *buf;
    if (config_lookup_string(&cfg, "user", &buf)) {
      return buf;
    } else {
      return DEFAULT_USER;
    }
  }
}

static bool is_dir_p(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

const char *config_get_webroot(void) {
  if (cli_cfg.webroot != NULL) {
    log_notice("webroot: %s (CLI)\n", cli_cfg.webroot);
    return cli_cfg.webroot;
  }
  const char *buf;
  if (config_lookup_string(&cfg, "webroot", &buf)) {
    if (is_dir_p(buf)) {
      log_notice("webroot: %s (config file)\n", buf);
      return buf;
    }
  }
#ifdef WEBROOT
  if (is_dir_p(WEBROOT)) {
    log_notice("webroot: %s (cmake)\n", WEBROOT);
    return WEBROOT;
  }
#endif
  log_notice("webroot: %s (default)\n", DEFAULT_WEBROOT);
  return DEFAULT_WEBROOT;
}

void config_cleanup(void) {
  config_destroy(&cfg);
}
