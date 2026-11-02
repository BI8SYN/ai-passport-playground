#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "host/ble_uuid.h"
struct os_mbuf;
/* Exact fixed ESP-IDF5.5.3 NimBLE exported function signature. No private data. */
typedef int hm_att_access_fn(uint16_t conn, uint16_t attr, uint8_t op, uint16_t offset, struct os_mbuf **om, void *arg);
int __wrap_ble_att_svr_register(const ble_uuid_t *uuid, uint8_t flags, uint8_t min_key_size, uint16_t *handle, hm_att_access_fn *access, void *arg);
bool hm_cccd_guard_verified(void);
