#pragma once

#include <event2/bufferevent.h>

enum __attribute__((__packed__)) ble_evt_t {
    BLE_EVT_TYPE_ADV_IND = 0x00,
    BLE_EVT_TYPE_ADV_DIRECT,
    BLE_EVT_TYPE_ADV_SCAN_IND,
    BLE_EVT_TYPE_ADV_NONCONN_IND,
    BLE_EVT_TYPE_SCAN_RSP,
};

enum __attribute__((__packed__)) ble_addr_t {
    BLE_ADDR_PUBLIC = 0x00,
    BLE_ADDR_RANDOM,
    BLE_ADDR_PUBLIC_IDENTITY,
    BLE_ADDR_RANDOM_IDENTITY
};

typedef struct ble_report_t {
    /* One report in the HCI event */
    enum ble_evt_t evt_type; /* 0x00 -- 0x04 */
    enum ble_addr_t addr_type;
    uint8_t addr[6];
    uint8_t data_len;
    uint8_t const *data;
    int8_t rssi;
} ble_report_t;

enum __attribute__((__packed__)) hci_type_t {
    HCI_COMMAND = 0x00,
    HCI_ACL_DATA,
    HCI_SYNC_DATA,
    HCI_EVENT,
};

typedef struct ble_report_hdr_t {
    /* HCI event containing (potentially) several reports */
    enum hci_type_t hci_type;
    uint8_t evt_code;     /* 0x3e */
    uint8_t param_len;    /* Total length of report */
    uint8_t sub_evt_code; /* 0x02 */
    uint8_t num_reports;  /* 0x01 -- 0x19 */
} ble_report_hdr_t;

void ble_readcb(struct bufferevent *bev, void *ptr);
void ble_scan_loop(int, uint8_t);
int ble_init(int);
char *hexlify(const uint8_t *, size_t);
