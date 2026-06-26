/**
 * @file pi_integrator_transform.c
 * @brief Data transformation pipeline engine — core integrator logic
 *
 * Implements: Pipeline execution, data transform stages (filter, enrich,
 * aggregate, encode, deliver), priority queue scheduling, ring buffer,
 * rate limiter (token bucket), circuit breaker, backpressure control,
 * adaptive batch controller, timer wheel, event bus, health monitor.
 *
 * L1-L8 knowledge embedded per function.
 * L5 (Algorithms): Token bucket, exponential backoff, EMA adaptation.
 * L8 (Advanced): Adaptive batching via EMA, circuit breaker pattern.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../include/pi_integrator_core.h"

/*=== L1: Pipeline ===*/

pi_pipeline_t *pi_pipeline_new(const char *name) {
    pi_pipeline_t *pipe = (pi_pipeline_t *)calloc(1, sizeof(pi_pipeline_t));
    if (!pipe) return NULL;
    pipe->name = name ? _strdup(name) : NULL;
    pipe->state = PI_PIPELINE_STAGE_IDLE;
    return pipe;
}

void pi_pipeline_free(pi_pipeline_t *pipe) {
    if (!pipe) return;
    pi_pipeline_stage_t *s = pipe->first_stage;
    while (s) {
        pi_pipeline_stage_t *next = s->next_stage;
        free(s->name); free(s);
        s = next;
    }
    free(pipe->name); free(pipe);
}

pi_pipeline_stage_t *pi_pipeline_add_stage(pi_pipeline_t *pipe, pi_pipeline_stage_type_t stype,
                                            const char *name, pi_stage_process_fn fn, void *user) {
    if (!pipe) return NULL;
    pi_pipeline_stage_t *stage = (pi_pipeline_stage_t *)calloc(1, sizeof(pi_pipeline_stage_t));
    if (!stage) return NULL;
    stage->stage_type = stype;
    stage->name = name ? _strdup(name) : NULL;
    stage->process_fn = fn;
    stage->user_data = user;
    stage->state = PI_PIPELINE_STAGE_IDLE;
    stage->owner_pipeline = pipe;
    if (!pipe->first_stage) {
        pipe->first_stage = pipe->last_stage = stage;
    } else {
        pipe->last_stage->next_stage = stage;
        pipe->last_stage = stage;
    }
    pipe->num_stages++;
    return stage;
}

void pi_pipeline_start(pi_pipeline_t *pipe) {
    if (!pipe) return;
    pipe->state = PI_PIPELINE_STAGE_RUNNING;
    pipe->started_at = (int64_t)time(NULL);
}

void pi_pipeline_stop(pi_pipeline_t *pipe) {
    if (!pipe) return;
    pipe->state = PI_PIPELINE_STAGE_STOPPED;
}

void pi_pipeline_pause(pi_pipeline_t *pipe) {
    if (!pipe || pipe->state != PI_PIPELINE_STAGE_RUNNING) return;
    pipe->state = PI_PIPELINE_STAGE_PAUSED;
}

void pi_pipeline_resume(pi_pipeline_t *pipe) {
    if (!pipe || pipe->state != PI_PIPELINE_STAGE_PAUSED) return;
    pipe->state = PI_PIPELINE_STAGE_RUNNING;
}

int pi_pipeline_push(pi_pipeline_t *pipe, void *data) {
    if (!pipe || !data) return -1;
    if (pipe->state != PI_PIPELINE_STAGE_RUNNING) return -1;
    /* Check backpressure */
    if (pipe->is_backpressure_active) return -2;
    /* Process through all stages */
    pi_pipeline_stage_t *stage = pipe->first_stage;
    int stages_ok = 0;
    while (stage) {
        if (stage->process_fn) {
            stage->process_fn(stage, data, stage->user_data);
            stage->items_processed++;
        }
        stages_ok++;
        stage = stage->next_stage;
    }
    pipe->total_processed++;
    /* Update queue tracking */
    if (pipe->current_queue_size > 0) pipe->current_queue_size--;
    return stages_ok;
}

void pi_pipeline_set_backpressure(pi_pipeline_t *pipe, size_t max_q, double threshold) {
    if (!pipe) return;
    pipe->max_queue_size = max_q;
    pipe->backpressure_threshold = threshold;
}

pi_pipeline_stage_state_t pi_pipeline_get_state(const pi_pipeline_t *pipe) {
    return pipe ? pipe->state : PI_PIPELINE_STAGE_STOPPED;
}

/*=== L2: Integrator Context ===*/

pi_integrator_context_t *pi_integrator_context_new(const char *name) {
    pi_integrator_context_t *ctx = (pi_integrator_context_t *)calloc(1, sizeof(pi_integrator_context_t));
    if (!ctx) return NULL;
    ctx->integrator_name = name ? _strdup(name) : _strdup("pi-integrator");
    ctx->heartbeat_interval_sec = 30;
    ctx->type_registry = pi_type_mapping_registry_new();
    return ctx;
}

void pi_integrator_context_free(pi_integrator_context_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->num_pipelines; i++) pi_pipeline_free(ctx->pipelines[i]);
    free(ctx->pipelines);
    for (int i = 0; i < ctx->num_opcua_mappings; i++) pi_opcua_pi_mapping_free(ctx->opcua_mappings[i]);
    free(ctx->opcua_mappings);
    for (int i = 0; i < ctx->num_mqtt_mappings; i++) pi_mqtt_tag_mapping_free(ctx->mqtt_mappings[i]);
    free(ctx->mqtt_mappings);
    pi_type_mapping_registry_free(ctx->type_registry);
    free(ctx->integrator_name); free(ctx->pi_data_archive_server);
    free(ctx->af_server_name); free(ctx->af_database_name);
    free(ctx->status_message);
    if (ctx->opcua_session) pi_opcua_session_disconnect(ctx->opcua_session);
    if (ctx->mqtt_client) pi_mqtt_client_disconnect(ctx->mqtt_client);
    free(ctx);
}

int pi_integrator_context_add_pipeline(pi_integrator_context_t *ctx, pi_pipeline_t *pipe) {
    if (!ctx || !pipe) return -1;
    ctx->num_pipelines++;
    ctx->pipelines = (pi_pipeline_t **)realloc(ctx->pipelines, (size_t)ctx->num_pipelines * sizeof(pi_pipeline_t *));
    ctx->pipelines[ctx->num_pipelines - 1] = pipe;
    return ctx->num_pipelines - 1;
}

int pi_integrator_context_add_opcua_mapping(pi_integrator_context_t *ctx, pi_opcua_pi_mapping_t *map) {
    if (!ctx || !map) return -1;
    ctx->num_opcua_mappings++;
    ctx->opcua_mappings = (pi_opcua_pi_mapping_t **)realloc(ctx->opcua_mappings, (size_t)ctx->num_opcua_mappings * sizeof(pi_opcua_pi_mapping_t *));
    ctx->opcua_mappings[ctx->num_opcua_mappings - 1] = map;
    return ctx->num_opcua_mappings - 1;
}

int pi_integrator_context_add_mqtt_mapping(pi_integrator_context_t *ctx, pi_mqtt_tag_mapping_t *map) {
    if (!ctx || !map) return -1;
    ctx->num_mqtt_mappings++;
    ctx->mqtt_mappings = (pi_mqtt_tag_mapping_t **)realloc(ctx->mqtt_mappings, (size_t)ctx->num_mqtt_mappings * sizeof(pi_mqtt_tag_mapping_t *));
    ctx->mqtt_mappings[ctx->num_mqtt_mappings - 1] = map;
    return ctx->num_mqtt_mappings - 1;
}

void pi_integrator_start(pi_integrator_context_t *ctx) {
    if (!ctx) return;
    ctx->is_running = true;
    ctx->start_time = time(NULL);
    ctx->last_heartbeat = ctx->start_time;
    for (int i = 0; i < ctx->num_pipelines; i++) pi_pipeline_start(ctx->pipelines[i]);
}

void pi_integrator_stop(pi_integrator_context_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->num_pipelines; i++) pi_pipeline_stop(ctx->pipelines[i]);
    ctx->is_running = false;
}

void pi_integrator_heartbeat(pi_integrator_context_t *ctx) {
    if (!ctx || !ctx->is_running) return;
    ctx->last_heartbeat = time(NULL);
}

/*=== L3: Priority Queue (binary heap) ===*/

static void pq_swap(pi_priority_item_t *a, pi_priority_item_t *b) {
    pi_priority_item_t t = *a; *a = *b; *b = t;
}

static void pq_sift_up(pi_priority_queue_t *q, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        bool should_swap = q->is_min_heap ? (q->heap[idx].priority < q->heap[parent].priority)
                                          : (q->heap[idx].priority > q->heap[parent].priority);
        if (should_swap) { pq_swap(&q->heap[idx], &q->heap[parent]); idx = parent; }
        else break;
    }
}

static void pq_sift_down(pi_priority_queue_t *q, size_t idx) {
    while (1) {
        size_t left = 2 * idx + 1, right = 2 * idx + 2, best = idx;
        if (left < q->size) {
            bool better = q->is_min_heap ? (q->heap[left].priority < q->heap[best].priority)
                                         : (q->heap[left].priority > q->heap[best].priority);
            if (better) best = left;
        }
        if (right < q->size) {
            bool better = q->is_min_heap ? (q->heap[right].priority < q->heap[best].priority)
                                         : (q->heap[right].priority > q->heap[best].priority);
            if (better) best = right;
        }
        if (best != idx) { pq_swap(&q->heap[idx], &q->heap[best]); idx = best; }
        else break;
    }
}

pi_priority_queue_t *pi_priority_queue_new(size_t capacity, bool min_heap) {
    pi_priority_queue_t *q = (pi_priority_queue_t *)calloc(1, sizeof(pi_priority_queue_t));
    if (!q) return NULL;
    q->capacity = capacity > 0 ? capacity : 256;
    q->heap = (pi_priority_item_t *)calloc(q->capacity, sizeof(pi_priority_item_t));
    if (!q->heap) { free(q); return NULL; }
    q->is_min_heap = min_heap;
    return q;
}

void pi_priority_queue_free(pi_priority_queue_t *q) {
    if (!q) return;
    free(q->heap); free(q);
}

bool pi_priority_queue_push(pi_priority_queue_t *q, void *data, int64_t priority) {
    if (!q || q->size >= q->capacity) return false;
    q->heap[q->size].data = data;
    q->heap[q->size].priority = priority;
    q->heap[q->size].enqueue_time = (int64_t)time(NULL);
    q->heap[q->size].sequence_number = q->size;
    pq_sift_up(q, q->size);
    q->size++;
    return true;
}

void *pi_priority_queue_pop(pi_priority_queue_t *q) {
    if (!q || q->size == 0) return NULL;
    void *data = q->heap[0].data;
    q->heap[0] = q->heap[--q->size];
    if (q->size > 0) pq_sift_down(q, 0);
    return data;
}

void *pi_priority_queue_peek(const pi_priority_queue_t *q) {
    if (!q || q->size == 0) return NULL;
    return q->heap[0].data;
}

size_t pi_priority_queue_size(const pi_priority_queue_t *q) {
    return q ? q->size : 0;
}

/*=== L3: Ring Buffer ===*/

pi_ring_buffer_t *pi_ring_buffer_new(size_t capacity, bool overwrite) {
    pi_ring_buffer_t *rb = (pi_ring_buffer_t *)calloc(1, sizeof(pi_ring_buffer_t));
    if (!rb) return NULL;
    rb->capacity = capacity > 0 ? capacity : 4096;
    rb->buffer = (uint8_t *)calloc(rb->capacity, 1);
    if (!rb->buffer) { free(rb); return NULL; }
    rb->overwrite_on_full = overwrite;
    return rb;
}

void pi_ring_buffer_free(pi_ring_buffer_t *rb) {
    if (!rb) return;
    free(rb->buffer); free(rb);
}

bool pi_ring_buffer_write(pi_ring_buffer_t *rb, const uint8_t *data, size_t len) {
    if (!rb || !data || len == 0) return false;
    if (len > rb->capacity - rb->count) {
        if (!rb->overwrite_on_full) return false;
    }
    for (size_t i = 0; i < len; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % rb->capacity;
        if (rb->is_full) rb->tail = (rb->tail + 1) % rb->capacity;
        if (rb->count < rb->capacity) rb->count++;
        else rb->is_full = true;
    }
    return true;
}

bool pi_ring_buffer_read(pi_ring_buffer_t *rb, uint8_t *out, size_t len) {
    if (!rb || !out || len == 0 || rb->count < len) return false;
    for (size_t i = 0; i < len; i++) {
        out[i] = rb->buffer[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->count--;
        rb->is_full = false;
    }
    return true;
}

size_t pi_ring_buffer_available(const pi_ring_buffer_t *rb) {
    return rb ? rb->count : 0;
}

void pi_ring_buffer_clear(pi_ring_buffer_t *rb) {
    if (!rb) return;
    rb->head = rb->tail = rb->count = 0;
    rb->is_full = false;
}

/*=== L5: Token Bucket Rate Limiter ===*/

void pi_rate_limiter_init(pi_rate_limiter_t *rl, double rate, double burst) {
    if (!rl) return;
    rl->rate = rate;
    rl->burst_size = burst;
    rl->current_tokens = burst;
    rl->last_refill_time = (int64_t)(time(NULL) * 1000000LL);
    rl->enabled = true;
}

bool pi_rate_limiter_consume(pi_rate_limiter_t *rl, double tokens) {
    if (!rl || !rl->enabled) return true;
    /* Refill tokens based on elapsed time */
    int64_t now = (int64_t)(time(NULL) * 1000000LL);
    double elapsed_sec = (double)(now - rl->last_refill_time) / 1000000.0;
    rl->current_tokens += elapsed_sec * rl->rate;
    if (rl->current_tokens > rl->burst_size) rl->current_tokens = rl->burst_size;
    rl->last_refill_time = now;
    if (rl->current_tokens >= tokens) {
        rl->current_tokens -= tokens;
        return true; /* Allowed */
    }
    return false; /* Rate limited */
}

void pi_rate_limiter_set_rate(pi_rate_limiter_t *rl, double new_rate) {
    if (!rl) return;
    rl->rate = new_rate;
}

/*=== L5: Circuit Breaker (stability pattern) ===*/

void pi_circuit_breaker_init(pi_circuit_breaker_t *cb, const char *name,
                              uint32_t failure_threshold, uint32_t success_threshold, double timeout_ms) {
    if (!cb) return;
    memset(cb, 0, sizeof(*cb));
    cb->name = name;
    cb->failure_threshold = failure_threshold > 0 ? failure_threshold : 5;
    cb->success_threshold = success_threshold > 0 ? success_threshold : 3;
    cb->timeout_ms = timeout_ms > 0 ? timeout_ms : 30000.0;
    cb->state = PI_CB_STATE_CLOSED;
}

bool pi_circuit_breaker_allow(pi_circuit_breaker_t *cb) {
    if (!cb) return true;
    switch (cb->state) {
        case PI_CB_STATE_CLOSED: return true;
        case PI_CB_STATE_OPEN: {
            /* Check if timeout has elapsed to transition to half-open */
            int64_t now = (int64_t)(time(NULL) * 1000LL);
            if (now - cb->opened_at > (int64_t)cb->timeout_ms) {
                cb->state = PI_CB_STATE_HALF_OPEN;
                cb->success_count = 0;
                return true; /* Allow one test request */
            }
            return false;
        }
        case PI_CB_STATE_HALF_OPEN: return true;
    }
    return true;
}

void pi_circuit_breaker_success(pi_circuit_breaker_t *cb) {
    if (!cb) return;
    if (cb->state == PI_CB_STATE_HALF_OPEN) {
        cb->success_count++;
        if (cb->success_count >= cb->success_threshold) {
            cb->state = PI_CB_STATE_CLOSED;
            cb->failure_count = 0;
        }
    }
}

void pi_circuit_breaker_failure(pi_circuit_breaker_t *cb) {
    if (!cb) return;
    cb->failure_count++;
    cb->last_failure_time = (int64_t)(time(NULL) * 1000LL);
    if (cb->failure_count >= cb->failure_threshold) {
        cb->state = PI_CB_STATE_OPEN;
        cb->opened_at = cb->last_failure_time;
    }
}

pi_circuit_breaker_state_t pi_circuit_breaker_get_state(const pi_circuit_breaker_t *cb) {
    return cb ? cb->state : PI_CB_STATE_CLOSED;
}

/*=== L5: Backpressure Control ===*/

void pi_backpressure_init(pi_backpressure_ctrl_t *bp, double high_water, double low_water,
                           double reduction, uint32_t cooldown) {
    if (!bp) return;
    memset(bp, 0, sizeof(*bp));
    bp->high_watermark = high_water > 0 ? high_water : 0.8;
    bp->low_watermark = low_water > 0 ? low_water : 0.5;
    bp->reduction_factor = reduction > 0 ? reduction : 0.5;
    bp->cooldown_ms = cooldown;
}

bool pi_backpressure_should_throttle(pi_backpressure_ctrl_t *bp, double current_fill) {
    if (!bp) return false;
    bp->current_fill_pct = current_fill;
    if (current_fill >= bp->high_watermark) {
        bp->active = true;
        bp->last_backpressure_time = (int64_t)(time(NULL) * 1000LL);
        return true;
    }
    if (bp->active && current_fill <= bp->low_watermark) {
        int64_t now = (int64_t)(time(NULL) * 1000LL);
        if (now - bp->last_backpressure_time > (int64_t)bp->cooldown_ms) {
            bp->active = false;
        }
    }
    return bp->active;
}

void pi_backpressure_release(pi_backpressure_ctrl_t *bp) {
    if (!bp) return;
    bp->active = false;
}

/*=== L8: Adaptive Batch Controller (EMA-based) ===*/

void pi_adaptive_batch_init(pi_adaptive_batch_ctrl_t *abc, uint32_t min_b, uint32_t max_b,
                             uint32_t min_w, uint32_t max_w, double target_lat, double alpha) {
    if (!abc) return;
    memset(abc, 0, sizeof(*abc));
    abc->enabled = true;
    abc->min_batch_size = min_b; abc->max_batch_size = max_b;
    abc->current_batch_size = (min_b + max_b) / 2;
    abc->min_window_ms = min_w; abc->max_window_ms = max_w;
    abc->current_window_ms = (min_w + max_w) / 2;
    abc->target_latency_ms = target_lat;
    abc->ema_alpha = alpha > 0 ? alpha : 0.3;
    abc->latency_ema = target_lat;
    abc->adjustment_step = 0.1;
}

uint32_t pi_adaptive_batch_get_size(pi_adaptive_batch_ctrl_t *abc) {
    return abc ? abc->current_batch_size : 64;
}

uint32_t pi_adaptive_batch_get_window(pi_adaptive_batch_ctrl_t *abc) {
    return abc ? abc->current_window_ms : 1000;
}

/**
 * Feedback-driven batch size adaptation using EMA (Exponential Moving Average).
 * L8 Knowledge: Adaptive batching uses EMA to smooth latency and throughput
 * measurements, then adjusts batch size to maintain target latency while
 * maximizing throughput. Based on control-theoretic approach to batch sizing:
 * if latency > target, reduce batch size; if latency < target, increase.
 */
void pi_adaptive_batch_feedback(pi_adaptive_batch_ctrl_t *abc, double latency, double thr) {
    if (!abc || !abc->enabled) return;
    /* EMA update */
    abc->latency_ema = abc->ema_alpha * latency + (1.0 - abc->ema_alpha) * abc->latency_ema;
    abc->throughput_ema = abc->ema_alpha * thr + (1.0 - abc->ema_alpha) * abc->throughput_ema;
    /* Control law: adjust batch size based on latency error */
    double error = abc->latency_ema - abc->target_latency_ms;
    if (fabs(error) < abc->target_latency_ms * 0.05) return; /* Dead zone */
    double adjustment = error / abc->target_latency_ms * abc->adjustment_step;
    double new_size = (double)abc->current_batch_size * (1.0 - adjustment);
    if (new_size < (double)abc->min_batch_size) new_size = (double)abc->min_batch_size;
    if (new_size > (double)abc->max_batch_size) new_size = (double)abc->max_batch_size;
    abc->current_batch_size = (uint32_t)new_size;
    abc->current_latency_ms = latency;
    abc->throughput = thr;
}

/*=== Timer Wheel ===*/

pi_timer_wheel_t *pi_timer_wheel_new(void) {
    pi_timer_wheel_t *tw = (pi_timer_wheel_t *)calloc(1, sizeof(pi_timer_wheel_t));
    if (!tw) return NULL;
    tw->start_time_ms = (int64_t)(time(NULL) * 1000LL);
    tw->last_tick_ms = tw->start_time_ms;
    return tw;
}

void pi_timer_wheel_free(pi_timer_wheel_t *tw) {
    if (!tw) return;
    for (int i = 0; i < PI_TW_SLOTS; i++) {
        pi_timer_t *t = tw->slots[i];
        while (t) { pi_timer_t *next = t->next; free(t); t = next; }
    }
    free(tw);
}

pi_timer_t *pi_timer_wheel_add(pi_timer_wheel_t *tw, int64_t delay_ms,
                                void (*cb)(void*), void *data, bool periodic, uint32_t period_ms) {
    if (!tw || !cb) return NULL;
    pi_timer_t *timer = (pi_timer_t *)calloc(1, sizeof(pi_timer_t));
    if (!timer) return NULL;
    timer->expire_time_ms = tw->last_tick_ms + delay_ms;
    timer->callback = cb;
    timer->user_data = data;
    timer->is_periodic = periodic;
    timer->period_ms = periodic ? period_ms : 0;
    timer->is_active = true;
    int slot = (int)((timer->expire_time_ms / PI_TW_SLOT_MS) % PI_TW_SLOTS);
    timer->next = tw->slots[slot];
    tw->slots[slot] = timer;
    tw->num_active_timers++;
    return timer;
}

void pi_timer_wheel_cancel(pi_timer_wheel_t *tw, pi_timer_t *timer) {
    if (!tw || !timer) return;
    timer->is_active = false;
    tw->num_active_timers--;
}

void pi_timer_wheel_tick(pi_timer_wheel_t *tw, int64_t now_ms) {
    if (!tw) return;
    int current = tw->current_slot;
    pi_timer_t *t = tw->slots[current];
    pi_timer_t *prev = NULL;
    while (t) {
        pi_timer_t *next = t->next;
        if (t->is_active && now_ms >= t->expire_time_ms) {
            if (t->callback) t->callback(t->user_data);
            if (t->is_periodic) {
                t->expire_time_ms = now_ms + t->period_ms;
                int ns = (int)((t->expire_time_ms / PI_TW_SLOT_MS) % PI_TW_SLOTS);
                if (ns != current) {
                    /* Move to new slot */
                    if (prev) prev->next = t->next; else tw->slots[current] = t->next;
                    t->next = tw->slots[ns];
                    tw->slots[ns] = t;
                    t = next; continue;
                }
            } else {
                if (prev) prev->next = t->next; else tw->slots[current] = t->next;
                free(t);
                tw->num_active_timers--;
                t = next; continue;
            }
        }
        prev = t; t = next;
    }
    tw->current_slot = (current + 1) % PI_TW_SLOTS;
    tw->last_tick_ms = now_ms;
}

/*=== Event Bus ===*/

static struct {
    pi_event_handler_fn handlers[PI_EVENT_COUNT][16];
    int handler_counts[PI_EVENT_COUNT];
    void *handler_data[PI_EVENT_COUNT][16];
} g_event_bus;

void pi_event_bus_init(void) {
    memset(&g_event_bus, 0, sizeof(g_event_bus));
}

void pi_event_bus_subscribe(pi_event_type_t type, pi_event_handler_fn handler, void *user_data) {
    if (type >= PI_EVENT_COUNT || !handler) return;
    int idx = g_event_bus.handler_counts[type];
    if (idx >= 16) return;
    g_event_bus.handlers[type][idx] = handler;
    g_event_bus.handler_data[type][idx] = user_data;
    g_event_bus.handler_counts[type]++;
}

void pi_event_bus_unsubscribe(pi_event_type_t type, pi_event_handler_fn handler) {
    if (type >= PI_EVENT_COUNT || !handler) return;
    for (int i = 0; i < g_event_bus.handler_counts[type]; i++) {
        if (g_event_bus.handlers[type][i] == handler) {
            /* Remove by swap-with-last */
            int last = g_event_bus.handler_counts[type] - 1;
            g_event_bus.handlers[type][i] = g_event_bus.handlers[type][last];
            g_event_bus.handler_data[type][i] = g_event_bus.handler_data[type][last];
            g_event_bus.handler_counts[type]--;
            return;
        }
    }
}

void pi_event_bus_publish(const pi_event_t *event) {
    if (!event || event->type >= PI_EVENT_COUNT) return;
    for (int i = 0; i < g_event_bus.handler_counts[event->type]; i++) {
        if (g_event_bus.handlers[event->type][i])
            g_event_bus.handlers[event->type][i](event, g_event_bus.handler_data[event->type][i]);
    }
}

void pi_event_bus_dispatch(void) {
    /* In a real system, this would process queued events from a thread-safe queue */
}

/*=== Health Monitor ===*/

void pi_health_report_init(pi_health_report_t *report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));
    report->opcua_connection = PI_HEALTH_UNKNOWN;
    report->mqtt_connection = PI_HEALTH_UNKNOWN;
    report->pi_data_access = PI_HEALTH_UNKNOWN;
    report->af_server = PI_HEALTH_UNKNOWN;
    report->pipeline_health = PI_HEALTH_UNKNOWN;
    report->memory_usage = PI_HEALTH_UNKNOWN;
}

pi_health_status_t pi_health_report_overall(const pi_health_report_t *report) {
    if (!report) return PI_HEALTH_UNKNOWN;
    /* Overall is the worst of all subsystems */
    pi_health_status_t worst = PI_HEALTH_OK;
    pi_health_status_t fields[] = {
        report->opcua_connection, report->mqtt_connection, report->pi_data_access,
        report->af_server, report->pipeline_health, report->memory_usage
    };
    for (int i = 0; i < 6; i++) {
        if (fields[i] > worst) worst = fields[i];
    }
    return worst;
}

void pi_health_collect(pi_integrator_context_t *ctx, pi_health_report_t *report) {
    if (!ctx || !report) return;
    pi_health_report_init(report);
    report->opcua_connection = ctx->opcua_session && pi_opcua_session_get_state(ctx->opcua_session) == PI_OPCUA_SESSION_ACTIVATED ? PI_HEALTH_OK : PI_HEALTH_WARNING;
    report->mqtt_connection = ctx->mqtt_client && pi_mqtt_client_get_state(ctx->mqtt_client) == PI_MQTT_STATE_CONNECTED ? PI_HEALTH_OK : PI_HEALTH_WARNING;
    report->pipeline_health = ctx->is_running ? PI_HEALTH_OK : PI_HEALTH_WARNING;
    report->last_check_time = (int64_t)time(NULL);
}
