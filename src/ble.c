#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "c3listener.h"
#include "kalman.h"
extern c3_config_t m_config;

#ifdef HAVE_GETTEXT
#include "gettext.h"
#define _(string) gettext(string)
#else
#define _(string) string
#endif /* HAVE_GETTEXT */

static void hexlify(uint8_t* dest, const uint8_t* src, size_t n) {
  memset(dest, 0, n*2+1);
  uint8_t buf[3] = {0};
  for(int i = 0; i < n; i++) {
    sprintf((char *)&buf, "%.2x", src[i]);
    strncat((char *)dest, (char *)&buf, 2);
  }
  return;
}

int ble_scan_loop(int dd, uint8_t filter_type) {
  //int ret = ERR_SUCCESS;

  unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr;
  struct hci_filter nf, of;
  socklen_t olen;
  int len;

  olen = sizeof(of);
  if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
    perror(_("Could not get socket options"));
    return -1;
  }

  hci_filter_clear(&nf);
  hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
  hci_filter_set_event(EVT_LE_META_EVENT, &nf);

  if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
    perror(_("Could not set socket options"));
    return -1;
  }

  while (1) {
    evt_le_meta_event *meta;
    le_advertising_info *info;

    char addr[18];

    while ((len = read(dd, buf, sizeof(buf))) < 0) {
      if (errno == EINTR) {
	perror(_("HCI Socket read interrupted"));
        len = 0;
        goto done;
      }

      if (errno == EAGAIN)
        continue;
      perror(_("Unknown HCI socket error"));
      goto done;
    }
    ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
    len -= (1 + HCI_EVENT_HDR_SIZE);

    meta = (void *)ptr;

    if (meta->subevent != 0x02) {
      printf(_("Failed to set HCI Socket Filter"));
      goto done;
    }

    int num_reports, offset = 0;
    num_reports = meta->data[0];

    /* Get packet detection timestamp */
    struct timespec packet_arrival_tv;
    clock_gettime(CLOCK_MONOTONIC, &packet_arrival_tv);
    double packet_ts = timespec_to_seconds(packet_arrival_tv);
    
    for (int i = 0; i < num_reports; i++) {
      info = (le_advertising_info *)(meta->data + offset + 1);
      //log_stdout("\tData length: %d\nData: %s", info->length, strbuf);
      if (memcmp(info->data, "\x02\x01\x04\x1a\xff", 5)
	  || info->length != 30) {
	continue;
      }
      int8_t rssi;
      double krssi;
      //data[info->length+1] = 0;
      //memcpy(data, &info->data, info->length);
      //hexlify(hdata, data, info->length);

      ba2str(&info->bdaddr, addr);
      offset += info->length + 11;
      memcpy(&rssi, (int8_t *)(meta->data + offset - 1), 1);
      char buf[1+31+1+6+HOSTNAME_MAX_LEN] = {0},
	i = 0, hostlen = 0;
      memcpy(buf+i, &info->length, 1);
      i+=1;
      memcpy(buf+i, info->data, info->length);
      i+=info->length;
      uint8_t *uuid=info->data+9;
      uint16_t major = 0, minor = 0;
      memcpy(&major, info->data+25, 2);
      memcpy(&minor, info->data+27, 2);
      major = (major & 0x00ff) << 8 | major >> 8;
      minor = (minor & 0x00ff) << 8 | minor >> 8;
      /* char strbuf[63]; */
      /* hexlify((uint8_t*)&strbuf, uuid, 16); */
      /* log_stdout("\t UUID: %s MAJOR: %d MINOR: %d\n", strbuf, major, minor); */
      beacon_t* b = hash(uuid, major, minor);
      krssi = kalman(b, rssi, packet_ts);
      log_stdout("%d, %f\n", rssi, krssi);
      memcpy(buf+i, &rssi, 1);
      i+=1;
      memcpy(buf+i, &info->bdaddr, 6);
      i+=6;
      hostlen = strlen(m_config.hostname);
      memcpy(buf+i, m_config.hostname, hostlen);
      i+=hostlen;
      udp_send(buf, i);
    }
  }

 done:
  setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
  
  if (len < 0)
    perror("WTF");
    return -1;
  
  return 0;
}
