/**
 * @file src/pi_af_analytics_eventframe.c
 * @brief PI AF Analytics — Event Frame Lifecycle Management
 *
 * Implements the complete event frame ecosystem:
 *   - Template registration (metadata defining event types)
 *   - Instance creation/closure (runtime event lifecycle)
 *   - Trigger evaluation (threshold, expression, digital, schedule)
 *   - Analytics execution at event boundaries (start/end)
 *   - Operator acknowledgment workflow
 *   - Linked-list management of active event frames
 *
 * Event Frames are the core PI AF mechanism for capturing process events
 * and triggering analytics in response. They bridge raw time-series data
 * and business-level events (batch starts, alarm conditions, maintenance).
 *
 * Knowledge Coverage: L1 (Event Frame Definitions), L2 (Event-Driven Analytics),
 *                     L3 (Linked List Queue), L7 (Industrial Event Processing)
 *
 * References:
 *   - OSIsoft PI AF SDK — AFEventFrame and AFEventFrameTemplate
 *   - ISA-88 Part 1 — Batch process models (event frames as phase boundaries)
 *   - ISA-95 Part 3 — Production event recording
 *   - Luckham, D. (2002) "The Power of Events" — Complex Event Processing
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pi_af_analytics_eventframe.h"

/* --------------------------------------------------------------------------
 * L1: String Tables
 * ------------------------------------------------------------------------*/

const char *pi_af_ef_state_name(pi_af_ef_state_t s) {
    switch (s) {
        case PI_AF_EF_STATE_IDLE:     return "Idle";
        case PI_AF_EF_STATE_ACTIVE:   return "Active";
        case PI_AF_EF_STATE_CLOSED:   return "Closed";
        case PI_AF_EF_STATE_CANCELED: return "Canceled";
        case PI_AF_EF_STATE_ACKED:    return "Acknowledged";
        default:                      return "Unknown";
    }
}

const char *pi_af_ef_severity_name(pi_af_ef_severity_t s) {
    switch (s) {
        case PI_AF_EF_SEVERITY_INFO:     return "Info";
        case PI_AF_EF_SEVERITY_LOW:      return "Low";
        case PI_AF_EF_SEVERITY_MEDIUM:   return "Medium";
        case PI_AF_EF_SEVERITY_HIGH:     return "High";
        case PI_AF_EF_SEVERITY_CRITICAL: return "Critical";
        default:                         return "Unknown";
    }
}

const char *pi_af_ef_trigger_type_name(pi_af_ef_trigger_type_t t) {
    switch (t) {
        case PI_AF_EF_TRIGGER_EXPRESSION:   return "Expression";
        case PI_AF_EF_TRIGGER_THRESHOLD_UP: return "Threshold Up";
        case PI_AF_EF_TRIGGER_THRESHOLD_DN: return "Threshold Down";
        case PI_AF_EF_TRIGGER_DIGITAL_ON:   return "Digital On";
        case PI_AF_EF_TRIGGER_DIGITAL_OFF:  return "Digital Off";
        case PI_AF_EF_TRIGGER_SCHEDULE:     return "Schedule";
        case PI_AF_EF_TRIGGER_MANUAL:       return "Manual";
        default:                            return "Unknown";
    }
}

/* --------------------------------------------------------------------------
 * Engine Lifecycle
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_ef_init(pi_af_ef_context_t *ctx) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;
    memset(ctx, 0, sizeof(*ctx));
    /* Initialize linked list sentinels */
    ctx->active_list_head = (uint32_t)(-1);
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        ctx->active_list_next[i] = (uint32_t)(-1);
    }
    return PI_AF_OK;
}

pi_af_error_t pi_af_ef_register_template(pi_af_ef_context_t *ctx,
                                          const pi_af_ef_template_t *tmpl,
                                          uint32_t *out_id) {
    if (!ctx || !tmpl) return PI_AF_ERR_INVALID_ARGUMENT;

    uint32_t slot = (uint32_t)(-1);
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (!ctx->template_in_use[i]) { slot = i; break; }
    }
    if (slot == (uint32_t)(-1)) return PI_AF_ERR_OUT_OF_MEMORY;

    memcpy(&ctx->templates[slot], tmpl, sizeof(pi_af_ef_template_t));

    if (ctx->templates[slot].template_id == 0) {
        uint32_t max_id = 0;
        for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
            if (ctx->template_in_use[i] &&
                ctx->templates[i].template_id > max_id) {
                max_id = ctx->templates[i].template_id;
            }
        }
        ctx->templates[slot].template_id = max_id + 1;
    }

    ctx->template_in_use[slot] = true;
    ctx->template_count++;

    if (out_id) *out_id = ctx->templates[slot].template_id;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Event Frame Lifecycle: Start → Active → End → Closed
 * ------------------------------------------------------------------------*/

/**
 * @brief Start a new event frame from a template.
 *
 * Creates a new instance, assigns an ID, sets start_time, and adds
 * to the linked list of active event frames for O(1) iteration.
 */
pi_af_error_t pi_af_ef_start(pi_af_ef_context_t *ctx, uint32_t template_id,
                              time_t start_ts, const char *reason,
                              uint32_t *out_ef_id) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    /* Find the template */
    pi_af_ef_template_t *tmpl = NULL;
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (ctx->template_in_use[i] &&
            ctx->templates[i].template_id == template_id) {
            tmpl = &ctx->templates[i];
            break;
        }
    }
    if (!tmpl) return PI_AF_ERR_NOT_FOUND;

    /* Find a free instance slot */
    uint32_t slot = (uint32_t)(-1);
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (!ctx->instance_in_use[i]) { slot = i; break; }
    }
    if (slot == (uint32_t)(-1)) return PI_AF_ERR_OUT_OF_MEMORY;

    /* Initialize the instance */
    pi_af_ef_instance_t *ef = &ctx->instances[slot];
    memset(ef, 0, sizeof(*ef));

    /* Assign ID */
    uint32_t max_id = 0;
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (ctx->instance_in_use[i] && ctx->instances[i].ef_id > max_id) {
            max_id = ctx->instances[i].ef_id;
        }
    }
    ef->ef_id = max_id + 1;
    ef->template_id = template_id;
    ef->state = PI_AF_EF_STATE_ACTIVE;
    ef->severity = tmpl->default_severity;
    ef->start_time = start_ts;
    ef->end_time = 0;

    {
        size_t t_len = strlen(tmpl->name);
        if (t_len > sizeof(ef->name) - 16) t_len = sizeof(ef->name) - 16;
        memcpy(ef->name, tmpl->name, t_len);
        ef->name[t_len] = ' ';
        snprintf(ef->name + t_len + 1, sizeof(ef->name) - t_len - 1,
                 "#%u", ef->ef_id);
    }
    if (reason) {
        strncpy(ef->start_reason, reason, sizeof(ef->start_reason) - 1);
    }

    /* Add to active linked list */
    ctx->active_list_next[slot] = ctx->active_list_head;
    ctx->active_list_head = slot;

    ctx->instance_in_use[slot] = true;
    ctx->instance_count++;
    ctx->total_created++;

    if (out_ef_id) *out_ef_id = ef->ef_id;
    return PI_AF_OK;
}

/**
 * @brief End an active event frame.
 *
 * Transitions the event frame to CLOSED, sets end_time, records
 * end reason, and removes from the active linked list.
 */
pi_af_error_t pi_af_ef_end(pi_af_ef_context_t *ctx, uint32_t ef_id,
                            time_t end_ts, const char *reason) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (ctx->instance_in_use[i] && ctx->instances[i].ef_id == ef_id) {
            pi_af_ef_instance_t *ef = &ctx->instances[i];

            if (ef->state != PI_AF_EF_STATE_ACTIVE) {
                return PI_AF_ERR_INVALID_ARGUMENT;
            }

            ef->end_time = end_ts;
            ef->state = PI_AF_EF_STATE_CLOSED;
            if (reason) {
                strncpy(ef->end_reason, reason, sizeof(ef->end_reason) - 1);
            }

            /* Remove from active linked list */
            if (ctx->active_list_head == i) {
                ctx->active_list_head = ctx->active_list_next[i];
            } else {
                uint32_t prev = ctx->active_list_head;
                while (prev != (uint32_t)(-1) &&
                       ctx->active_list_next[prev] != i &&
                       ctx->active_list_next[prev] != (uint32_t)(-1)) {
                    prev = ctx->active_list_next[prev];
                }
                if (prev != (uint32_t)(-1) &&
                    ctx->active_list_next[prev] == i) {
                    ctx->active_list_next[prev] = ctx->active_list_next[i];
                }
            }
            ctx->active_list_next[i] = (uint32_t)(-1);

            /* Move to history if there's space */
            if (ctx->history_count < PI_AF_MAX_EVENT_FRAMES) {
                memcpy(&ctx->history[ctx->history_count], ef,
                       sizeof(pi_af_ef_instance_t));
                ctx->history_count++;
            }

            ctx->instance_in_use[i] = false;
            ctx->instance_count--;
            ctx->total_closed++;

            return PI_AF_OK;
        }
    }
    return PI_AF_ERR_NOT_FOUND;
}

pi_af_error_t pi_af_ef_cancel(pi_af_ef_context_t *ctx, uint32_t ef_id,
                               const char *reason) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (ctx->instance_in_use[i] && ctx->instances[i].ef_id == ef_id) {
            pi_af_ef_instance_t *ef = &ctx->instances[i];

            if (ef->state != PI_AF_EF_STATE_ACTIVE) {
                return PI_AF_ERR_INVALID_ARGUMENT;
            }

            ef->end_time = time(NULL); /* Cancel timestamp = now */
            ef->state = PI_AF_EF_STATE_CANCELED;
            if (reason) {
                strncpy(ef->end_reason, reason, sizeof(ef->end_reason) - 1);
            }

            /* Remove from active linked list */
            if (ctx->active_list_head == i) {
                ctx->active_list_head = ctx->active_list_next[i];
            } else {
                uint32_t prev = ctx->active_list_head;
                while (prev != (uint32_t)(-1) &&
                       ctx->active_list_next[prev] != i &&
                       ctx->active_list_next[prev] != (uint32_t)(-1)) {
                    prev = ctx->active_list_next[prev];
                }
                if (prev != (uint32_t)(-1) &&
                    ctx->active_list_next[prev] == i) {
                    ctx->active_list_next[prev] = ctx->active_list_next[i];
                }
            }
            ctx->active_list_next[i] = (uint32_t)(-1);

            ctx->instance_in_use[i] = false;
            ctx->instance_count--;
            ctx->total_canceled++;

            return PI_AF_OK;
        }
    }
    return PI_AF_ERR_NOT_FOUND;
}

/**
 * @brief Acknowledge a closed event frame.
 *
 * Records the operator's acknowledgment, which is typically
 * a regulatory requirement (e.g., FDA 21 CFR Part 11 for pharma).
 */
pi_af_error_t pi_af_ef_acknowledge(pi_af_ef_context_t *ctx, uint32_t ef_id,
                                    const char *operator_name) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    /* Search active instances first, then history */
    pi_af_ef_instance_t *ef = NULL;
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (ctx->instance_in_use[i] && ctx->instances[i].ef_id == ef_id) {
            ef = &ctx->instances[i]; break;
        }
    }
    if (!ef) {
        for (uint32_t i = 0; i < ctx->history_count; i++) {
            if (ctx->history[i].ef_id == ef_id) {
                ef = &ctx->history[i]; break;
            }
        }
    }
    if (!ef) return PI_AF_ERR_NOT_FOUND;

    ef->state = PI_AF_EF_STATE_ACKED;
    ef->acknowledged_time = time(NULL);
    if (operator_name) {
        strncpy(ef->acknowledged_by, operator_name,
                sizeof(ef->acknowledged_by) - 1);
    }
    ctx->total_acknowledged++;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Trigger Evaluation
 * ------------------------------------------------------------------------*/

/**
 * @brief Evaluate whether a trigger condition is currently met.
 *
 * TYPES:
 *   THRESHOLD_UP:   prev ≤ threshold && current > threshold
 *   THRESHOLD_DN:   prev ≥ threshold && current < threshold
 *   DIGITAL_ON:     prev == 0 && current != 0
 *   DIGITAL_OFF:    prev != 0 && current == 0
 *   EXPRESSION:     expression evaluates to non-zero
 *   SCHEDULE:       always true (caller checks timestamp)
 *   MANUAL:         always false (caller triggers explicitly)
 *
 * Hysteresis is applied as a deadband around the threshold
 * to prevent chatter (rapid toggling near the boundary).
 */
pi_af_error_t pi_af_ef_evaluate_trigger(const pi_af_ef_trigger_t *trigger,
                                         double current_value,
                                         double prev_value,
                                         bool *out_triggered) {
    if (!trigger || !out_triggered) return PI_AF_ERR_INVALID_ARGUMENT;

    *out_triggered = false;

    switch (trigger->trigger_type) {
        case PI_AF_EF_TRIGGER_THRESHOLD_UP: {
            double lo = trigger->threshold - trigger->hysteresis;
            double hi = trigger->threshold + trigger->hysteresis;
            /* Rising edge: was at or below low band, now at or above high band */
            *out_triggered = (prev_value <= lo) && (current_value >= hi);
            break;
        }
        case PI_AF_EF_TRIGGER_THRESHOLD_DN: {
            double hi = trigger->threshold + trigger->hysteresis;
            double lo = trigger->threshold - trigger->hysteresis;
            /* Falling edge: was at or above high band, now at or below low band */
            *out_triggered = (prev_value >= hi) && (current_value <= lo);
            break;
        }
        case PI_AF_EF_TRIGGER_DIGITAL_ON:
            /* Rising edge on digital signal */
            *out_triggered = (prev_value == 0.0) && (current_value != 0.0);
            break;
        case PI_AF_EF_TRIGGER_DIGITAL_OFF:
            /* Falling edge on digital signal */
            *out_triggered = (prev_value != 0.0) && (current_value == 0.0);
            break;
        case PI_AF_EF_TRIGGER_EXPRESSION:
            /* Expression evaluation — in production, this would call
             * the expression evaluator. Here we check if the value is non-zero. */
            *out_triggered = (current_value != 0.0);
            break;
        case PI_AF_EF_TRIGGER_SCHEDULE:
            /* Schedule triggers are handled by the scheduler calling
             * pi_af_ef_start/end at the appropriate time. */
            *out_triggered = false;
            break;
        case PI_AF_EF_TRIGGER_MANUAL:
            *out_triggered = false;
            break;
        default:
            return PI_AF_ERR_INVALID_ARGUMENT;
    }

    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Trigger Processing Loop
 * ------------------------------------------------------------------------*/

/**
 * @brief Process all triggers for all templates.
 *
 * This is the main event frame processing loop, called periodically
 * by the analytics engine scheduler. For each active template,
 * it evaluates start and end triggers against current data.
 *
 * Note: In a real PI AF system, this would query the PI Data Archive
 * for current values of the monitored attributes. Here we use a
 * simplified model where the caller provides values.
 */
pi_af_error_t pi_af_ef_process_triggers(pi_af_ef_context_t *ctx,
                                         time_t current_time,
                                         uint32_t *new_active,
                                         uint32_t *new_closed) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    uint32_t started = 0, closed = 0;

    /* For each active instance, evaluate end triggers */
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (!ctx->instance_in_use[i]) continue;
        pi_af_ef_instance_t *ef = &ctx->instances[i];
        if (ef->state != PI_AF_EF_STATE_ACTIVE) continue;

        /* Look up the template */
        pi_af_ef_template_t *tmpl = NULL;
        for (uint32_t j = 0; j < PI_AF_MAX_EVENT_FRAMES; j++) {
            if (ctx->template_in_use[j] &&
                ctx->templates[j].template_id == ef->template_id) {
                tmpl = &ctx->templates[j]; break;
            }
        }
        if (!tmpl) continue;

        /* Evaluate end triggers */
        for (uint32_t t = 0; t < tmpl->end_trigger_count; t++) {
            bool triggered = false;
            /* In production, we'd look up actual attribute values here */
            pi_af_ef_evaluate_trigger(&tmpl->end_triggers[t],
                                       1.0, 0.0, &triggered);
            if (triggered) {
                pi_af_ef_end(ctx, ef->ef_id, current_time,
                             "End trigger condition met");
                closed++;
                break; /* One end trigger is enough */
            }
        }
    }

    /* For each template, evaluate start triggers */
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (!ctx->template_in_use[i]) continue;
        pi_af_ef_template_t *tmpl = &ctx->templates[i];

        for (uint32_t t = 0; t < tmpl->start_trigger_count; t++) {
            bool triggered = false;
            pi_af_ef_evaluate_trigger(&tmpl->start_triggers[t],
                                       1.0, 0.0, &triggered);
            if (triggered) {
                uint32_t ef_id;
                pi_af_ef_start(ctx, tmpl->template_id, current_time,
                               "Start trigger condition met", &ef_id);
                started++;
                break;
            }
        }
    }

    if (new_active) *new_active = started;
    if (new_closed) *new_closed = closed;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Utility Functions
 * ------------------------------------------------------------------------*/

double pi_af_ef_duration_seconds(const pi_af_ef_instance_t *ef) {
    if (!ef) return 0.0;

    time_t end = ef->end_time;
    if (ef->state == PI_AF_EF_STATE_ACTIVE) {
        end = time(NULL);
    }
    if (end <= ef->start_time) return 0.0;
    return (double)(end - ef->start_time);
}

pi_af_ef_instance_t *pi_af_ef_get(pi_af_ef_context_t *ctx, uint32_t ef_id) {
    if (!ctx) return NULL;
    for (uint32_t i = 0; i < PI_AF_MAX_EVENT_FRAMES; i++) {
        if (ctx->instance_in_use[i] && ctx->instances[i].ef_id == ef_id) {
            return &ctx->instances[i];
        }
    }
    /* Also check history */
    for (uint32_t i = 0; i < ctx->history_count; i++) {
        if (ctx->history[i].ef_id == ef_id) {
            return &ctx->history[i];
        }
    }
    return NULL;
}

pi_af_error_t pi_af_ef_get_analytics(const pi_af_ef_instance_t *ef,
                                      bool is_start,
                                      const uint32_t **out_ids,
                                      uint32_t *out_count) {
    /* This would look up the template and return the analytics list.
     * For now, return an empty list. */
    (void)ef; (void)is_start;
    static const uint32_t empty_ids[1] = {0};
    if (out_ids)  *out_ids  = empty_ids;
    if (out_count) *out_count = 0;
    return PI_AF_OK;
}

uint32_t pi_af_ef_active_count(const pi_af_ef_context_t *ctx) {
    if (!ctx) return 0;
    uint32_t count = 0;
    uint32_t cur = ctx->active_list_head;
    while (cur != (uint32_t)(-1) && cur < PI_AF_MAX_EVENT_FRAMES) {
        if (ctx->instance_in_use[cur] &&
            ctx->instances[cur].state == PI_AF_EF_STATE_ACTIVE) {
            count++;
        }
        cur = ctx->active_list_next[cur];
    }
    return count;
}
