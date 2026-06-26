/** pi_buffer.h - PI Buffer Subsystem */
#ifndef PI_BUFFER_H
#define PI_BUFFER_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_BUFFER_MAX_QUEUE_SIZE 100000
#define PI_BUFFER_DEFAULT_MAX_EVENTS 50000
#define PI_BUFFER_DEFAULT_TIMEOUT_MS 5000
#define PI_BUFFER_MAX_RETRIES 10
typedef enum { PI_BUFFER_MODE_BUFFERED=0, PI_BUFFER_MODE_DIRECT=1, PI_BUFFER_MODE_QUEUED=2 } pi_buffer_mode_t;
typedef struct { int32_t point_id; pi_value_t value; int32_t source_id; pi_timestamp_t received_time; int32_t retry_count, expired; } pi_buffer_event_t;
typedef struct { int64_t events_received, events_sent, events_dropped, events_expired, events_current; int64_t bytes_buffered, connection_loss_count, flush_count; double avg_queue_latency_ms, max_queue_latency_ms; pi_timestamp_t last_connection_loss, last_connection_restore; } pi_buffer_stats_t;
typedef struct { int32_t max_events, send_timeout_ms, retry_max, persist_to_disk; pi_buffer_mode_t mode; int32_t auto_flush; } pi_buffer_config_t;
typedef struct { pi_buffer_event_t queue[PI_BUFFER_MAX_QUEUE_SIZE]; int32_t head, tail, count; pi_buffer_config_t config; pi_buffer_stats_t stats; int32_t connected; pi_timestamp_t last_flush_time; int32_t overflow_count; } pi_buffer_t;
void pi_buffer_init(pi_buffer_t *buf, const pi_buffer_config_t *config);
void pi_buffer_destroy(pi_buffer_t *buf);
int pi_buffer_enqueue(pi_buffer_t *buf, int32_t pid, const pi_value_t *v, int32_t src);
int pi_buffer_flush(pi_buffer_t *buf, int max_send);
int pi_buffer_send_one(pi_buffer_t *buf, const pi_buffer_event_t *event);
void pi_buffer_set_connected(pi_buffer_t *buf);
void pi_buffer_set_disconnected(pi_buffer_t *buf);
int pi_buffer_is_connected(const pi_buffer_t *buf);
int32_t pi_buffer_queue_size(const pi_buffer_t *buf);
int pi_buffer_is_full(const pi_buffer_t *buf);
int pi_buffer_is_empty(const pi_buffer_t *buf);
double pi_buffer_utilization_pct(const pi_buffer_t *buf);
int pi_buffer_expire_old(pi_buffer_t *buf, double max_age_seconds);
const pi_buffer_stats_t* pi_buffer_get_stats(const pi_buffer_t *buf);
void pi_buffer_reset_stats(pi_buffer_t *buf);
#ifdef __cplusplus
}
#endif
#endif
