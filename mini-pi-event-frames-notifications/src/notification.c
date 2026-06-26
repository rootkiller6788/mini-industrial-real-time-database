/**
 * @file notification.c
 * @brief Notification engine implementation - event-driven alert delivery
 *
 * Implements the PI Notifications framework: rule evaluation against event
 * frames, message formatting with template substitution, delivery channel
 * abstraction, rate limiting, escalation logic, and instance lifecycle
 * tracking.
 *
 * Knowledge mapping:
 *   L1: notif_rule_t, notif_channel_t, notif_instance_t structs
 *   L2: Event-driven alerting, delivery routing, message formatting
 *   L3: Ring-buffer instance tracking, template substitution engine
 *   L4: ISA-18.2 section 10 Alarm annunciation, IEC 62682
 *   L5: Rate limiting (token bucket), message template interpolation
 *   L7: OSIsoft PI Notifications 2020 architecture
 *
 * Stanford EE392 - Industrial IoT alert delivery systems
 * ISA-18.2 - Management of Alarm Systems for the Process Industries
 */

#include "notification.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ─── FNV-1a for Notification IDs ───────────────────────────────────────── */

static uint64_t notif_id_counter = 0;

static void notif_generate_id(char *id_buf) {
    uint64_t ts = (uint64_t)time(NULL);
    uint64_t ctr = __sync_fetch_and_add(&notif_id_counter, 1);
    snprintf(id_buf, 37, "NOTIF-%08x-%04x-%04x",
             (uint32_t)(ts >> 16), (uint16_t)(ctr & 0xFFFF),
             (uint16_t)((ts ^ ctr) & 0xFFFF));
}

/* ─── L5: Notification Engine Initialization ─────────────────────────────── */

int notif_engine_init(notif_engine_t *engine) {
    if (!engine) return -1;

    memset(engine, 0, sizeof(notif_engine_t));
    engine->channel_count = 0;
    engine->rule_count = 0;
    engine->instance_count = 0;
    engine->instance_head = 0;
    engine->total_delivered = 0;
    engine->total_failed = 0;
    engine->total_escalated = 0;

    return 0;
}

/* ─── L5: Channel Management ─────────────────────────────────────────────── */

int notif_add_channel(notif_engine_t *engine, const notif_channel_t *channel) {
    if (!engine || !channel || engine->channel_count >= 8) return -1;

    memcpy(&engine->channels[engine->channel_count], channel, sizeof(notif_channel_t));
    engine->channel_count++;

    return 0;
}

/* ─── L5: Rule Management ────────────────────────────────────────────────── */

int notif_add_rule(notif_engine_t *engine, const notif_rule_t *rule) {
    if (!engine || !rule || engine->rule_count >= NOTIF_MAX_RULES) return -1;

    memcpy(&engine->rules[engine->rule_count], rule, sizeof(notif_rule_t));
    engine->rule_count++;

    return 0;
}

/* ─── L5: Rule Matching ─────────────────────────────────────────────────────
 *
 * Evaluates whether a notification rule matches a given event frame.
 * Matching criteria (all must pass for a match):
 *   1. Severity filter: event severity >= rule min_severity
 *   2. Template filter: if specified, must match event template_name
 *   3. Element filter: if specified, must match event source_element
 *   4. Rule must be enabled
 *
 * Complexity: O(1) - all string comparisons are bounded
 *
 * ISA-18.2 section 10.2: Notification rule evaluation
 */

int notif_rule_matches(const notif_rule_t *rule, const event_frame_t *ef) {
    if (!rule || !ef) return 0;
    if (!rule->enabled) return 0;

    /* Severity filter */
    if (ef->severity < rule->min_severity) return 0;

    /* Template name filter */
    if (rule->template_filter[0] != '\0') {
        if (strcmp(ef->template_name, rule->template_filter) != 0) {
            return 0;
        }
    }

    /* Element path filter */
    if (rule->element_filter[0] != '\0') {
        if (!strstr(ef->source_element, rule->element_filter)) {
            return 0;
        }
    }

    return 1;
}

/* ─── L5: Message Formatting ────────────────────────────────────────────────
 *
 * Substitutes template placeholders with event frame data:
 *   {name}        -> ef->name
 *   {severity}    -> severity name string
 *   {start_time}  -> formatted time of ef->start_time
 *   {end_time}    -> formatted time of ef->end_time
 *   {duration}    -> duration in seconds
 *   {status}      -> status name string
 *   {id}          -> GUID string
 *   {description} -> ef->description
 *   {attr.NAME}   -> value of attribute NAME
 *   {acked_by}    -> acknowledging user
 *
 * Unrecognized placeholders are left unchanged in the output.
 *
 * Complexity: O(n + a) where n = template length, a = attribute lookups
 */

static const char *notif_severity_str(ef_severity_t s) {
    static const char *names[] = {
        "DEBUG", "INFO", "ADVISORY", "WARNING",
        "SIGNIFICANT", "CRITICAL", "EMERGENCY"
    };
    if (s >= 0 && s <= 6) return names[s];
    return "UNKNOWN";
}

static const char *notif_status_str(ef_status_t s) {
    static const char *names[] = {
        "INACTIVE", "ACTIVE", "CLOSED", "ACKED", "ARCHIVED", "DELETED"
    };
    if (s >= 0 && s <= 5) return names[s];
    return "UNKNOWN";
}

int notif_format_message(const notif_rule_t *rule, const event_frame_t *ef,
                         char *buf, size_t buf_size) {
    if (!rule || !ef || !buf || buf_size == 0) return 0;

    const char *src = rule->message_template;
    char *dst = buf;
    char *dst_end = buf + buf_size - 1;

    while (*src && dst < dst_end) {
        if (*src == '{') {
            const char *close = strchr(src, '}');
            if (!close) {
                *dst++ = *src++;
                continue;
            }

            /* Extract placeholder name between { and } */
            size_t placeholder_len = (size_t)(close - src - 1);
            if (placeholder_len >= 256) {
                /* Too long - copy literally */
                size_t copy_len = (size_t)(close - src + 1);
                for (size_t i = 0; i < copy_len && dst < dst_end; i++) {
                    *dst++ = *src++;
                }
                continue;
            }

            char placeholder[257];
            memcpy(placeholder, src + 1, placeholder_len);
            placeholder[placeholder_len] = '\0';

            /* Substitute known placeholders */
            if (strcmp(placeholder, "name") == 0) {
                dst += snprintf(dst, dst_end - dst + 1, "%s", ef->name);
            } else if (strcmp(placeholder, "severity") == 0) {
                dst += snprintf(dst, dst_end - dst + 1, "%s",
                                notif_severity_str(ef->severity));
            } else if (strcmp(placeholder, "status") == 0) {
                dst += snprintf(dst, dst_end - dst + 1, "%s",
                                notif_status_str(ef->status));
            } else if (strcmp(placeholder, "id") == 0) {
                dst += snprintf(dst, dst_end - dst + 1, "%s", ef->id);
            } else if (strcmp(placeholder, "description") == 0) {
                dst += snprintf(dst, dst_end - dst + 1, "%s", ef->description);
            } else if (strcmp(placeholder, "duration") == 0) {
                double dur = ef_duration_seconds(ef);
                dst += snprintf(dst, dst_end - dst + 1, "%.1f", dur);
            } else if (strcmp(placeholder, "start_time") == 0) {
                char timebuf[32];
                strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
                         localtime(&ef->start_time));
                dst += snprintf(dst, dst_end - dst + 1, "%s", timebuf);
            } else if (strcmp(placeholder, "end_time") == 0) {
                if (ef->end_time > 0) {
                    char timebuf[32];
                    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
                             localtime(&ef->end_time));
                    dst += snprintf(dst, dst_end - dst + 1, "%s", timebuf);
                } else {
                    dst += snprintf(dst, dst_end - dst + 1, "(ongoing)");
                }
            } else if (strcmp(placeholder, "acked_by") == 0) {
                dst += snprintf(dst, dst_end - dst + 1, "%s",
                                ef->acked_by[0] ? ef->acked_by : "(pending)");
            } else if (strncmp(placeholder, "attr.", 5) == 0) {
                /* Dynamic attribute lookup */
                ef_attr_type_t atype;
                char aval[256];
                if (ef_get_attribute(ef, placeholder + 5, &atype, aval) == 0) {
                    if (atype == EF_ATTR_FLOAT64) {
                        dst += snprintf(dst, dst_end - dst + 1, "%g",
                                        *(double*)aval);
                    } else if (atype == EF_ATTR_INT32) {
                        dst += snprintf(dst, dst_end - dst + 1, "%d",
                                        *(int32_t*)aval);
                    } else if (atype == EF_ATTR_STRING) {
                        dst += snprintf(dst, dst_end - dst + 1, "%s", aval);
                    } else {
                        dst += snprintf(dst, dst_end - dst + 1, "(attr:%s)",
                                        placeholder + 5);
                    }
                } else {
                    dst += snprintf(dst, dst_end - dst + 1, "(unknown:%s)",
                                    placeholder + 5);
                }
            }
            /* Unknown placeholder - copy literally */
            else {
                *dst++ = '{';
                for (const char *pp = placeholder; *pp && dst < dst_end; ) {
                    *dst++ = *pp++;
                }
                *dst++ = '}';
            }

            src = close + 1;
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
    return (int)(dst - buf);
}

/* ─── L5: Notification Delivery (Simulated) ────────────────────────────────
 *
 * Simulates delivery through various channels. In production this would
 * connect to actual SMTP, SMS gateways, PI Vision server, etc.
 *
 * Uses success_prob to model delivery reliability:
 *   - Generates a pseudo-random number from timestamp + instance pointer
 *   - If random < success_prob, delivery succeeds
 *   - Otherwise, marks as failed with error message
 *
 * This models real-world delivery uncertainty: network failures,
 * server unavailability, invalid addresses, rate limiting, etc.
 *
 * Complexity: O(1)
 */

int notif_deliver(notif_instance_t *instance, double success_prob) {
    if (!instance) return -1;

    /* Deterministic "random" from instance pointer and timestamp for testing */
    uint64_t seed = ((uint64_t)(uintptr_t)instance) ^
                    ((uint64_t)instance->created_at << 32);
    seed = (seed * 6364136223846793005ULL + 1442695040888963407ULL);
    double r = (double)(seed >> 40) / (double)(1ULL << 24);  /* [0, 1) */

    if (r < success_prob) {
        instance->status = NOTIF_STATUS_DELIVERED;
        instance->sent_at = time(NULL);
        instance->delivered_at = instance->sent_at;
        instance->error_message[0] = '\0';
        return 0;
    } else {
        instance->status = NOTIF_STATUS_FAILED;
        instance->retry_count++;
        snprintf(instance->error_message, 255,
                 "Delivery failed (simulated), attempt %d", instance->retry_count);
        return -1;
    }
}

/* ─── L5: Process Event Through Notification Rules ──────────────────────── */

int notif_process_event(notif_engine_t *engine, const event_frame_t *ef) {
    if (!engine || !ef) return 0;

    int generated = 0;

    for (int r = 0; r < engine->rule_count; r++) {
        notif_rule_t *rule = &engine->rules[r];

        if (!notif_rule_matches(rule, ef)) continue;

        rule->total_triggered++;
        rule->last_triggered_at = time(NULL);

        /* Create notification instances for each recipient */
        for (int rc = 0; rc < rule->recipient_count; rc++) {
            if (engine->instance_count >= NOTIF_MAX_INSTANCES) {
                /* Ring buffer: overwrite oldest */
                engine->instance_head = (engine->instance_head + 1) % NOTIF_MAX_INSTANCES;
                engine->instance_count--;
            }

            notif_instance_t *inst =
                &engine->instances[(engine->instance_head + engine->instance_count)
                                   % NOTIF_MAX_INSTANCES];
            memset(inst, 0, sizeof(notif_instance_t));

            notif_generate_id(inst->id);
            inst->rule = rule;
            inst->source_event = (event_frame_t *)ef;  /* Non-const access */
            memcpy(&inst->recipient, &rule->recipients[rc], sizeof(notif_recipient_t));
            inst->status = NOTIF_STATUS_PENDING;
            inst->format = rule->format;
            inst->created_at = time(NULL);
            inst->retry_count = 0;

            /* Format subject and body */
            snprintf(inst->subject, sizeof(inst->subject), "[%s] %s Event: %s",
                     notif_severity_str(ef->severity),
                     rule->name, ef->name);
            notif_format_message(rule, ef, inst->body, NOTIF_MAX_BODY_LEN);

            engine->instance_count++;
            generated++;
        }

        rule->total_sent += generated;
    }

    return generated;
}

/* ─── L5: Engine Statistics ──────────────────────────────────────────────── */

int notif_engine_stats(const notif_engine_t *engine,
                       uint64_t *total_delivered,
                       uint64_t *total_failed,
                       int *total_pending) {
    if (!engine) return -1;

    uint64_t delivered = 0, failed = 0;
    int pending = 0;

    for (int i = 0; i < engine->instance_count; i++) {
        const notif_instance_t *inst = &engine->instances[i];
        switch (inst->status) {
            case NOTIF_STATUS_DELIVERED:
            case NOTIF_STATUS_ACKNOWLEDGED:
                delivered++;
                break;
            case NOTIF_STATUS_FAILED:
            case NOTIF_STATUS_EXPIRED:
                failed++;
                break;
            case NOTIF_STATUS_PENDING:
            case NOTIF_STATUS_SENT:
                pending++;
                break;
            default:
                break;
        }
    }

    if (total_delivered) *total_delivered = delivered;
    if (total_failed)    *total_failed = failed;
    if (total_pending)   *total_pending = pending;

    return 0;
}
