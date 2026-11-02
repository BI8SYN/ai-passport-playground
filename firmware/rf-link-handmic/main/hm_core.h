#ifndef HM_CORE_H
#define HM_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HM_JSON_BYTES 513
#define HM_AUDIO_BYTES 100
#define HM_ADPCM_AUDIO_BYTES 100
#define HM_FLOW_BYTES 24
#define HM_CREDIT_WINDOW 32

typedef enum { HM_DIRECTION_NONE, HM_DIRECTION_MIC, HM_DIRECTION_SPEAKER } hm_direction_t;
typedef struct { uint32_t rx_packets, tx_packets, seq_missing, duplicate, late, overflow, underrun, io_errors; } hm_stats_t;
typedef struct { bool valid; uint32_t frequency_a_hz, frequency_b_hz, age_s; uint64_t received_ms, expires_ms; } hm_tele_t;
typedef struct { bool valid; char grid[7], model[16], antenna[16]; uint32_t frequency_hz, height_m, age_s; uint64_t received_ms, expires_ms; } hm_rig_t;
typedef struct { bool valid, accuracy_known, distance_known, approx; char source[9]; int32_t lat_e7, lon_e7; uint32_t accuracy_m, distance_m, age_s, ref_age_s; uint64_t received_ms, expires_ms, distance_expires_ms, distance_received_ms; } hm_position_t;
typedef struct {
    bool connected, audio_ready, ready, physical_ptt, pending_ptt, wait_release, rejected, muted, tx_allowed, talker_active;
    uint8_t volume; int battery;
    uint32_t sid, context_id, turn_id, press_id, stream_id, talker_uid, next_seq, grant_end, tx_seq;
    hm_direction_t direction;
    char network[11], phase[5], callsign[25], server[49], talker_callsign[25];
    char stop_reason[33]; /* Local UI outcome only; never serialized to wire status. */
    hm_tele_t tele; hm_rig_t rig; hm_position_t position;
} hm_snapshot_t;
typedef struct { uint32_t id; char request[513], response[513]; } hm_cache_entry_t;
typedef struct {
    bool (*save_volume)(uint8_t value, void *ctx);
    void *save_volume_ctx;
    bool (*audio_report)(const char *section, char out[400]);
    hm_snapshot_t s; char device_id[37];
    uint32_t sid_counter, request_high, turn_high, stream_high, event_seq;
    uint16_t gatt_max, value_max; uint8_t cache_next, format;
    hm_cache_entry_t cache[16], hello;
    uint64_t last_flow_ms, stream_started_ms, last_pcm_ms, mic_flow_ms, press_started_ms, consume_ms, tx_clock_ms;
    bool first_pcm, first_credit, tx_discontinuity, audio_available, remote_volume_busy;
    uint32_t peer_next, peer_end, tx_clock_seq, rx_seen_high, received_mask, stats_stream;
    hm_stats_t stats;
    /* BLE transport totals for this boot, supplied by the runtime. */
    uint32_t transport_boot_submitted, transport_boot_dropped;
    uint8_t event_head, event_count;
    char events[4][513];
    char metadata_snapshot[3][513];
    uint64_t metadata_expiry[3], metadata_distance_expiry;
} hm_core_t;
/* All calls are serialized by caller; timestamps are monotonic milliseconds.
 * open is only for an authenticated, trusted, fully subscribed BLE connection.
 * close invalidates it. No heap survives a command. No task, BLE or I/O here. */
void hm_core_init(hm_core_t *, const char *device_uuid, uint8_t volume);
void hm_core_open(hm_core_t *, uint16_t max_gatt_value, uint64_t now);
void hm_core_close(hm_core_t *);
bool hm_core_command(hm_core_t *, const char *json, size_t len, uint64_t now, char out[HM_JSON_BYTES]);
bool hm_core_press(hm_core_t *, bool down, uint64_t now, char event[HM_JSON_BYTES]);
bool hm_core_tick(hm_core_t *, uint64_t now, char event[HM_JSON_BYTES]);
bool hm_core_on_flow(hm_core_t *, const uint8_t *, size_t len, uint64_t now);
bool hm_core_make_flow(const hm_core_t *, uint8_t out[HM_FLOW_BYTES]);
bool hm_core_audio_accept(hm_core_t *, const uint8_t *, size_t len, uint64_t now);
/* pcm contains frame_samples*2 S16LE bytes for the negotiated mode (320).
 * out must hold HM_AUDIO_BYTES; send only hm_core_audio_bytes() on success. */
bool hm_core_mic_frame(hm_core_t *, const uint8_t *pcm, uint64_t now, uint8_t out[HM_AUDIO_BYTES]);
unsigned hm_core_frame_ms(const hm_core_t *);
unsigned hm_core_frame_samples(const hm_core_t *);
size_t hm_core_audio_bytes(const hm_core_t *);
void hm_core_advance(hm_core_t *, uint32_t next_seq, uint64_t now);
void hm_core_get_snapshot(hm_core_t *, uint64_t now, hm_snapshot_t *);
void hm_core_status(const hm_core_t *, char out[HM_JSON_BYTES]);
void hm_core_info(const hm_core_t *, char out[HM_JSON_BYTES]);
void hm_core_set_volume_store(hm_core_t *, bool (*save)(uint8_t, void *), void *ctx);
/* Returns applied success; event may be empty when no sid is active. */
bool hm_core_set_volume(hm_core_t *, uint8_t value, char event[HM_JSON_BYTES]);
void hm_core_set_battery(hm_core_t *, int percent, char event[HM_JSON_BYTES]);
/* Local menu/audio failures stop without waiting for App. */
void hm_core_stop(hm_core_t *, const char *reason, char event[HM_JSON_BYTES]);
void hm_core_set_audio_available(hm_core_t *, bool available);
bool hm_core_speaker_slot(hm_core_t *, uint64_t now, uint32_t *consumed_seq);

/* Local preview/rollback applies immediately and emits without persistent I/O. */
void hm_core_preview_volume(hm_core_t *,uint8_t,char event[HM_JSON_BYTES]);
#endif
