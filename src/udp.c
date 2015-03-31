#include <sys/socket.h>

struct sockaddr_in c3ld;

int init_udp(char *ip, int port) {
  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("cannot create socket");
    return 0;
  }

  memset((char *)&myaddr, 0, sizeof(myaddr));
  myaddr.sin_family = AF_INET;
  myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  myaddr.sin_port = htons(port);
  if (bind(fd, (struct sockaddr *)&c3ld, sizeof(c3ld)) < 0) {
    perror("bind failed");
    return 0;
  }
}
