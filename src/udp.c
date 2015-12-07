#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include "c3listener.h"


static struct sockaddr_in c3ld;
static int fd;

void init_udp(char *ip, int port) {
  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("cannot create socket");
    exit(1);
  }

  memset((void *)&c3ld, 0, sizeof(c3ld));
  c3ld.sin_family = AF_INET;
  c3ld.sin_port = htons(port);
  c3ld.sin_addr.s_addr=inet_addr(ip);
  
}

int udp_send(uint8_t *data, uint8_t len) {
  int ret = sendto(fd,
		data,
		len,
		MSG_DONTWAIT,
		(struct sockaddr *)&c3ld, sizeof(c3ld));
  if (ret == -1) {
    log_stdout("Dropping packet\n");
  }
  return ret;
}
