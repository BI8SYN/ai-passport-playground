#include "hm_cccd_guard.h"
#include "host/ble_att.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,3)
#error "Review the NimBLE ble_att_svr_register ABI and CCCD permissions before updating ESP-IDF"
#endif
extern int __real_ble_att_svr_register(const ble_uuid_t *, uint8_t, uint8_t, uint16_t *, hm_att_access_fn *, void *);
static unsigned guarded;
int __wrap_ble_att_svr_register(const ble_uuid_t *uuid,uint8_t flags,uint8_t min_key_size,uint16_t *handle,hm_att_access_fn *access,void *arg) {
    /* NimBLE applies its NOTIFY_INDICATE security flags only to CCCD writes.
     * Add authenticated identity authorization to reads as well. */
    if (ble_uuid_u16(uuid)==0x2902) {
        flags|=BLE_ATT_F_READ_ENC|BLE_ATT_F_READ_AUTHEN|BLE_ATT_F_READ_AUTHOR;
        min_key_size=16;
    }
    int rc=__real_ble_att_svr_register(uuid,flags,min_key_size,handle,access,arg);
    if (!rc && ble_uuid_u16(uuid)==0x2902) guarded++;
    return rc;
}
bool hm_cccd_guard_verified(void) { return guarded>=3; }
