#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

#include "beacon.h"
#include "ble.h"
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
    char *buf = calloc(1, n * 2 + 1);
    for (size_t i = 0; i < n; i++) {
        sprintf(buf + (i * 2), "%.2x", src[i]);
    }
    return buf;
}

static ble_report_t *ble_get_report(uint8_t const *const buf,
                                    ble_report_hdr_t const *const hdr,
                                    uint8_t idx)
/* Deinterlace raw hci_evt buffer into structured reports or NULL on
   error*/
{
    ble_report_t *rpt = NULL;
    uint8_t const *p = buf + 2 + idx;
    if (!(rpt = calloc(1, sizeof(ble_report_t)))) {
        log_error("Failed to allocate memory");
        return NULL;
    }
    /* Archane pointer arithmatic. Bluetooth HCI requires brain
       damage; what possible reason could there be for interleaving
       the reports */
    rpt->evt_type = *p;
    p += hdr->num_reports;
    rpt->addr_type = *p;
    p += hdr->num_reports - idx + (idx * 6);
    memcpy(rpt->addr, p, 6);
    /* Move pointer to Length_Data[0] */
    p += (hdr->num_reports - idx) * 6;
    for (uint8_t i = 0, offset = 0; i < hdr->num_reports; i++) {
        if (i == idx) {
            rpt->data_len = *(p + i);
            /* Point p at the offset of this report's data */
            p += offset + hdr->num_reports;
            break;
        }
        offset += *(p + i);
    }
    rpt->data = p;
    rpt->rssi = (int8_t) * (buf + hdr->param_len - hdr->num_reports + idx);
    return rpt;
}

int ble_init(int dev_id) {
    /* Always happens in parent running as root */
    int ctl, err, dd;
    uint8_t own_type = 0x00;
    uint8_t scan_type = 0x01;
    uint8_t filter_policy = 0x00;
    uint16_t interval = htobs(0x0064);
    uint16_t window = htobs(0x0064);

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
    int n = -1;
    double ts = time_now();
    ble_report_hdr_t hdr_buf;

    struct evbuffer *input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) >= sizeof(ble_report_hdr_t)) {
        if ((n = evbuffer_copyout(input, &hdr_buf, sizeof(ble_report_hdr_t))) <
            0) {
            log_error("Failed to read from evbuffer");
            return;
        }

        /* By this point the ble_report_hdr is complete */
        if (evbuffer_get_length(input) < (uint8_t)(hdr_buf.param_len + 2)) {
            /* All the data hasn't arrived yet, set watermark and
               retry later */
            log_notice("Incomplete ble_report");
            bufferevent_setwatermark(bev, EV_READ, hdr_buf.param_len + 2, 0);
            return;
        }

        /* The data has arrived, reset watermark in preparation
           for the next packet */
        bufferevent_setwatermark(bev, EV_READ, sizeof(ble_report_hdr_t), 0);
        /* Drain the bytes not included in param_len field, that makes
           it simpler to resolve rssi later on (without magic
           numbers) */
        evbuffer_drain(input, offsetof(ble_report_hdr_t, param_len) + 1);
        uint8_t *body_buf = NULL;
        if (!(body_buf = calloc(1, hdr_buf.param_len))) {
            evbuffer_drain(input, hdr_buf.param_len);
            log_error("Dropped ble report due to OOM");
            return;
        }
        if (evbuffer_remove(input, body_buf, hdr_buf.param_len) < 0) {
            log_error("Failed to read evbuffer, ble report dropped");
            evbuffer_drain(input, hdr_buf.param_len);
            free(body_buf);
            return;
        }

        for (uint8_t i = 0; i < hdr_buf.num_reports; i++) {
            ble_report_t *rpt = NULL;
            if (!(rpt = ble_get_report(body_buf, &hdr_buf, i))) {
                continue;
            }

            if (rpt->addr_type != 1 || rpt->data_len < 29 ||
                rpt->data_len > 30) {
                /* Skip if this doesn't look like a report from a beacon */
                goto skip;
            }
#if 1
            log_notice("HCI Num Report: %d/%d", i + 1, hdr_buf.num_reports);
            log_notice("HCI Event Type: %d", rpt->evt_type);
            log_notice("HCI Addr Type: %d", rpt->addr_type);
            log_notice("MAC: %s", hexlify(rpt->addr, 6));
            log_notice("Len: %d\n", rpt->data_len);
            log_notice("Packet: %s", hexlify(rpt->data, rpt->data_len));
#endif

            /* Parse data from HCI Event Report */
            int8_t tx_power;
            beacon_t *b;
            if (rpt->data_len == 30) {
                /* Secure packet */
                b = sbeacon_find_or_add(rpt->addr);
                tx_power = rpt->data[30];
            } else {
                uint8_t const *uuid = rpt->data + 9;
                tx_power = rpt->data[29];
                uint16_t major = rpt->data[25] << 8 | rpt->data[26];
                uint16_t minor = rpt->data[27] << 8 | rpt->data[28];

                /* Lookup beacon */
                b = ibeacon_find_or_add(uuid, major, minor);
            }
            /* Derive / Correct Values */
            int8_t cor_rssi = rpt->rssi + config_get_antenna_correction();
            double flt_rssi = kalman(b, cor_rssi, ts);

            /* Filter Distance Data */
            double flt_dist =
                pow(10, (tx_power - flt_rssi) / (10 * config_get_path_loss()));

            /* Correct for HAAB truncating data below 0m */
            b->distance = sqrt(pow(flt_dist, 2) - pow(config_get_haab(), 2));
            if (isnan(b->distance)) {
                b->distance = 0;
            }

            b->tx_power = (b->count * b->tx_power + tx_power) / (b->count + 1);
            b->count++;

            /* Convert variance to meters from RSSI units linearize near
               current estimate */
            double stddev =
                sqrt(b->kalman.P[0][0]); /* Std. dev in RSSI units */

            double min_dist = pow(10, (tx_power - (flt_rssi - stddev)) /
                                          (10 * config_get_path_loss()));
            double max_dist = pow(10, (tx_power - (flt_rssi + stddev)) /
                                          (10 * config_get_path_loss()));
            b->variance =
                (pow(max_dist - flt_dist, 2) + pow(min_dist - flt_dist, 2)) / 2;
#if 0
            double raw_dist = pow(
                10, ((tx_power - cor_rssi) / (10 * config_get_path_loss())));
#endif
            if (b->type == BEACON_IBEACON) {
#if 0
                struct ibeacon_id *id = b->id;
                log_debug("min: %d, raw/ant_corr/flt/tx_power: %d/%d/%.2f/%d, "
                          "raw/flt/haab: %.2f/%.2f/%.2f, var: %.2f, error: "
                          "%.2fm\n",
                          id->minor, rpt->rssi, cor_rssi, flt_rssi, b->tx_power,
                          raw_dist, flt_dist, b->distance, b->variance,
                          sqrt(b->variance));
#endif
            } else if (b->type == BEACON_SECURE) {
#if 0
                struct sbeacon_id *id = b->id;
                char *mac = hexlify(id->mac, 6);
                log_debug(
                    "mac: %s, raw/ant_corr/flt/tx_power: %d/%d/%.2f/%d, "
                    "raw/flt/haab: %.2f/%.2f/%.2f, var: %.2f, error: %.2fm\n",
                    mac, rpt->rssi, cor_rssi, flt_rssi, b->tx_power, raw_dist,
                    flt_dist, b->distance, b->variance, sqrt(b->variance));
		free(mac);
#endif
                report_secure(b, rpt->data, rpt->data_len);
            } else {
                log_warn("Unknown packet");
            }
        skip:
            free(rpt);
        }
        free(body_buf);
    }
}
