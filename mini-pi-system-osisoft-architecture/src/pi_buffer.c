/**
 * pi_buffer.c - PI Buffer Subsystem Implementation
 * Store-and-forward circular queue with connection management.
 * Knowledge: L1-L5  MIT 6.302  RWTH Aachen Industrial Control
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../include/pi_buffer.h"

void pi_buffer_init(pi_buffer_t *buf, const pi_buffer_config_t *config) {
    if (!buf) return;
    memset(buf, 0, sizeof(*buf));
    if (config) memcpy(&buf->config, config, sizeof(pi_buffer_config_t));
    else { buf->config.max_events = PI_BUFFER_DEFAULT_MAX_EVENTS; buf->config.send_timeout_ms = PI_BUFFER_DEFAULT_TIMEOUT_MS; buf->config.retry_max = PI_BUFFER_MAX_RETRIES; buf->config.mode = PI_BUFFER_MODE_BUFFERED; }
    buf->connected = 1;
    buf->last_flush_time = PI_TIME_EMPTY;
}

void pi_buffer_destroy(pi_buffer_t *buf) {
    if (!buf) return;
    memset(buf, 0, sizeof(*buf));
}

int pi_buffer_enqueue(pi_buffer_t *buf, int32_t point_id, const pi_value_t *value, int32_t source_id) {
    if (!buf || !value) return -1;
    if (pi_buffer_is_full(buf)) { buf->stats.events_dropped++; buf->overflow_count++; return -1; }
    pi_buffer_event_t *e = &buf->queue[buf->tail];
    e->point_id = point_id;
    memcpy(&e->value, value, sizeof(pi_value_t));
    e->source_id = source_id;
    pi_timestamp_now(&e->received_time);
    e->retry_count = 0;
    e->expired = 0;
    buf->tail = (buf->tail + 1) % PI_BUFFER_MAX_QUEUE_SIZE;
    buf->count++;
    buf->stats.events_received++;
    buf->stats.events_current++;
    pi_timestamp_t now; pi_timestamp_now(&now);
    double latency = pi_timestamp_diff_seconds(&e->received_time, &now) * 1000.0;
    if (latency > buf->stats.max_queue_latency_ms) buf->stats.max_queue_latency_ms = latency;
    return 0;
}

int pi_buffer_flush(pi_buffer_t *buf, int max_send) {
    if (!buf || max_send <= 0) return 0;
    if (!buf->connected) return 0;
    int sent = 0;
    while (buf->count > 0 && sent < max_send) {
        pi_buffer_event_t *e = &buf->queue[buf->head];
        if (e->expired) {
            buf->head = (buf->head + 1) % PI_BUFFER_MAX_QUEUE_SIZE;
            buf->count--;
            buf->stats.events_current--;
            continue;
        }
        if (pi_buffer_send_one(buf, e) == 0) {
            sent++;
            buf->head = (buf->head + 1) % PI_BUFFER_MAX_QUEUE_SIZE;
            buf->count--;
            buf->stats.events_sent++;
            buf->stats.events_current--;
        } else { break; }
    }
    if (sent > 0) { buf->stats.flush_count++; pi_timestamp_now(&buf->last_flush_time); }
    return sent;
}

int pi_buffer_send_one(pi_buffer_t *buf, const pi_buffer_event_t *event) {
    if (!buf || !event) return -1;
    (void)event;
    /* Simulated send: always succeeds in test/offline context. */
    return 0;
}

void pi_buffer_set_connected(pi_buffer_t *buf) {
    if (!buf) return;
    if (!buf->connected) {
        pi_timestamp_now(&buf->stats.last_connection_restore);
        buf->stats.connection_loss_count++;
    }
    buf->connected = 1;
}

void pi_buffer_set_disconnected(pi_buffer_t *buf) {
    if (!buf) return;
    if (buf->connected) pi_timestamp_now(&buf->stats.last_connection_loss);
    buf->connected = 0;
}

int pi_buffer_is_connected(const pi_buffer_t *buf) {
    return buf ? buf->connected : 0;
}

int32_t pi_buffer_queue_size(const pi_buffer_t *buf) {
    return buf ? buf->count : 0;
}

int pi_buffer_is_full(const pi_buffer_t *buf) {
    if (!buf) return 0;
    return buf->count >= buf->config.max_events ? 1 : 0;
}

int pi_buffer_is_empty(const pi_buffer_t *buf) {
    return buf ? (buf->count == 0 ? 1 : 0) : 1;
}

double pi_buffer_utilization_pct(const pi_buffer_t *buf) {
    if (!buf || buf->config.max_events <= 0) return 0.0;
    return 100.0 * (double)buf->count / (double)buf->config.max_events;
}

int pi_buffer_expire_old(pi_buffer_t *buf, double max_age_seconds) {
    if (!buf) return 0;
    int expired = 0, i, pos = buf->head;
    pi_timestamp_t now; pi_timestamp_now(&now);
    for (i = 0; i < buf->count; i++) {
        pi_buffer_event_t *e = &buf->queue[pos];
        if (!e->expired) {
            double age = pi_timestamp_diff_seconds(&e->received_time, &now);
            if (age > max_age_seconds) {
                e->expired = 1;
                buf->stats.events_expired++;
                expired++;
            }
        }
        pos = (pos + 1) % PI_BUFFER_MAX_QUEUE_SIZE;
    }
    return expired;
}

const pi_buffer_stats_t* pi_buffer_get_stats(const pi_buffer_t *buf) {
    return buf ? &buf->stats : NULL;
}

void pi_buffer_reset_stats(pi_buffer_t *buf) {
    if (!buf) return;
    memset(&buf->stats, 0, sizeof(buf->stats));
    buf->overflow_count = 0;
}

/* ─── Buffer Persistence ────────────────────────────────────────── */
int pi_buffer_save_to_disk(const pi_buffer_t *buf, const char *filepath) {
    if (!buf || !filepath) return -1;
    FILE *fp = fopen(filepath, "wb");
    if (!fp) return -2;
    fwrite(&buf->count, sizeof(int32_t), 1, fp);
    fwrite(&buf->head, sizeof(int32_t), 1, fp);
    fwrite(&buf->tail, sizeof(int32_t), 1, fp);
    int i, pos = buf->head;
    for (i = 0; i < buf->count; i++) {
        fwrite(&buf->queue[pos], sizeof(pi_buffer_event_t), 1, fp);
        pos = (pos + 1) % PI_BUFFER_MAX_QUEUE_SIZE;
    }
    fclose(fp);
    return 0;
}

int pi_buffer_load_from_disk(pi_buffer_t *buf, const char *filepath) {
    if (!buf || !filepath) return -1;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -2;
    int32_t saved_count, saved_head, saved_tail;
    if (fread(&saved_count, sizeof(int32_t), 1, fp) != 1) { fclose(fp); return -3; }
    if (fread(&saved_head, sizeof(int32_t), 1, fp) != 1) { fclose(fp); return -3; }
    if (fread(&saved_tail, sizeof(int32_t), 1, fp) != 1) { fclose(fp); return -3; }
    buf->count = 0; buf->head = 0; buf->tail = 0;
    int i;
    for (i = 0; i < saved_count; i++) {
        pi_buffer_event_t ev;
        if (fread(&ev, sizeof(pi_buffer_event_t), 1, fp) != 1) break;
        pi_buffer_enqueue(buf, ev.point_id, &ev.value, ev.source_id);
    }
    fclose(fp);
    return i;
}
