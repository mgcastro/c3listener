#pragma once

#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>

#define UNUSED(x) (void)(x)

#define HOSTNAME_MAX_LEN 255
#define MAX_HASH_CB                                                            \
    5 /* Max number of functions called on each                                \
         location during hash table walk */

#define GC_INTERVAL_SEC                                                        \
    (MAX_BEACON_INACTIVE_SEC / 2) /* How often to                              \
                                     check for                                 \
                                     inactive                                  \
                                     beacons */

#define KEEP_ALIVE_SEC 30

#define MAX_ACK_INTERVAL_SEC                                                   \
    40 /* Reopen UDP socket if we haven't                                      \
         heard from the server in for this                                     \
         long */

#define MAX_BEACON_INACTIVE_SEC                                                \
    10 /* Free memory for any beacons                                          \
          quietfor this long */

#define DEFAULT_CONFIG_FILE SYSCONFDIR "/c3listener.conf"
#define DEFAULT_REMOTE_HOSTNAME "127.0.0.1"
#define DEFAULT_PORT "9999"
#define DEFAULT_PATH_LOSS 3.2
#define DEFAULT_HCI_INTERFACE 0.0
#define DEFAULT_HAAB 0.0
#define DEFAULT_ANTENNA_CORRECTION 0
#define DEFAULT_REPORT_INTERVAL_MSEC 5000
#define DEFAULT_USER "nobody"
#define DEFAULT_WEBROOT "./web"

#define SERVER_RECONNECT_INTERVAL_SEC 10

#define HTTP_TIMEOUT_SEC 1
#define HTTP_MAX_PENDING_REQUESTS 1000

typedef struct cli_conf {
    int_fast8_t hci_dev_id;
    bool debug;
    char *config_file;
    char *user;
    char *webroot;
} c3_cli_config_t;

void config_cleanup(void);
const char *config_get_user(void);
struct timeval config_get_report_interval(void);
int config_get_antenna_correction(void);
double config_get_haab(void);
double config_get_path_loss(void);
const char *config_get_remote_port(void);
const char *config_get_remote_hostname(void);
bool config_debug(void);
int config_get_hci_interface(void);
void config_start(int argc, char **argv);
const char *config_get_webroot(void);
int config_set(char *, char *);
int config_reboot(void);
void config_refresh(void);
void config_local_write(void);
char *config_get_local_hostname(void);

enum {
    CONFIG_OK = 0,
    CONFIG_CONF_NOT_FOUND = 128,
    CONFIG_CONF_TYPE_MISMATCH,
    CONFIG_CONF_EINVAL,
    CONFIG_CONF_UNSUPPORTED_TYPE,
    CONFIG_CONF_CREATE_ERROR
};
