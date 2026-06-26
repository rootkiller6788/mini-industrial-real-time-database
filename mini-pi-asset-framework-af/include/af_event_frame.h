/**
 * @file af_event_frame.h
 * @brief PI Asset Framework - AFEventFrame definition and API
 *
 * AFEventFrame captures time-bounded events in the PI AF system.
 * Event frames are generated when trigger conditions on attributes
 * evaluate to true, capturing start/end times, severity, and
 * associated data for operational analysis.
 *
 * Common use cases:
 *   - Process batch start/end events
 *   - Equipment downtime tracking
 *   - Alarm/alert state changes
 *   - Production run capture
 *   - Maintenance event logging
 *
 * L1 Definitions:
 *   - AFEventFrame: Time-bounded event with metadata
 *   - AFEventSeverity: Event severity classification
 *   - AFEventStatus: Lifecycle status of an event frame
 *
 * L2 Core Concepts:
 *   - Trigger expression evaluation
 *   - Event frame lifecycle (active, closed, acknowledged)
 *   - Severity-based notification routing
 *
 * L5 Algorithms:
 *   - Trigger condition evaluation with hysteresis
 *   - Event frame capture (auto-close on end condition)
 *   - Duration and statistics computation
 */

#ifndef AF_EVENT_FRAME_H
#define AF_EVENT_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include "af_element.h"
#include "af_attribute.h"

#define AF_MAX_EF_NAME_LEN      256
#define AF_MAX_EF_DESC_LEN      2048
#define AF_MAX_EF_ATTRS         128

/* ─── L1: Event Severity ────────────────────────────────────── */
typedef enum {
    AF_EF_SEVERITY_DEBUG    = 0,
    AF_EF_SEVERITY_INFO     = 1,
    AF_EF_SEVERITY_WARNING  = 2,
    AF_EF_SEVERITY_MINOR    = 3,
    AF_EF_SEVERITY_MAJOR    = 4,
    AF_EF_SEVERITY_CRITICAL = 5
} af_ef_severity_t;

/* ─── L1: Event Frame Status ────────────────────────────────── */
typedef enum {
    AF_EF_STATUS_ACTIVE       = 0,  /**< Event is currently active */
    AF_EF_STATUS_CLOSED       = 1,  /**< End condition met, event closed */
    AF_EF_STATUS_ACKNOWLEDGED = 2,  /**< Closed and acknowledged by operator */
    AF_EF_STATUS_CANCELLED    = 3   /**< Manually cancelled */
} af_ef_status_t;

/* ─── L1: Trigger Condition ──────────────────────────────────── */
typedef enum {
    AF_TRIGGER_GT  = 0,  /**< value > threshold */
    AF_TRIGGER_GE  = 1,  /**< value >= threshold */
    AF_TRIGGER_LT  = 2,  /**< value < threshold */
    AF_TRIGGER_LE  = 3,  /**< value <= threshold */
    AF_TRIGGER_EQ  = 4,  /**< value == threshold */
    AF_TRIGGER_NEQ = 5,  /**< value != threshold */
    AF_TRIGGER_TRUE = 6  /**< boolean value is true */
} af_trigger_op_t;

typedef struct {
    af_trigger_op_t op;
    double          threshold;
    double          hysteresis;   /**< Deadband to prevent chattering */
    char            attr_name[AF_MAX_ATTR_NAME_LEN];
} af_trigger_condition_t;

/* ─── L1: AFEventFrame Structure ─────────────────────────────── */
typedef struct {
    char     id[64];
    char     name[AF_MAX_EF_NAME_LEN];
    char     description[AF_MAX_EF_DESC_LEN];

    /* Timing */
    time_t   start_time;
    time_t   end_time;          /**< 0 if still active */
    double   duration_seconds;  /**< Computed duration */

    /* Source */
    AFElement           *source_element;
    char                  trigger_attr_name[AF_MAX_ATTR_NAME_LEN];

    /* Trigger conditions */
    af_trigger_condition_t start_condition;
    af_trigger_condition_t end_condition;
    bool                   has_end_condition;

    /* Severity and Status */
    af_ef_severity_t  severity;
    af_ef_status_t    status;

    /* Snapshot of attribute values at trigger */
    char     captured_attr_names[AF_MAX_EF_ATTRS][AF_MAX_ATTR_NAME_LEN];
    af_value_t captured_attr_values[AF_MAX_EF_ATTRS];
    int      captured_count;

    /* Metadata */
    uint64_t created_time;
    char     acknowledged_by[64];
    time_t   acknowledged_time;
    char     notes[AF_MAX_EF_DESC_LEN];
} af_event_frame_t;

/* ─── Event Frame Lifecycle ──────────────────────────────────── */
af_event_frame_t* af_ef_create(const char *name);
void af_ef_destroy(af_event_frame_t *ef);
bool af_ef_set_description(af_event_frame_t *ef, const char *desc);

/* ─── Trigger Configuration ──────────────────────────────────── */
/**
 * Configure the start trigger condition for an event frame.
 * When the source attribute value evaluates the condition to true,
 * the event frame is activated (start time recorded).
 */
bool af_ef_set_start_trigger(af_event_frame_t *ef,
                               const char *attr_name,
                               af_trigger_op_t op,
                               double threshold,
                               double hysteresis);

/**
 * Configure the end trigger condition (optional).
 * When the end condition becomes true while the event frame is active,
 * the event frame is closed and duration computed.
 */
bool af_ef_set_end_trigger(af_event_frame_t *ef,
                             const char *attr_name,
                             af_trigger_op_t op,
                             double threshold,
                             double hysteresis);

/* ─── Source ─────────────────────────────────────────────────── */
bool af_ef_set_source(af_event_frame_t *ef, AFElement *element);

/* ─── Event Evaluation ───────────────────────────────────────── */
/**
 * Evaluate the trigger conditions against current attribute values.
 *
 * @param ef Event frame to evaluate
 * @return true if event frame state changed (started or closed)
 *
 * Logic:
 * - If inactive and start condition evaluates true → activate (record start_time)
 * - If active and end condition evaluates true → close (record end_time, compute duration)
 * - Hysteresis prevents rapid toggling
 */
bool af_ef_evaluate(af_event_frame_t *ef);

/**
 * Manually close an active event frame.
 * @param ef Event frame to close
 * @param end_t Close time (0 = current time)
 * @return true if closed
 */
bool af_ef_close(af_event_frame_t *ef, time_t end_t);

/**
 * Acknowledge a closed event frame.
 * @param ef Event frame
 * @param ack_by Name of acknowledging person/system
 * @return true if acknowledged
 */
bool af_ef_acknowledge(af_event_frame_t *ef, const char *ack_by);

/* ─── Severity ───────────────────────────────────────────────── */
void af_ef_set_severity(af_event_frame_t *ef, af_ef_severity_t sev);
af_ef_severity_t af_ef_get_severity(const af_event_frame_t *ef);

/* ─── Attribute Capture ──────────────────────────────────────── */
/**
 * Capture current values of specified attributes into the event frame.
 * This snapshots data for later analysis of the event.
 */
int af_ef_capture_attributes(af_event_frame_t *ef,
                               const char *attr_names[],
                               int name_count);

/**
 * Get a captured attribute value by name.
 * @return Pointer to value, or NULL if not captured
 */
const af_value_t* af_ef_get_captured(const af_event_frame_t *ef,
                                       const char *attr_name);

/* ─── Statistics ─────────────────────────────────────────────── */
/**
 * Compute event frame duration in seconds.
 * @return Duration, or -1 if end_time not set
 */
double af_ef_get_duration(const af_event_frame_t *ef);

/**
 * Get the current status string representation.
 */
const char* af_ef_status_string(af_ef_status_t status);

/**
 * Get the severity string representation.
 */
const char* af_ef_severity_string(af_ef_severity_t sev);

/**
 * Check if an event frame is currently active.
 */
bool af_ef_is_active(const af_event_frame_t *ef);

/* ─── Notes ──────────────────────────────────────────────────── */
bool af_ef_add_note(af_event_frame_t *ef, const char *note);

#endif /* AF_EVENT_FRAME_H */
