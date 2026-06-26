/**
 * @file include/pi_af_analytics_eventframe.h
 * @brief PI AF Analytics — Event Frame Management & Triggered Calculations
 *
 * Event Frames are the PI AF mechanism for capturing process events
 * (batch starts/ends, alarm conditions, maintenance windows) as
 * time-bounded entities. This module models event-frame-triggered
 * analytics — calculations that fire on event frame boundaries.
 *
 * The event frame lifecycle:
 *   1. Trigger condition detected → event frame START
 *   2. Event frame is ACTIVE (duration unknown)
 *   3. End condition detected → event frame END
 *   4. Event frame is CLOSED (immutable)
 *
 * Analytics can be configured to execute:
 *   - On event frame START (beginning of an event)
 *   - On event frame END (accumulate over the full event duration)
 *   - Periodically within the event frame window
 *   - On event frame ACKNOWLEDGMENT
 *
 * Knowledge Coverage: L1 (Event Frame Definitions), L2 (Event-Driven Execution),
 *                     L3 (Event Queue & Linked List), L7 (Industrial Event Pattern)
 *
 * Reference:
 *   - OSIsoft PI AF SDK — AFEventFrame, AFEventFrameTemplate
 *   - ISA-88 Batch Control — Event frames map to batch phases
 *   - ISA-95 Part 3 — Production event models
 *   - IEC 61512 (ISA-88 equivalent) — Batch process event lifecycle
 *
 * MIT 6.302 — Event-driven simulation, DES event lifecycle
 * Stanford ENGR205 — Industrial event processing
 * CMU 24-677 — Event queue scheduling discipline
 */

#ifndef PI_AF_ANALYTICS_EVENTFRAME_H
#define PI_AF_ANALYTICS_EVENTFRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "pi_af_analytics_core.h"

/* --------------------------------------------------------------------------
 * L1 — Definitions: Event Frame Domain Model
 * ------------------------------------------------------------------------*/

/** Maximum event frames in the system */
#define PI_AF_MAX_EVENT_FRAMES    64

/** Maximum event frame name length */
#define PI_AF_MAX_EF_NAME         256

/** Maximum trigger expressions per event frame template */
#define PI_AF_MAX_EF_TRIGGERS     8

/** Maximum severity levels */
#define PI_AF_MAX_SEVERITY        5

/**
 * @brief Event frame lifecycle states.
 *
 * Models the complete lifecycle from detection through closure.
 * Matches OSIsoft PI AF EventFrame.Status.
 */
typedef enum {
    PI_AF_EF_STATE_IDLE      = 0,  /**< Not started — waiting for trigger */
    PI_AF_EF_STATE_ACTIVE    = 1,  /**< Started but not ended — in progress */
    PI_AF_EF_STATE_CLOSED    = 2,  /**< Ended normally — immutable */
    PI_AF_EF_STATE_CANCELED  = 3,  /**< Canceled by operator or system */
    PI_AF_EF_STATE_ACKED     = 4,  /**< Acknowledged by operator */
} pi_af_ef_state_t;

/**
 * @brief Event frame trigger types.
 */
typedef enum {
    PI_AF_EF_TRIGGER_EXPRESSION   = 0,  /**< Boolean expression evaluates to true */
    PI_AF_EF_TRIGGER_THRESHOLD_UP = 1,  /**< Value crosses above threshold */
    PI_AF_EF_TRIGGER_THRESHOLD_DN = 2,  /**< Value crosses below threshold */
    PI_AF_EF_TRIGGER_DIGITAL_ON   = 3,  /**< Digital tag transitions to 1 */
    PI_AF_EF_TRIGGER_DIGITAL_OFF  = 4,  /**< Digital tag transitions to 0 */
    PI_AF_EF_TRIGGER_SCHEDULE     = 5,  /**< Time-based schedule */
    PI_AF_EF_TRIGGER_MANUAL       = 6,  /**< Operator-initiated */
} pi_af_ef_trigger_type_t;

/**
 * @brief Severity level for event frames.
 *
 * Higher severity events typically trigger more urgent notifications.
 */
typedef enum {
    PI_AF_EF_SEVERITY_INFO     = 0,  /**< Informational only */
    PI_AF_EF_SEVERITY_LOW      = 1,  /**< Minor event */
    PI_AF_EF_SEVERITY_MEDIUM   = 2,  /**< Requires attention */
    PI_AF_EF_SEVERITY_HIGH     = 3,  /**< Significant operational impact */
    PI_AF_EF_SEVERITY_CRITICAL = 4,  /**< Safety or major production impact */
} pi_af_ef_severity_t;

/**
 * @brief Trigger condition definition for an event frame template.
 */
typedef struct {
    pi_af_ef_trigger_type_t trigger_type;
    char      expression[PI_AF_MAX_EXPRESSION_LEN]; /**< For EXPRESSION type */
    char      attribute_path[PI_AF_MAX_ANALYTIC_NAME]; /**< Monitored attribute */
    double    threshold;                           /**< For THRESHOLD types */
    bool      is_start_trigger;                    /**< true = start EF, false = end EF */
    double    hysteresis;                          /**< Deadband to prevent chatter */
} pi_af_ef_trigger_t;

/**
 * @brief Event frame template (metadata definition).
 *
 * An event frame template defines the structure of event frames:
 * what triggers them, what analytics they run, what data they capture.
 */
typedef struct {
    uint32_t  template_id;
    char      name[PI_AF_MAX_EF_NAME];
    char      description[PI_AF_MAX_EF_NAME];
    pi_af_ef_severity_t default_severity;

    /* Start and end triggers */
    uint32_t  start_trigger_count;
    pi_af_ef_trigger_t start_triggers[PI_AF_MAX_EF_TRIGGERS];
    uint32_t  end_trigger_count;
    pi_af_ef_trigger_t end_triggers[PI_AF_MAX_EF_TRIGGERS];

    /* Analytics to execute at each lifecycle transition */
    uint32_t  on_start_analytic_ids[8];
    uint32_t  on_start_analytic_count;
    uint32_t  on_end_analytic_ids[8];
    uint32_t  on_end_analytic_count;

    /* Whether to create a child event frame per asset */
    bool      per_asset;
    char      asset_filter[PI_AF_MAX_ANALYTIC_NAME];
} pi_af_ef_template_t;

/**
 * @brief A single event frame instance (runtime entity).
 *
 * Created when a start trigger fires. Contains the event's
 * time boundaries, captured data, and current state.
 */
typedef struct {
    uint32_t  ef_id;                              /**< Unique event frame ID */
    uint32_t  template_id;                        /**< Which template created this */
    char      name[PI_AF_MAX_EF_NAME];            /**< Display name */
    pi_af_ef_state_t state;                       /**< Current lifecycle state */
    pi_af_ef_severity_t severity;

    time_t    start_time;                         /**< When the event began */
    time_t    end_time;                           /**< When the event ended (0 if active) */
    time_t    acknowledged_time;                  /**< When operator acknowledged */
    char      acknowledged_by[64];                /**< Operator name */

    /* Captured data at start/end (snapshot of key attributes) */
    double    start_values[8];
    uint32_t  start_value_count;
    double    end_values[8];
    uint32_t  end_value_count;
    char      start_value_names[8][32];
    char      end_value_names[8][32];

    /* Reason codes */
    char      start_reason[256];                  /**< Why this event frame started */
    char      end_reason[256];                    /**< Why it ended */

    /* Analytics that ran for this event frame */
    uint32_t  executed_analytic_ids[8];
    uint32_t  executed_analytic_count;
    double    analytic_results[8];
} pi_af_ef_instance_t;

/* --------------------------------------------------------------------------
 * L3 — Engineering Structures: Event Frame Manager
 * ------------------------------------------------------------------------*/

/**
 * @brief Event frame manager — singleton for all EF operations.
 */
typedef struct {
    /* Templates (metadata) */
    pi_af_ef_template_t templates[PI_AF_MAX_EVENT_FRAMES];
    uint32_t             template_count;
    bool                 template_in_use[PI_AF_MAX_EVENT_FRAMES];

    /* Active instances (runtime) */
    pi_af_ef_instance_t instances[PI_AF_MAX_EVENT_FRAMES];
    uint32_t             instance_count;
    bool                 instance_in_use[PI_AF_MAX_EVENT_FRAMES];

    /* Closed instance history */
    pi_af_ef_instance_t history[PI_AF_MAX_EVENT_FRAMES];
    uint32_t             history_count;

    /* Linked list of active event frames (for fast iteration) */
    uint32_t    active_list_head;  /**< Index of first active EF */
    uint32_t    active_list_next[PI_AF_MAX_EVENT_FRAMES]; /**< Next in linked list */

    /* Event frame counters */
    uint64_t    total_created;
    uint64_t    total_closed;
    uint64_t    total_canceled;
    uint64_t    total_acknowledged;
} pi_af_ef_context_t;

/* --------------------------------------------------------------------------
 * L5 — Algorithms: Event Frame Engine
 * ------------------------------------------------------------------------*/

/**
 * @brief Initialize the event frame context.
 *
 * @param ctx  Uninitialized context
 * @return     PI_AF_OK on success
 */
pi_af_error_t pi_af_ef_init(pi_af_ef_context_t *ctx);

/**
 * @brief Register an event frame template.
 *
 * @param ctx       EF context
 * @param tmpl      Template definition
 * @param out_id    Output: assigned template ID
 * @return          PI_AF_OK on success
 */
pi_af_error_t pi_af_ef_register_template(pi_af_ef_context_t *ctx,
                                          const pi_af_ef_template_t *tmpl,
                                          uint32_t *out_id);

/**
 * @brief Start a new event frame from a template.
 *
 * Creates a new event frame instance, sets start_time to now,
 * and optionally triggers start-bound analytics.
 *
 * @param ctx         EF context
 * @param template_id Template to instantiate
 * @param start_ts    Start timestamp
 * @param reason      Human-readable reason
 * @param out_ef_id   Output: new event frame ID
 * @return            PI_AF_OK on success
 *
 * Complexity: O(1)
 */
pi_af_error_t pi_af_ef_start(pi_af_ef_context_t *ctx, uint32_t template_id,
                              time_t start_ts, const char *reason,
                              uint32_t *out_ef_id);

/**
 * @brief End an active event frame.
 *
 * Sets end_time, transitions state to CLOSED, and triggers
 * end-bound analytics.
 *
 * @param ctx      EF context
 * @param ef_id    Event frame to end
 * @param end_ts   End timestamp
 * @param reason   Reason for ending
 * @return         PI_AF_OK on success
 *
 * Complexity: O(1)
 */
pi_af_error_t pi_af_ef_end(pi_af_ef_context_t *ctx, uint32_t ef_id,
                            time_t end_ts, const char *reason);

/**
 * @brief Cancel an active event frame.
 *
 * Similar to end but marks as CANCELED rather than CLOSED.
 *
 * @param ctx    EF context
 * @param ef_id  Event frame to cancel
 * @param reason Cancellation reason
 * @return       PI_AF_OK on success
 */
pi_af_error_t pi_af_ef_cancel(pi_af_ef_context_t *ctx, uint32_t ef_id,
                               const char *reason);

/**
 * @brief Acknowledge an event frame by an operator.
 *
 * Records the acknowledgment time and operator name.
 *
 * @param ctx        EF context
 * @param ef_id      Event frame to acknowledge
 * @param operator   Operator name
 * @return           PI_AF_OK on success
 */
pi_af_error_t pi_af_ef_acknowledge(pi_af_ef_context_t *ctx, uint32_t ef_id,
                                    const char *operator_name);

/**
 * @brief Get the duration of an event frame in seconds.
 *
 * For active event frames: duration = now − start_time.
 * For closed frames: duration = end_time − start_time.
 *
 * @param ef  Event frame instance
 * @return    Duration in seconds
 *
 * Complexity: O(1)
 */
double pi_af_ef_duration_seconds(const pi_af_ef_instance_t *ef);

/**
 * @brief Check if a trigger condition is currently met.
 *
 * Evaluates trigger expressions against current attribute values
 * to determine if an event frame should start or end.
 *
 * @param trigger        Trigger definition
 * @param current_value  Current value of the monitored attribute
 * @param prev_value     Previous value (for edge detection)
 * @param out_triggered  Output: true if trigger condition is met
 * @return               PI_AF_OK on success
 *
 * Complexity: O(1) for simple triggers, O(e) for expression triggers
 */
pi_af_error_t pi_af_ef_evaluate_trigger(const pi_af_ef_trigger_t *trigger,
                                         double current_value,
                                         double prev_value,
                                         bool *out_triggered);

/**
 * @brief Evaluate all triggers for all templates and create/end event frames.
 *
 * This is the main event frame processing loop — called periodically
 * by the analytics engine scheduler.
 *
 * @param ctx           EF context
 * @param current_time  Current time
 * @param new_active    Output: number of newly started event frames
 * @param new_closed    Output: number of newly ended event frames
 * @return              PI_AF_OK on success
 */
pi_af_error_t pi_af_ef_process_triggers(pi_af_ef_context_t *ctx,
                                         time_t current_time,
                                         uint32_t *new_active,
                                         uint32_t *new_closed);

/**
 * @brief Look up an event frame instance by ID.
 *
 * @param ctx    EF context
 * @param ef_id  Event frame ID
 * @return       Pointer to instance, or NULL
 *
 * Complexity: O(n) where n = instance count
 */
pi_af_ef_instance_t *pi_af_ef_get(pi_af_ef_context_t *ctx, uint32_t ef_id);

/**
 * @brief Get the list of analytics to execute for a given event frame.
 *
 * Returns the template's on_start or on_end analytic IDs based on
 * whether the event frame is starting or ending.
 *
 * @param ef            Event frame
 * @param is_start      true = start analytics, false = end analytics
 * @param out_ids       Output: array of analytic IDs
 * @param out_count     Output: number of IDs
 * @return              PI_AF_OK on success
 */
pi_af_error_t pi_af_ef_get_analytics(const pi_af_ef_instance_t *ef,
                                      bool is_start,
                                      const uint32_t **out_ids,
                                      uint32_t *out_count);

/**
 * @brief Get human-readable state name.
 *
 * Complexity: O(1)
 */
const char *pi_af_ef_state_name(pi_af_ef_state_t s);

/**
 * @brief Get human-readable severity name.
 *
 * Complexity: O(1)
 */
const char *pi_af_ef_severity_name(pi_af_ef_severity_t s);

/**
 * @brief Get human-readable trigger type name.
 *
 * Complexity: O(1)
 */
const char *pi_af_ef_trigger_type_name(pi_af_ef_trigger_type_t t);

/**
 * @brief Count active event frames.
 *
 * @param ctx  EF context
 * @return     Number of currently active event frames
 *
 * Complexity: O(1)
 */
uint32_t pi_af_ef_active_count(const pi_af_ef_context_t *ctx);

#endif /* PI_AF_ANALYTICS_EVENTFRAME_H */
