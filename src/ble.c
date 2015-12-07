#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include "beacon.h"
#include "c3listener.h"
#include "hash.h"
#include "kalman.h"
#include "report.h"
#include "time_util.h"

extern c3_config_t m_config;

#ifdef HAVE_GETTEXT
#include "gettext.h"
#define _(string) gettext(string)
#else
#define _(string) string
#endif /* HAVE_GETTEXT */

char *hexlify(const uint8_t* src, size_t n) {
  char *buf = malloc(n*2+1);
  memset(buf, 0, n*2+1);
  for(int i = 0; i < n; i++) {
    sprintf(buf+(i*2), "%.2x", src[i]);
  }
  return buf;
}

int ble_init(void) {
  int dev_id = 0, dd;
  int err;
  uint8_t own_type = 0x00;
  uint8_t scan_type = 0x01;
  uint8_t filter_policy = 0x00;
  uint16_t interval = htobs(0x00F0); 
  uint16_t window = htobs(0x00F0); 

  if (dev_id < 0)
    dev_id = hci_get_route(NULL);

  dd = hci_open_dev(dev_id);
  /* fcntl(dd, F_SETFL, O_NONBLOCK); */
  
  if (dd < 0) {
    perror(_("Could not open bluetooth device"));
    exit(ERR_NO_BLUETOOTH_DEV);
  }

  err = hci_le_set_scan_parameters(dd, scan_type, interval, window, own_type,
                                   filter_policy, 100);
  if (err < 0) {
    perror(_("Set scan parameters failed"));
    exit(ERR_SCAN_ENABLE_FAIL);
  }

  uint8_t filter_dup = 0;
  err = hci_le_set_scan_enable(dd, 0x1, filter_dup, 100);
  if (err < 0) {
    perror(_("Enable scan failed"));
    exit(ERR_SCAN_ENABLE_FAIL);
  }
  return dd;
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
  
  double last_report = NAN, last_gc = NAN, last_report_attempt = NAN;
  /* double last_gc = NAN, last_packet = NAN; */
  struct pollfd sockets[1];
  int poll_ret;
  sockets[0].fd = dd;
  sockets[0].events = POLLIN;
  double ts = NAN;
  while (1) {
    evt_le_meta_event *meta;
    le_advertising_info *info;

    /* Max interval for walker_cb callback = REPORT_INTERVAL_MSEC + REPORT_INTERVAL_MSEC / 4 */
    poll_ret = poll(sockets, 1, REPORT_INTERVAL_MSEC);
    if (poll_ret > 0 && (sockets[0].revents & POLLIN)) {
      while ((len = read(dd, buf, sizeof(buf))) < 0) {
	if (errno == EINTR) {
	  perror(_("HCI Socket read interrupted"));
	  len = 0;
	  goto done;
	}
      
	if (errno == EAGAIN || errno == EWOULDBLOCK)
	  continue;
	perror(_("Unknown HCI socket error"));
	goto done;
      }
      ts = time_now();
      ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
      len -= (1 + HCI_EVENT_HDR_SIZE);

      meta = (void *)ptr;

      if (meta->subevent != 0x02) {
	printf(_("Failed to set HCI Socket Filter"));
	goto done;
      }

      int num_reports;
      num_reports = meta->data[0];
    
      for (int i = 0; i < num_reports; i++) {
	info = (le_advertising_info *)(meta->data + 1);
	if (memcmp(info->data, "\x02\x01\x04\x1a\xff", 5)
	    || info->length < 30 || info->length > 31) {
	  /* Skip non-ibeacon adverts */
	  continue;
	}
	int rssi_offset = info->length + 10;
	int8_t rssi = meta->data[rssi_offset];
	uint8_t *uuid=info->data+9;
	int8_t tx_power = info->data[29];
	uint16_t major = info->data[25] << 8 | info->data[26];
	uint16_t minor = info->data[27] << 8 | info->data[28];
	/* char *debug = hexlify(uuid, 16); */
	/* log_stdout("\t UUID: %s MAJOR: %d MINOR: %d\n", */
	/* 		 debug, major, minor); */
	/* free(debug); */
	beacon_t* b = beacon_find_or_add(uuid, major, minor);
	b->rssi = kalman(b, rssi, ts);
	b->tx_power = (b->count * b->tx_power + tx_power)/(b->count + 1);
	b->count++;
	log_stdout("%d, %f, %d\n", rssi, b->rssi, tx_power);
	/* report(b); */
      }
    }
    /* log_stdout("Walk diff: %f; Last walk: %f; Packet TS: %f\n", */
    /* 	       packet_ts - last_walk, last_walk, packet_ts); */
    int cb_idx = 0;
    walker_cb func[MAX_HASH_CB] = {NULL};
    void *args[MAX_HASH_CB] = {NULL};
    
    if (isnan(last_report_attempt) ||
	ts - last_report_attempt > REPORT_INTERVAL_MSEC / 1000.0) {
      /* log_stdout("\tlast_report = %f; isnan = %d, %f > %f\n", last_report, isnan(last_report), */
      /* 		 ts - last_report, REPORT_INTERVAL_MSEC / 1000.0);	  */
      report_clear();
      func[cb_idx] = report_beacon;
      args[cb_idx++] = NULL;
    }
    if (isnan(last_gc) ||
	ts - last_gc > GC_INTERVAL_SEC) {
      func[cb_idx] = beacon_expire;
      args[cb_idx++] = &ts;
      /* log_stdout("GC interval = %f\n", ts - last_gc); */
      last_gc = ts;
    }
    hash_walk(func, args, cb_idx);
    /* log_stdout("Poll cycle took %f seconds\n", time_now()-ts); */
    /* If we generated a report this walk, send it */ 
    for (int i = 0; i < MAX_HASH_CB; i++) {
      if (func[i] == report_beacon) {
	if (report_length() > report_header_length() ||
	    ts - last_report > KEEP_ALIVE_SEC || isnan(last_report)) {
	  report_send();
	  /* log_stdout("Report interval = %f\n", ts - last_report); */
	  last_report = ts;
	}
	last_report_attempt = ts;
	break;
      }
    }
  }

 done:
  setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
  
  if (len < 0)
    perror("WTF");
    return -1;
  
  return 0;
}
