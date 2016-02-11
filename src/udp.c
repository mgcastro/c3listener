#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <netdb.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "c3listener.h"
#include "log.h"

static int fd;

int udp_init(const char *server_hostname, const char *port) {
  struct addrinfo hints, *result, *rp;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0; /* Any protocol */

  while (true) {
    int s = getaddrinfo(server_hostname, port, &hints, &result);
    if (s != 0) {
      log_error("Server lookup failed: %s\n", gai_strerror(s));
      sleep(5);
    } else {
      break;
    }
  }

  /* getaddrinfo() returns a list of address structures.
     Try each address until we successfully connect(2).
     If socket(2) (or connect(2)) fails, we (close the socket
     and) try the next address. */
  bool conn_p = false;
  while (!conn_p) {
    /* Keep trying until we connect */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
      void *p;
      if (rp->ai_addr->sa_family == AF_INET)
        p = &(((struct sockaddr_in *)rp->ai_addr)->sin_addr);
      else
        p = &(((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr);
      char s[INET6_ADDRSTRLEN];
      inet_ntop(rp->ai_family, p, s, sizeof s);
      fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      fcntl(fd, F_SETFL, O_NONBLOCK);
      if (fd == -1)
        continue;
      if (connect(fd, rp->ai_addr, rp->ai_addrlen) != -1) {
        log_notice("Connected to %s\n", s);
        conn_p = true;
        break; /* Success */
      } else {
        log_warn("Failed to connect: %s\n", strerror(errno));
        close(fd);
        sleep(5);
      }
    }
  }

  freeaddrinfo(result); /* No longer needed */
  return fd;
}

int udp_send(uint8_t *data, uint8_t len) {
  int ret = write(fd, data, len);
  if (ret < len) {
    if (errno == ECONNREFUSED) {
      log_error("Packet refused at server\n");
    }
  }
  /* char *buf[4] = {0}; */
  /* ret = read(fd, buf, 3); */
  /* if (ret > 0) { */
  /*   log_stdout("Got data: %s\n", buf); */
  /* } */
  return ret;
}

void udp_cleanup(void) {
  close(fd);
}

void udp_readcb(struct bufferevent *b, void *c) {
  return;
}
