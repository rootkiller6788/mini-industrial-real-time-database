/**
 * @file notification.h
 * @brief PI Notifications - alarm and event notification delivery system
 *
 * Delivers event-based alerts via configurable rules, delivery channels,
 * and escalation policies following OSIsoft PI Notifications architecture.
 *
 * Knowledge Levels:
 *   L1 Definitions:  notif_rule_t, notif_channel_t, notif_recipient_t
 *   L2 Core Concepts: Event-driven alerting, delivery routing, escalation
 *   L3 Engineering:   Subscription-based trigger matching, channel abstraction
 *   L4 Standards:     ISA-18.2 section 10 Annunciation, IEC 62682
 *   L7 Application:   PI Notifications 2020, operator alert workflows
 *
 * MIT 6.302 - Event-triggered notification state machines
 * Stanford EE392 - Industrial alert delivery systems
 */

#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include "event_frame.h"
#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef enum {
    NOTIF_CHANNEL_EMAIL        = 0,
    NOTIF_CHANNEL_SMS          = 1,
    NOTIF_CHANNEL_VOICE        = 2,
    NOTIF_CHANNEL_PI_VISION    = 3,
    NOTIF_CHANNEL_WEBHOOK      = 4,
    NOTIF_CHANNEL_OPC_UA       = 5,
    NOTIF_CHANNEL_MODBUS_ALARM = 6,
    NOTIF_CHANNEL_CUSTOM       = 7
} notif_channel_type_t;

typedef enum {
    NOTIF_STATUS_PENDING       = 0,
    NOTIF_STATUS_SENT          = 1,
    NOTIF_STATUS_DELIVERED     = 2,
    NOTIF_STATUS_FAILED        = 3,
    NOTIF_STATUS_ACKNOWLEDGED  = 4,
    NOTIF_STATUS_EXPIRED       = 5
} notif_status_t;

typedef enum {
    NOTIF_FORMAT_PLAIN_TEXT   = 0,
    NOTIF_FORMAT_HTML         = 1,
    NOTIF_FORMAT_JSON         = 2,
    NOTIF_FORMAT_PI_VISION_URL = 3
} notif_format_t;

#define NOTIF_MAX_RECIPIENTS   16
#define NOTIF_MAX_ADDRESS_LEN  256

typedef struct {
    char           name[128];
    char           address[NOTIF_MAX_ADDRESS_LEN];
    notif_channel_type_t preferred_channel;
    int            is_group;
    int            is_on_call;
    char           on_call_schedule[256];
    int            priority;
    int            max_retries;
    int            retry_delay_s;
} notif_recipient_t;

typedef struct {
    char                  name[128];
    notif_channel_type_t  type;
    int                   enabled;
    char                  smtp_server[256];
    int                   smtp_port;
    char                  smtp_user[128];
    char                  smtp_password[128];
    int                   smtp_use_tls;
    char                  sms_gateway[256];
    char                  sms_api_key[128];
    char                  webhook_url[512];
    char                  webhook_secret[128];
    char                  pi_vision_server[256];
    int                   max_message_length;
    int                   rate_limit_per_min;
    int                   messages_sent_this_min;
    time_t                rate_window_start;
    uint64_t              total_sent;
    uint64_t              total_failed;
    uint64_t              total_acknowledged;
    time_t                last_delivery_at;
} notif_channel_t;

#define NOTIF_MAX_RULES         256
#define NOTIF_MAX_RULE_NAME     128
#define NOTIF_MAX_CONDITION_LEN 1024

typedef struct {
    char                  name[NOTIF_MAX_RULE_NAME];
    char                  description[512];
    int                   enabled;
    ef_severity_t         min_severity;
    char                  template_filter[128];
    char                  element_filter[256];
    char                  condition_expression[NOTIF_MAX_CONDITION_LEN];
    notif_channel_type_t  channel_type;
    char                  channel_name[128];
    notif_format_t        format;
    char                  message_template[512];
    notif_recipient_t     recipients[NOTIF_MAX_RECIPIENTS];
    int                   recipient_count;
    int                   max_delay_ms;
    uint64_t              total_triggered;
    uint64_t              total_sent;
    uint64_t              total_acknowledged;
    time_t                last_triggered_at;
} notif_rule_t;

#define NOTIF_MAX_INSTANCES    1024
#define NOTIF_MAX_BODY_LEN     4096

typedef struct {
    char               id[37];
    notif_rule_t      *rule;
    event_frame_t     *source_event;
    notif_recipient_t  recipient;
    notif_status_t     status;
    notif_format_t     format;
    char               subject[256];
    char               body[NOTIF_MAX_BODY_LEN];
    time_t             created_at;
    time_t             sent_at;
    time_t             delivered_at;
    time_t             acknowledged_at;
    int                retry_count;
    char               error_message[256];
} notif_instance_t;

typedef struct {
    notif_channel_t     channels[8];
    int                 channel_count;
    notif_rule_t        rules[NOTIF_MAX_RULES];
    int                 rule_count;
    notif_instance_t    instances[NOTIF_MAX_INSTANCES];
    int                 instance_count;
    int                 instance_head;
    uint64_t            total_delivered;
    uint64_t            total_failed;
    uint64_t            total_escalated;
} notif_engine_t;

int notif_engine_init(notif_engine_t *engine);
int notif_add_channel(notif_engine_t *engine, const notif_channel_t *channel);
int notif_add_rule(notif_engine_t *engine, const notif_rule_t *rule);
int notif_process_event(notif_engine_t *engine, const event_frame_t *ef);
int notif_rule_matches(const notif_rule_t *rule, const event_frame_t *ef);
int notif_format_message(const notif_rule_t *rule, const event_frame_t *ef,
                         char *buf, size_t buf_size);
int notif_deliver(notif_instance_t *instance, double success_prob);
int notif_engine_stats(const notif_engine_t *engine,
                       uint64_t *total_delivered, uint64_t *total_failed,
                       int *total_pending);

#endif
