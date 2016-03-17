#include <pwd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <sys/socket.h>

#include <string.h>
#include <errno.h>
#include <assert.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <sys/types.h>
#include <sys/wait.h>

#include "beacon.h"
#include "ble.h"
#include "config.h"
#include "http.h"
#include "log.h"
#include "report.h"
#include "udp.h"
#include "ipc.h"

#define EVLOOP_NO_EXIT_ON_EMPTY 0x04

#include <event2/event.h>
#include <event2/http.h>
static void log_cb(int severity, const char *msg) {
  syslog(severity, msg);
  return;
}

/* Config and other globals */
int dd = 0, child_pid = 0;
const uint8_t filter_type = 0, filter_dup = 0;
struct event_base *base;

/* Sockets linking parent and child for IPC */
int ipc_sock_pair[2];
struct bufferevent *ipc_bev = {0};


void sigint_handler(int);
void do_parent(void);
void do_child(void);

int main(int argc, char **argv) {
  config_start(argc, argv);

  event_set_log_callback(log_cb);
  if (config_debug()) {
    event_enable_debug_mode();
  }

  log_notice("Starting c3listener %s\n", PACKAGE_VERSION);
  fflush(stdout);

  /* Daemonize */
  if (!config_debug()) {
    if (daemon(0, 0)) {
      perror("Daemonizing failed");
      exit(errno);
    }
  }

  base = event_base_new();

  /* Setup BLE pre-fork, child will not have permissions */
  dd = ble_init(config_get_hci_interface());
  evutil_make_socket_nonblocking(dd);
  
  /* Setup Web Server, pre-fork to get low port */
  struct evhttp *http = evhttp_new(base);
  evhttp_bind_socket(http, "*", 80);
  evhttp_set_gencb(http,
		   http_main_cb,
		   (void *)config_get_webroot()); 

  /* Setup sockets for parent/child IPC */
  errno = 0;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, ipc_sock_pair) < 0) {
    log_error("Socket pair failed: %s\n", strerror(errno));
    exit(errno);
  }
  
  child_pid = fork();
  if (child_pid < 0) {
    log_error("Failed to spawn child", strerror(errno));
    exit(errno);
  }
  if (child_pid > 0) {
    do_parent();
  } else {
    /* We need to reinit the event_base in the child process */
    event_reinit(base);
    do_child();
  }
  return errno;
}

void do_parent(void) {
  /* We are parent */
  /* Set the signal handlers to cleanup BLE priv. socket */
  signal(SIGINT, sigint_handler);
  signal(SIGTERM, sigint_handler);
  signal(SIGHUP, sigint_handler);
  int status;

  close(ipc_sock_pair[1]);
  ipc_bev = bufferevent_socket_new(base, ipc_sock_pair[0], BEV_OPT_DEFER_CALLBACKS);
  evutil_make_socket_nonblocking(ipc_sock_pair[0]);
  bufferevent_setcb(ipc_bev, ipc_parent_readcb, NULL, NULL, NULL);
  bufferevent_enable(ipc_bev, EV_READ|EV_WRITE);

  event_base_dispatch(base);
  
  while (true) {
    /* Loop for child events, if the child exits; then cleanup */
    log_notice("Parent not running event loop\n");
    pid_t pid = waitpid(-1, &status, 0);
    /* If the parent was signaled, the signal handler will never
       give back control. So we should only reach this code if the
       child exits or receives a signal */
    if (WIFSIGNALED(status) || WIFEXITED(status)) {
      /* A child has ended */
      if (WIFSIGNALED(status)) {
        log_notice("Child %d exited by signal: %s", pid,
                   strsignal(WTERMSIG(status)));
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
}

void do_child(void) {
  /* In the child */
  close(ipc_sock_pair[0]);
  ipc_bev = bufferevent_socket_new(base, ipc_sock_pair[1], 0);
  evutil_make_socket_nonblocking(ipc_sock_pair[1]);
  bufferevent_setcb(ipc_bev, ipc_child_readcb, NULL, NULL, NULL);
  bufferevent_enable(ipc_bev, EV_READ|EV_WRITE);
  
  const char *user = config_get_user();
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
  log_notice("Dropped privileges to %s (%d:%d)\n)", user, pw->pw_uid,
             pw->pw_gid);

  /* Setup a bufferevent to process BLE scan results */
  struct bufferevent *ble_bev = bufferevent_socket_new(base, dd, 0);
  bufferevent_setcb(ble_bev, ble_readcb, NULL, NULL, NULL);
  bufferevent_enable(ble_bev, EV_READ);

  /* Setup a bufferevent to write to and ack the server */
  int fd = udp_init(config_get_remote_hostname(), config_get_remote_port());
  struct bufferevent *udp_bev = bufferevent_socket_new(base, fd, 0);
  bufferevent_setcb(udp_bev, udp_readcb, NULL, NULL, NULL);
  bufferevent_enable(udp_bev, EV_READ|EV_WRITE);
  report_init(udp_bev);
  
  /* Setup a timer for sending report */
  struct event *report_ev = event_new(base, -1, EV_PERSIST, report_cb, NULL);
  struct timeval report_tv = config_get_report_interval();
  evtimer_add(report_ev, &report_tv);
  
  /* Setup a GC timer */

  /* Loop on established events */
  event_base_dispatch(base);
}

void sigint_handler(int signum) {
  log_notice("Parent got signal: %d\n", signum);
  if (child_pid > 0) {
    /* If the child has been started, we need to kill it */
    log_notice("Killing scanning process\n");
    kill(child_pid, SIGTERM);
  }
  config_cleanup();
  if (hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000) < 0) {
    log_error("Disable scan failed", strerror(errno));
  } else {
    log_notice("Scan disabled\n");
  }
  if (hci_close_dev(dd) < 0) {
    log_error("Closing HCI Socket Failed\n");
  } else {
    log_notice("HCI Socket Closed\n");
  }
  event_base_free(base);
  fflush(stderr);
  exit(errno);
}
