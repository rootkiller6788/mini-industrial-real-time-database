/**
 * @file pi_integrator_core.h
 * @brief PI Integrator Core Engine — Pipeline, Scheduler, Health Monitor
 *
 * Knowledge:
 *   L1: Pipeline stage, scheduler, health check, connection pool
 *   L2: Data pipeline concept, event-driven architecture, pub/sub dispatch
 *   L3: Ring buffer, priority queue, timer wheel, thread pool
 *   L5: Backpressure control, flow rate limiting, circuit breaker pattern
 *   L7: PI Integrator for Business Analytics — enterprise data publishing
 *   L8: Adaptive batching, dynamic connection pooling
 */

#ifndef PI_INTEGRATOR_CORE_H
#define PI_INTEGRATOR_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include "pi_opcua_bridge.h"
#include "pi_mqtt_stream.h"
#include "pi_integrator_data_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/*=== L1: Pipeline Stage ===*/

typedef enum {
    PI_PIPELINE_STAGE_IDLE = 0, PI_PIPELINE_STAGE_RUNNING = 1,
    PI_PIPELINE_STAGE_PAUSED = 2, PI_PIPELINE_STAGE_ERROR = 3,
    PI_PIPELINE_STAGE_STOPPED = 4
} pi_pipeline_stage_state_t;

typedef enum {
    PI_STAGE_SOURCE_PI     = 0,  /* Read from PI Data Archive */
    PI_STAGE_TRANSFORM     = 1,  /* Apply data transformations */
    PI_STAGE_FILTER        = 2,  /* Filter by quality, value range, deadband */
    PI_STAGE_ENRICH        = 3,  /* Enrich with AF metadata, units, context */
    PI_STAGE_AGGREGATE     = 4,  /* Aggregate: avg, min, max, sum, count */
    PI_STAGE_ENCODE        = 5,  /* Encode to target format: JSON, CSV, binary */
    PI_STAGE_DELIVER_OPCUA = 6,  /* Deliver via OPC UA */
    PI_STAGE_DELIVER_MQTT  = 7,  /* Deliver via MQTT */
    PI_STAGE_DELIVER_SQL   = 8,  /* Deliver to SQL database */
    PI_STAGE_COUNT         = 9
} pi_pipeline_stage_type_t;

typedef struct pi_pipeline_stage_s pi_pipeline_stage_t;
typedef struct pi_pipeline_s pi_pipeline_t;

typedef void (*pi_stage_process_fn)(pi_pipeline_stage_t *stage, void *data, void *user_data);

struct pi_pipeline_stage_s {
    pi_pipeline_stage_type_t  stage_type;
    char                     *name;
    pi_pipeline_stage_state_t state;
    pi_stage_process_fn       process_fn;
    void                     *user_data;
    uint64_t                  items_processed;
    uint64_t                  items_failed;
    double                    avg_processing_time_us;
    pi_pipeline_stage_t      *next_stage;
    pi_pipeline_t            *owner_pipeline;
};

/*=== L1: Pipeline ===*/

struct pi_pipeline_s {
    char                 *name;
    pi_pipeline_stage_t  *first_stage;
    pi_pipeline_stage_t  *last_stage;
    int                   num_stages;
    pi_pipeline_stage_state_t state;
    bool                  is_streaming;
    uint64_t              total_processed;
    uint64_t              total_errors;
    int64_t               started_at;
    double                throughput_items_per_sec;
    /* Backpressure control (L5) */
    size_t                max_queue_size;
    size_t                current_queue_size;
    double                backpressure_threshold;
    bool                  is_backpressure_active;
};

/*=== L2: Integrator Context ===*/

typedef struct {
    char                *integrator_name;
    char                *pi_data_archive_server;
    uint32_t             pi_data_archive_port;
    char                *af_server_name;
    char                *af_database_name;
    /* OPC UA bridge */
    pi_opcua_session_t  *opcua_session;
    pi_opcua_session_config_t opcua_config;
    /* MQTT client */
    pi_mqtt_client_t    *mqtt_client;
    pi_mqtt_config_t     mqtt_config;
    /* Type mapping */
    pi_type_mapping_registry_t *type_registry;
    /* Pipelines */
    int                  num_pipelines;
    pi_pipeline_t      **pipelines;
    /* Tag mappings */
    int                  num_opcua_mappings;
    pi_opcua_pi_mapping_t **opcua_mappings;
    int                  num_mqtt_mappings;
    pi_mqtt_tag_mapping_t **mqtt_mappings;
    /* State */
    bool                 is_running;
    bool                 is_paused;
    time_t               start_time;
    time_t               last_heartbeat;
    uint32_t             heartbeat_interval_sec;
    char                *status_message;
    int                  error_count;
} pi_integrator_context_t;

/*=== L3: Priority Queue (for message scheduling) ===*/

typedef struct {
    void     *data;
    int64_t   priority;
    int64_t   enqueue_time;
    uint64_t  sequence_number;
} pi_priority_item_t;

typedef struct {
    pi_priority_item_t *heap;
    size_t               capacity;
    size_t               size;
    bool                 is_min_heap;
} pi_priority_queue_t;

/*=== L3: Ring Buffer (for streaming data) ===*/

typedef struct {
    uint8_t  *buffer;
    size_t    capacity;
    size_t    head;
    size_t    tail;
    size_t    count;
    bool      is_full;
    bool      overwrite_on_full;
} pi_ring_buffer_t;

/*=== L5: Rate Limiter (token bucket algorithm) ===*/

typedef struct {
    double    rate;              /* Tokens per second */
    double    burst_size;       /* Max burst tokens */
    double    current_tokens;   /* Current token count */
    int64_t   last_refill_time; /* Last token refill (microseconds) */
    bool      enabled;
} pi_rate_limiter_t;

/*=== L5: Circuit Breaker ===*/

typedef enum {
    PI_CB_STATE_CLOSED       = 0,  /* Normal operation */
    PI_CB_STATE_OPEN         = 1,  /* Failing, reject requests */
    PI_CB_STATE_HALF_OPEN    = 2   /* Testing if recovered */
} pi_circuit_breaker_state_t;

typedef struct {
    pi_circuit_breaker_state_t state;
    uint32_t    failure_threshold;
    uint32_t    success_threshold;
    uint32_t    failure_count;
    uint32_t    success_count;
    double      timeout_ms;
    int64_t     last_failure_time;
    int64_t     opened_at;
    const char *name;
} pi_circuit_breaker_t;

/*=== L5: Backpressure Controller ===*/

typedef struct {
    bool     active;
    double   high_watermark;    /* Start backpressure at this fill % */
    double   low_watermark;     /* Release backpressure at this fill % */
    double   current_fill_pct;
    double   reduction_factor;  /* Reduce ingestion rate by this factor */
    uint32_t cooldown_ms;
    int64_t  last_backpressure_time;
} pi_backpressure_ctrl_t;

/*=== L8: Adaptive Batch Controller ===*/

typedef struct {
    bool     enabled;
    uint32_t min_batch_size;
    uint32_t max_batch_size;
    uint32_t current_batch_size;
    uint32_t min_window_ms;
    uint32_t max_window_ms;
    uint32_t current_window_ms;
    double   target_latency_ms;
    double   current_latency_ms;
    double   throughput;
    double   adjustment_step;
    /* EMA (Exponential Moving Average) for smooth adaptation */
    double   latency_ema;
    double   throughput_ema;
    double   ema_alpha;
} pi_adaptive_batch_ctrl_t;

/*=== L3: Timer Wheel (for scheduling) ===*/

#define PI_TW_SLOTS 256
#define PI_TW_SLOT_MS 100

typedef struct pi_timer_s {
    struct pi_timer_s *next;
    int64_t  expire_time_ms;
    void    *user_data;
    void   (*callback)(void *user_data);
    bool     is_periodic;
    uint32_t period_ms;
    bool     is_active;
} pi_timer_t;

typedef struct {
    pi_timer_t *slots[PI_TW_SLOTS];
    int         current_slot;
    int64_t     start_time_ms;
    int64_t     last_tick_ms;
    uint32_t    num_active_timers;
} pi_timer_wheel_t;

/*=== L2: Event Bus (pub/sub dispatch) ===*/

typedef enum {
    PI_EVENT_TAG_VALUE_CHANGE   = 0,
    PI_EVENT_OPCUA_DATA_CHANGE  = 1,
    PI_EVENT_MQTT_MESSAGE_RECV  = 2,
    PI_EVENT_CONNECTION_LOST    = 3,
    PI_EVENT_CONNECTION_RESTORE = 4,
    PI_EVENT_PIPELINE_STARTED   = 5,
    PI_EVENT_PIPELINE_STOPPED   = 6,
    PI_EVENT_PIPELINE_ERROR     = 7,
    PI_EVENT_HEARTBEAT          = 8,
    PI_EVENT_SYSTEM_SHUTDOWN    = 9,
    PI_EVENT_COUNT              = 16
} pi_event_type_t;

typedef struct {
    pi_event_type_t type;
    int64_t         timestamp;
    char           *source;
    char           *message;
    void           *data;
    int32_t         severity;
} pi_event_t;

typedef void (*pi_event_handler_fn)(const pi_event_t *event, void *user_data);

/*=== L2: Health Monitor ===*/

typedef enum {
    PI_HEALTH_OK       = 0,
    PI_HEALTH_WARNING  = 1,
    PI_HEALTH_CRITICAL = 2,
    PI_HEALTH_UNKNOWN  = 3
} pi_health_status_t;

typedef struct {
    pi_health_status_t opcua_connection;
    pi_health_status_t mqtt_connection;
    pi_health_status_t pi_data_access;
    pi_health_status_t af_server;
    pi_health_status_t pipeline_health;
    pi_health_status_t memory_usage;
    uint64_t           messages_pending;
    double             cpu_usage_pct;
    size_t             memory_used_bytes;
    size_t             memory_limit_bytes;
    int                active_connections;
    int                failed_requests_1min;
    int64_t            last_check_time;
} pi_health_report_t;

/*=== API ===*/

/* Pipeline */
pi_pipeline_t *pi_pipeline_new(const char *name);
void pi_pipeline_free(pi_pipeline_t *pipe);
pi_pipeline_stage_t *pi_pipeline_add_stage(pi_pipeline_t *pipe, pi_pipeline_stage_type_t stype, const char *name, pi_stage_process_fn fn, void *user);
void pi_pipeline_start(pi_pipeline_t *pipe);
void pi_pipeline_stop(pi_pipeline_t *pipe);
void pi_pipeline_pause(pi_pipeline_t *pipe);
void pi_pipeline_resume(pi_pipeline_t *pipe);
int pi_pipeline_push(pi_pipeline_t *pipe, void *data);
void pi_pipeline_set_backpressure(pi_pipeline_t *pipe, size_t max_q, double threshold);
pi_pipeline_stage_state_t pi_pipeline_get_state(const pi_pipeline_t *pipe);

/* Integrator Context */
pi_integrator_context_t *pi_integrator_context_new(const char *name);
void pi_integrator_context_free(pi_integrator_context_t *ctx);
int pi_integrator_context_add_pipeline(pi_integrator_context_t *ctx, pi_pipeline_t *pipe);
int pi_integrator_context_add_opcua_mapping(pi_integrator_context_t *ctx, pi_opcua_pi_mapping_t *map);
int pi_integrator_context_add_mqtt_mapping(pi_integrator_context_t *ctx, pi_mqtt_tag_mapping_t *map);
void pi_integrator_start(pi_integrator_context_t *ctx);
void pi_integrator_stop(pi_integrator_context_t *ctx);
void pi_integrator_heartbeat(pi_integrator_context_t *ctx);

/* Priority Queue */
pi_priority_queue_t *pi_priority_queue_new(size_t capacity, bool min_heap);
void pi_priority_queue_free(pi_priority_queue_t *q);
bool pi_priority_queue_push(pi_priority_queue_t *q, void *data, int64_t priority);
void *pi_priority_queue_pop(pi_priority_queue_t *q);
void *pi_priority_queue_peek(const pi_priority_queue_t *q);
size_t pi_priority_queue_size(const pi_priority_queue_t *q);

/* Ring Buffer */
pi_ring_buffer_t *pi_ring_buffer_new(size_t capacity, bool overwrite);
void pi_ring_buffer_free(pi_ring_buffer_t *rb);
bool pi_ring_buffer_write(pi_ring_buffer_t *rb, const uint8_t *data, size_t len);
bool pi_ring_buffer_read(pi_ring_buffer_t *rb, uint8_t *out, size_t len);
size_t pi_ring_buffer_available(const pi_ring_buffer_t *rb);
void pi_ring_buffer_clear(pi_ring_buffer_t *rb);

/* Rate Limiter */
void pi_rate_limiter_init(pi_rate_limiter_t *rl, double rate, double burst);
bool pi_rate_limiter_consume(pi_rate_limiter_t *rl, double tokens);
void pi_rate_limiter_set_rate(pi_rate_limiter_t *rl, double new_rate);

/* Circuit Breaker */
void pi_circuit_breaker_init(pi_circuit_breaker_t *cb, const char *name, uint32_t failure_threshold, uint32_t success_threshold, double timeout_ms);
bool pi_circuit_breaker_allow(pi_circuit_breaker_t *cb);
void pi_circuit_breaker_success(pi_circuit_breaker_t *cb);
void pi_circuit_breaker_failure(pi_circuit_breaker_t *cb);
pi_circuit_breaker_state_t pi_circuit_breaker_get_state(const pi_circuit_breaker_t *cb);

/* Backpressure */
void pi_backpressure_init(pi_backpressure_ctrl_t *bp, double high_water, double low_water, double reduction, uint32_t cooldown);
bool pi_backpressure_should_throttle(pi_backpressure_ctrl_t *bp, double current_fill);
void pi_backpressure_release(pi_backpressure_ctrl_t *bp);

/* Adaptive Batching */
void pi_adaptive_batch_init(pi_adaptive_batch_ctrl_t *abc, uint32_t min_b, uint32_t max_b, uint32_t min_w, uint32_t max_w, double target_lat, double alpha);
uint32_t pi_adaptive_batch_get_size(pi_adaptive_batch_ctrl_t *abc);
uint32_t pi_adaptive_batch_get_window(pi_adaptive_batch_ctrl_t *abc);
void pi_adaptive_batch_feedback(pi_adaptive_batch_ctrl_t *abc, double latency, double thr);

/* Timer Wheel */
pi_timer_wheel_t *pi_timer_wheel_new(void);
void pi_timer_wheel_free(pi_timer_wheel_t *tw);
pi_timer_t *pi_timer_wheel_add(pi_timer_wheel_t *tw, int64_t delay_ms, void (*cb)(void*), void *data, bool periodic, uint32_t period_ms);
void pi_timer_wheel_cancel(pi_timer_wheel_t *tw, pi_timer_t *timer);
void pi_timer_wheel_tick(pi_timer_wheel_t *tw, int64_t now_ms);

/* Event Bus */
void pi_event_bus_init(void);
void pi_event_bus_subscribe(pi_event_type_t type, pi_event_handler_fn handler, void *user_data);
void pi_event_bus_unsubscribe(pi_event_type_t type, pi_event_handler_fn handler);
void pi_event_bus_publish(const pi_event_t *event);
void pi_event_bus_dispatch(void);

/* Health Monitor */
void pi_health_report_init(pi_health_report_t *report);
pi_health_status_t pi_health_report_overall(const pi_health_report_t *report);
void pi_health_collect(pi_integrator_context_t *ctx, pi_health_report_t *report);

#ifdef __cplusplus
}
#endif
#endif
