#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "beacon.h"
#include "config.h"
#include "hash.h"
#include "kalman.h"
#include "log.h"
#include "report.h"
#include "time_util.h"

#ifdef HAVE_GETTEXT
#include "gettext.h"
#define _(string) gettext(string)
#else
#define _(string) string
#endif /* HAVE_GETTEXT */

extern int dd;

char *hexlify(const uint8_t *src, size_t n) {
    char *buf = malloc(n * 2 + 1);
    memset(buf, 0, n * 2 + 1);
    for (size_t i = 0; i < n; i++) {
        sprintf(buf + (i * 2), "%.2x", src[i]);
    }
    return buf;
}

int ble_init(int dev_id) {
    /* Always happens in parent running as root */
    int ctl, err, dd;
    uint8_t own_type = 0x00;
    uint8_t scan_type = 0x01;
    uint8_t filter_policy = 0x00;
    uint16_t interval = htobs(0x00F0);
    uint16_t window = htobs(0x00F0);

    if (dev_id < 0) {
        log_warn("Bluetooth interface invalid or not specified, trying first "
                 "interface\n");
        dev_id = hci_get_route(NULL);
    }

    /* Get a control socket so we can bring the interface up, if needed */
    if ((ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0) {
        log_error("Can't open HCI socket: %s", strerror(errno));
        exit(errno);
    }

    if (ioctl(ctl, HCIDEVUP, dev_id) < 0) {
        if (errno != EALREADY) {
            log_error("Could not open bluetooth device", strerror(errno));
            exit(errno);
        } else {
            log_notice("Using interface hci%d\n", dev_id);
        }
    } else {
        log_notice("Brought up interface hci%d", dev_id);
    }

    if ((dd = hci_open_dev(dev_id)) < 0) {
        log_error(_("Could not open bluetooth device"), strerror(errno));
        exit(errno);
    }

    err = hci_le_set_scan_parameters(dd, scan_type, interval, window, own_type,
                                     filter_policy, 1000);
    if (err < 0) {
        log_error(_("Set scan parameters failed"), strerror(errno));
        exit(errno);
    }

    uint8_t filter_dup = 0;
    err = hci_le_set_scan_enable(dd, 0x1, filter_dup, 1000);
    if (err < 0) {
        log_error(_("Enable scan failed"), strerror(errno));
        exit(errno);
    }
    struct hci_filter nf, of;
    socklen_t olen = sizeof(of);
    if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
        log_error(_("Could not get socket options"), strerror(errno));
        raise(SIGTERM); /* Need to cleanup BLE explicitly once we start the scan
                           */
        exit(errno);
    }

    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);

    if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
        log_error(_("Could not set socket options"), strerror(errno));
        raise(SIGTERM);
        exit(errno);
    }
    return dd;
}

void ble_readcb(struct bufferevent *bev, void *ptr) {
    UNUSED(ptr);

    uint8_t buf[HCI_MAX_EVENT_SIZE], *p = buf + 1 + HCI_EVENT_HDR_SIZE;
    int n;
    //double ts = time_now();

    struct evbuffer *input = bufferevent_get_input(bev);
    while ((n = evbuffer_remove(input, buf, sizeof(buf))) > 0) {
      if (p[0] != 0x02) {
	log_error(_("Failed to set HCI Socket Filter"));
	exit(1);
      }

        uint_fast8_t num_reports = p[1];
	uint8_t *data = NULL, *rssi_base = NULL;
        for (uint_fast8_t i = 0; i < num_reports; i++) {
	    uint8_t *base = p+2,
	      evt_type = *(base+i),
	      addr_type = *(base+num_reports+i),
	      *addr = base+num_reports+num_reports+(i*6),
	      len = *(base+num_reports+num_reports+(num_reports*6)+i);
	    if (evt_type != 3 || addr_type != 1 || len < 18 || len > 23) {
	      /* Secure beacon packets have these characteristics, skip others */
	      continue;
	    }
	    if (data == NULL) {
	      data = base+num_reports+num_reports+(num_reports*6)+num_reports;
	      rssi_base = data;
	      for (int i = 0; i < num_reports; i++) {
		rssi_base += *(base+num_reports+num_reports+(num_reports*6)+i);
	      }
	    } else {
	      data += len;
	    }
	    log_notice("HCI Num Report: %d/%d", i+1, num_reports);
	    log_notice("HCI Event Type: %d", evt_type);
	    log_notice("HCI Addr Type: %d", addr_type);
	    log_notice("MAC: %s", hexlify(addr, 6));
	    log_notice("Len: %d\n", len);
	    log_notice("Packet: %s", hexlify(data, len));
	    
            /* Parse data from HCI Event Report */
            int_fast8_t rssi = (int8_t)*(rssi_base + i);
	    log_notice("RSSI: %d\n", rssi);
	    /* uint8_t full_nonce[16] = {0}; */
	    /* memcpy(full_nonce, addr, 6); */
	    /* memcpy(full_nonce+6, data+2, 10);  */
	    /* log_notice("Nonce: %s\n", hexlify(full_nonce, 16)); */
	    /* log_notice("EID/MAC: %s\n", hexlify(addr, 6)); */
	    /* log_notice("Payload: %s\n", hexlify(data+10, len-16)); */
	    /* log_notice("Tag: %s\n\n", hexlify(data+len-6, 4)); */
	    report_secure(addr, data, len);
            /* uint8_t *uuid = info->data + 9; */
            /* int8_t tx_power = info->data[29]; */
            /* uint16_t major = info->data[25] << 8 | info->data[26]; */
            /* uint16_t minor = info->data[27] << 8 | info->data[28]; */

            /* /\* Lookup beacon *\/ */
            /* beacon_t *b = beacon_find_or_add(uuid, major, minor); */

            /* /\* Derive / Correct Values *\/ */
            /* int8_t cor_rssi = raw_rssi + config_get_antenna_correction(); */
            /* double flt_rssi = kalman(b, cor_rssi, ts); */

            /* /\* Filter Distance Data *\/ */
            /* double flt_dist = */
            /*     pow(10, (tx_power - flt_rssi) / (10 * config_get_path_loss())); */

            /* /\* Correct for HAAB truncating data below 0m *\/ */
            /* b->distance = sqrt(pow(flt_dist, 2) - pow(config_get_haab(), 2)); */
            /* if (isnan(b->distance)) { */
            /*     b->distance = 0; */
            /* } */

            /* b->tx_power = (b->count * b->tx_power + tx_power) / (b->count + 1); */
            /* b->count++; */

            /* /\* Convert variance to meters from RSSI units linearize near */
            /*    current estimate *\/ */
            /* double stddev = */
            /*     sqrt(b->kalman.P[0][0]); /\* Std. dev in RSSI units *\/ */

            /* double min_dist = pow(10, (tx_power - (flt_rssi - stddev)) / */
            /*                               (10 * config_get_path_loss())); */
            /* double max_dist = pow(10, (tx_power - (flt_rssi + stddev)) / */
            /*                               (10 * config_get_path_loss())); */
            /* b->variance = */
            /*     (pow(max_dist - flt_dist, 2) + pow(min_dist - flt_dist, 2)) / 2; */
            /* 	 double raw_dist = */
            /* 	   pow(10, */
            /* 	       ((tx_power - cor_rssi) / (10 *
             * config_get_path_loss()))); */
            /* 	 log_debug("\ */
            /* min: %d, raw/ant_corr/flt/tx_power: %d/%d/%.2f/%d, raw/flt/haab:
             * %.2f/%.2f/%.2f, var: %.2f, error: %.2fm\n", */
            /* 		   minor, */
            /* 		   raw_rssi, cor_rssi, flt_rssi, b->tx_power, */
            /* 		   raw_dist, flt_dist, b->distance, */
            /* 		   b->variance, sqrt(b->variance)); */
        }
    }
}

