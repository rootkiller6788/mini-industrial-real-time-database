/**
 * @file pi_mqtt_stream.h
 * @brief PI System to MQTT Data Streaming — Core Types and API
 *
 * 知识覆盖:
 *   L1 Definitions:   MQTT Topic, QoS, Message, Broker config, Will message
 *   L2 Core Concepts: Publish-Subscribe pattern, topic wildcards, retained messages
 *   L3 Structures:    MQTT CONNECT/CONNACK/PUBLISH/SUBSCRIBE packet format
 *   L4 Standards:     MQTT 3.1.1 (OASIS) / MQTT 5.0, ISO/IEC 20922
 *   L5 Algorithms:    Time-series chunking, payload compression, store-and-forward
 *   L7 Applications:  OSIsoft PI Integrator for MQTT, Azure IoT Hub, AWS IoT Core
 *   L8 Advanced:      MQTT Sparkplug B for IT/OT convergence
 *
 * 九校课程对标:
 *   Stanford EE392: Industrial AI & IoT streaming
 *   CMU 24-677: Networked control systems data transport
 *   RWTH Aachen: Industrie 4.0 communication protocols
 *
 * @license MIT
 */

#ifndef PI_MQTT_STREAM_H
#define PI_MQTT_STREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include "pi_opcua_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * L1 — MQTT Core Definitions (MQTT 3.1.1 / MQTT 5.0 / ISO/IEC 20922)
 *===========================================================================*/

/** MQTT Quality of Service levels (MQTT 3.1.1 section 4.1) */
typedef enum {
    PI_MQTT_QOS_0 = 0,  /* At most once (fire and forget) */
    PI_MQTT_QOS_1 = 1,  /* At least once (acknowledged)  */
    PI_MQTT_QOS_2 = 2   /* Exactly once (4-step handshake) */
} pi_mqtt_qos_t;

/** MQTT protocol version */
typedef enum {
    PI_MQTT_V3_1   = 3,   /* MQTT 3.1 (deprecated) */
    PI_MQTT_V3_1_1 = 4,   /* MQTT 3.1.1 (OASIS Standard) */
    PI_MQTT_V5_0   = 5    /* MQTT 5.0 (session expiry, shared subs) */
} pi_mqtt_version_t;

/** MQTT CONNACK return codes (MQTT 3.1.1 section 3.2.2.4) */
typedef enum {
    PI_MQTT_CONN_ACCEPTED                = 0,
    PI_MQTT_CONN_REFUSED_PROTOCOL        = 1,
    PI_MQTT_CONN_REFUSED_ID_REJECTED     = 2,
    PI_MQTT_CONN_REFUSED_SERVER_UNAVAIL  = 3,
    PI_MQTT_CONN_REFUSED_BAD_USER_PASS   = 4,
    PI_MQTT_CONN_REFUSED_NOT_AUTHORIZED  = 5
} pi_mqtt_conn_return_t;

/** MQTT message (MQTT 3.1.1 section 3.3 PUBLISH) */
typedef struct {
    char        *topic;          /* Topic name (UTF-8, owned) */
    uint8_t     *payload;        /* Message payload (owned) */
    size_t       payload_len;    /* Payload length in bytes */
    pi_mqtt_qos_t qos;           /* Quality of Service */
    bool         retain;         /* Retain flag */
    bool         dup;            /* Duplicate delivery (QoS 1/2) */
    uint16_t     packet_id;      /* Packet identifier (QoS 1/2) */
    int64_t      timestamp;      /* PI source timestamp (seconds since 1970) */
    char        *content_type;   /* MIME content type (MQTT 5.0, owned) */
    char        *response_topic; /* Response topic (MQTT 5.0, owned) */
    uint32_t     message_expiry; /* Message expiry interval secs (MQTT 5.0) */
} pi_mqtt_message_t;

/** MQTT topic subscription descriptor */
typedef struct {
    char        *topic_filter;       /* Topic filter (+ / # wildcards, owned) */
    pi_mqtt_qos_t max_qos;           /* Maximum QoS for delivery */
    bool         no_local;           /* MQTT 5.0: skip own publications */
    bool         retain_as_published;/* MQTT 5.0: preserve retain flag */
    uint8_t      retain_handling;    /* MQTT 5.0: 0=send,1=new only,2=no send */
} pi_mqtt_subscription_t;

/** MQTT Will Message (MQTT 3.1.1 section 3.1.2.5) */
typedef struct {
    char        *topic;         /* Will topic (owned) */
    uint8_t     *payload;       /* Will payload (owned) */
    size_t       payload_len;
    pi_mqtt_qos_t qos;
    bool         retain;
    uint32_t     will_delay;    /* MQTT 5.0: delay before publish (seconds) */
    bool         is_set;        /* Will is configured */
} pi_mqtt_will_t;

/*===========================================================================
 * L1 — MQTT Broker Connection Configuration
 *===========================================================================*/

/** MQTT broker connection configuration */
typedef struct {
    char        *broker_host;           /* Hostname/IP (owned) */
    uint16_t     broker_port;           /* 1883=plain, 8883=TLS */
    char        *client_id;             /* Unique client ID (owned) */
    char        *username;              /* Optional username (owned) */
    char        *password;              /* Optional password (owned) */
    pi_mqtt_version_t protocol_version;
    uint16_t     keepalive_seconds;     /* Keep-alive interval seconds */
    bool         clean_session;         /* Clean session / Clean Start */
    uint32_t     session_expiry;        /* MQTT 5.0 session expiry (0=immediate) */
    uint32_t     connect_timeout_ms;
    uint32_t     ack_timeout_ms;
    bool         auto_reconnect;
    uint32_t     max_reconnect_delay_ms;
    pi_mqtt_will_t will_message;        /* Last will and testament */
    /* TLS */
    bool         use_tls;
    char        *ca_cert_path;          /* CA cert PEM (owned) */
    char        *client_cert_path;      /* Client cert (owned) */
    char        *client_key_path;       /* Private key (owned) */
    bool         verify_server;
} pi_mqtt_config_t;

/*===========================================================================
 * L1 — PI to MQTT Streaming Types
 *===========================================================================*/

/** PI tag -> MQTT topic mapping */
typedef struct {
    char        *pi_tag_name;           /* PI tag name (owned) */
    char        *mqtt_topic;            /* MQTT publish topic (owned) */
    pi_mqtt_qos_t publish_qos;          /* QoS level for this topic */
    bool         publish_retain;        /* Retain last published value */
    double       min_publish_gap_ms;    /* Minimum interval between publishes */
    double       deadband;              /* Absolute value change threshold */
    double       deadband_pct;          /* Percentage deadband (of span) */
    bool         include_quality;       /* Include quality code in payload */
    bool         include_timestamp;     /* Include PI timestamp in payload */
    bool         is_streaming;          /* Currently active for streaming */
    int64_t      last_publish_time;     /* Last publish timestamp */
    double       last_published_value;  /* Last published value */
    bool         compress_payload;      /* Enable payload compression */
    char        *encoding;              /* "json", "csv", "binary" (owned) */
} pi_mqtt_tag_mapping_t;

/** MQTT publish batch configuration */
typedef struct {
    uint32_t     max_batch_size;        /* Max messages per batch */
    uint32_t     batch_window_ms;       /* Collection window in ms */
    bool         enable_batching;       /* Enable message batching */
    size_t       max_batch_bytes;       /* Max total bytes per batch */
    bool         sort_by_timestamp;     /* Sort batched messages by time */
} pi_mqtt_batch_config_t;

/** MQTT streaming statistics */
typedef struct {
    uint64_t  total_published;
    uint64_t  total_received;
    uint64_t  total_publish_errors;
    uint64_t  total_bytes_sent;
    uint64_t  total_bytes_received;
    uint64_t  total_reconnects;
    uint64_t  total_store_forward_events;
    uint64_t  messages_queued;
    uint64_t  messages_dropped;
    double    avg_publish_latency_ms;
    double    uptime_seconds;
    uint32_t  active_mappings;
    uint32_t  active_subscriptions;
} pi_mqtt_stream_stats_t;

/*===========================================================================
 * L2 — MQTT Client Handle & State
 *===========================================================================*/

typedef struct pi_mqtt_client_s pi_mqtt_client_t;

typedef enum {
    PI_MQTT_STATE_DISCONNECTED  = 0,
    PI_MQTT_STATE_CONNECTING    = 1,
    PI_MQTT_STATE_CONNECTED     = 2,
    PI_MQTT_STATE_DISCONNECTING = 3,
    PI_MQTT_STATE_ERROR         = 99
} pi_mqtt_state_t;

/** Callback for received MQTT messages */
typedef void (*pi_mqtt_message_callback_t)(
    void                    *user_data,
    const char              *topic,
    const uint8_t           *payload,
    size_t                   payload_len,
    pi_mqtt_qos_t            qos,
    bool                     retain
);

/** Callback for connection state changes */
typedef void (*pi_mqtt_connect_callback_t)(
    void             *user_data,
    pi_mqtt_state_t   new_state,
    int               error_code,
    const char       *error_message
);

/*===========================================================================
 * L3 — MQTT Packet Types
 *===========================================================================*/

/** MQTT control packet types (MQTT 3.1.1 section 2.1) */
typedef enum {
    PI_MQTT_PKT_CONNECT     = 1,
    PI_MQTT_PKT_CONNACK     = 2,
    PI_MQTT_PKT_PUBLISH     = 3,
    PI_MQTT_PKT_PUBACK      = 4,
    PI_MQTT_PKT_PUBREC      = 5,
    PI_MQTT_PKT_PUBREL      = 6,
    PI_MQTT_PKT_PUBCOMP     = 7,
    PI_MQTT_PKT_SUBSCRIBE   = 8,
    PI_MQTT_PKT_SUBACK      = 9,
    PI_MQTT_PKT_UNSUBSCRIBE = 10,
    PI_MQTT_PKT_UNSUBACK    = 11,
    PI_MQTT_PKT_PINGREQ     = 12,
    PI_MQTT_PKT_PINGRESP    = 13,
    PI_MQTT_PKT_DISCONNECT  = 14,
    PI_MQTT_PKT_AUTH        = 15
} pi_mqtt_packet_type_t;

/*===========================================================================
 * L2 — Serialization Formats
 *===========================================================================*/

/** Serialized PI data encoding format */
typedef enum {
    PI_MQTT_ENCODING_JSON      = 0,
    PI_MQTT_ENCODING_CSV       = 1,
    PI_MQTT_ENCODING_BINARY    = 2,
    PI_MQTT_ENCODING_MSGPACK   = 3,
    PI_MQTT_ENCODING_SPARKPLUG = 4
} pi_mqtt_encoding_t;

/** JSON-serialized PI data point */
typedef struct {
    char    *json_buffer;       /* Serialized JSON (owned) */
    size_t   json_length;       /* String length */
    bool     is_valid;          /* JSON is well-formed */
} pi_mqtt_json_payload_t;

/** CSV-serialized PI data batch */
typedef struct {
    char    *csv_buffer;        /* CSV text (owned) */
    size_t   csv_length;        /* Buffer length */
    int      num_rows;          /* Number of data rows */
    int      num_columns;       /* Number of columns */
    char    *header;            /* CSV header row (owned) */
} pi_mqtt_csv_payload_t;

/*===========================================================================
 * L5 — Store-and-Forward Buffer Types
 *===========================================================================*/

/** Store-and-forward message entry */
typedef struct {
    char           *topic;          /* Original topic (owned) */
    uint8_t        *payload;        /* Original payload (owned) */
    size_t          payload_len;
    pi_mqtt_qos_t   qos;
    bool            retain;
    int64_t         queued_time;    /* When message was queued */
    int32_t         retry_count;    /* Transmission attempts */
    int64_t         last_retry_time;
    bool            is_acknowledged;/* Broker confirmed */
} pi_mqtt_store_forward_entry_t;

/** Store-and-forward buffer configuration */
typedef struct {
    size_t      max_entries;        /* Max buffered messages */
    size_t      max_total_bytes;    /* Max total buffer bytes */
    char       *storage_path;       /* Persistent path (owned, NULL=memory) */
    bool        persist_to_disk;    /* Persist buffer to disk */
    uint32_t    max_retries;        /* Max retries before drop */
    uint32_t    retry_base_delay_ms;/* Base delay (exponential backoff) */
    double      backoff_multiplier; /* Exponential multiplier */
    uint32_t    max_retry_delay_ms; /* Maximum retry delay cap */
} pi_mqtt_sf_config_t;

/*===========================================================================
 * L8 — MQTT Sparkplug B Types (IT/OT convergence, Eclipse Tahu)
 *===========================================================================*/

/** Sparkplug B data types (Eclipse Tahu specification) */
typedef enum {
    SPARKPLUG_DTYPE_UNKNOWN    = 0,
    SPARKPLUG_DTYPE_INT8       = 1,
    SPARKPLUG_DTYPE_INT16      = 2,
    SPARKPLUG_DTYPE_INT32      = 3,
    SPARKPLUG_DTYPE_INT64      = 4,
    SPARKPLUG_DTYPE_UINT8      = 5,
    SPARKPLUG_DTYPE_UINT16     = 6,
    SPARKPLUG_DTYPE_UINT32     = 7,
    SPARKPLUG_DTYPE_UINT64     = 8,
    SPARKPLUG_DTYPE_FLOAT      = 9,
    SPARKPLUG_DTYPE_DOUBLE     = 10,
    SPARKPLUG_DTYPE_BOOLEAN    = 11,
    SPARKPLUG_DTYPE_STRING     = 12,
    SPARKPLUG_DTYPE_DATETIME   = 13,
    SPARKPLUG_DTYPE_TEXT       = 14,
    SPARKPLUG_DTYPE_UUID       = 15,
    SPARKPLUG_DTYPE_DATASET    = 16,
    SPARKPLUG_DTYPE_BYTES      = 17,
    SPARKPLUG_DTYPE_FILE       = 18,
    SPARKPLUG_DTYPE_TEMPLATE   = 19
} sparkplug_data_type_t;

/** Sparkplug B metric */
typedef struct {
    char                 *name;          /* Metric name (owned) */
    sparkplug_data_type_t data_type;
    bool                  is_null;
    bool                  is_historical;
    int64_t               timestamp;
    union {
        int64_t   int_val;
        double    double_val;
        bool      bool_val;
        char     *string_val;
        struct {
            uint8_t *data;
            size_t   size;
        } bytes_val;
    } value;
    char                 *alias;         /* Metric alias (owned) */
    bool                  is_transient;
} sparkplug_metric_t;

/** Sparkplug B Edge Node descriptor */
typedef struct {
    char       *group_id;         /* Sparkplug group ID (owned) */
    char       *edge_node_id;     /* Edge node ID (owned) */
    char       *descriptor;       /* Description (owned) */
    uint64_t    birth_sequence;   /* BIRTH certificate seq */
    int         num_metrics;
    sparkplug_metric_t *metrics;  /* Array of metric defs (owned) */
    int64_t     birth_timestamp;  /* BIRTH timestamp */
    int64_t     death_timestamp;  /* DEATH timestamp (0=alive) */
    bool        is_online;
} sparkplug_edge_node_t;

/*===========================================================================
 * L8 — OPC UA PubSub over MQTT Types (IEC 62541-14)
 *===========================================================================*/

/** OPC UA PubSub MQTT transport configuration */
typedef struct {
    char       *writer_group_name;      /* WriterGroup name (owned) */
    char       *data_set_name;          /* Published DataSet name (owned) */
    char       *mqtt_topic_prefix;      /* MQTT topic prefix (owned) */
    double      publishing_interval_ms;
    bool        use_json_encoding;      /* true=UA-JSON, false=UA-Binary */
    bool        use_broker_metadata;    /* Send DataSetMetaData to broker */
    uint32_t    max_network_message_size;
    uint32_t    max_keepalive_time_ms;
    bool        enable_network_message_timestamp;
    bool        enable_data_set_message_sequence;
} pi_opcua_pubsub_mqtt_config_t;

/*===========================================================================
 * API Function Declarations — MQTT Client Lifecycle
 *===========================================================================*/

void pi_mqtt_config_init(pi_mqtt_config_t *cfg);
pi_mqtt_client_t *pi_mqtt_client_connect(const pi_mqtt_config_t *cfg);
void pi_mqtt_client_disconnect(pi_mqtt_client_t *client);
pi_mqtt_state_t pi_mqtt_client_get_state(const pi_mqtt_client_t *client);
const char *pi_mqtt_client_get_last_error(const pi_mqtt_client_t *client);
void pi_mqtt_client_set_connect_callback(pi_mqtt_client_t *client,
                                          pi_mqtt_connect_callback_t cb,
                                          void *user_data);

/* --- Publish (L2: Publish model, L4: MQTT 3.1.1 §3.3) --- */

uint16_t pi_mqtt_publish(pi_mqtt_client_t *client,
                          const char *topic,
                          const uint8_t *payload,
                          size_t payload_len,
                          pi_mqtt_qos_t qos,
                          bool retain);
uint16_t pi_mqtt_publish_pi_data_point(pi_mqtt_client_t *client,
                                        const char *topic,
                                        const pi_data_point_t *dp,
                                        pi_mqtt_qos_t qos,
                                        bool retain);

/* --- Subscribe (L2: Subscribe model, L4: MQTT 3.1.1 §3.8) --- */

uint16_t pi_mqtt_subscribe(pi_mqtt_client_t *client,
                            const char *topic_filter,
                            pi_mqtt_qos_t max_qos,
                            pi_mqtt_message_callback_t callback,
                            void *user_data);
uint16_t pi_mqtt_unsubscribe(pi_mqtt_client_t *client,
                              const char *topic_filter);
uint16_t pi_mqtt_subscribe_many(pi_mqtt_client_t *client,
                                 const pi_mqtt_subscription_t *subscriptions,
                                 size_t count,
                                 pi_mqtt_message_callback_t callback,
                                 void *user_data);

/* --- PI Tag to MQTT (L6: Canonical Problem) --- */

pi_mqtt_tag_mapping_t *pi_mqtt_map_pi_tag(pi_mqtt_client_t *client,
                                            const char *pi_tag_name,
                                            const char *mqtt_topic,
                                            pi_mqtt_qos_t publish_qos,
                                            double deadband,
                                            double deadband_pct,
                                            double min_gap_ms,
                                            const char *encoding);
void pi_mqtt_start_streaming(pi_mqtt_client_t *client,
                              pi_mqtt_tag_mapping_t *mapping);
void pi_mqtt_stop_streaming(pi_mqtt_client_t *client,
                             pi_mqtt_tag_mapping_t *mapping);
void pi_mqtt_tag_mapping_free(pi_mqtt_tag_mapping_t *mapping);

/* --- Data Serialization (L3: Encoding) --- */

pi_mqtt_json_payload_t *pi_mqtt_serialize_to_json(const pi_data_point_t *dp);
pi_mqtt_csv_payload_t *pi_mqtt_serialize_to_csv(const pi_data_point_t *dps, size_t count);
uint8_t *pi_mqtt_serialize_to_binary(const pi_data_point_t *dp, size_t *out_len);
uint8_t *pi_mqtt_serialize_sparkplug_b(const char *group_id,
                                        const char *edge_node_id,
                                        const pi_data_point_t *dp,
                                        size_t *out_len);
int pi_mqtt_deserialize_sparkplug_b(const uint8_t *payload, size_t payload_len,
                                     pi_data_point_t **dps_out, size_t *count_out);
void pi_mqtt_json_payload_free(pi_mqtt_json_payload_t *jp);
void pi_mqtt_csv_payload_free(pi_mqtt_csv_payload_t *cp);

/* --- Batching (L5: Batch-and-window algorithm) --- */

void pi_mqtt_configure_batching(pi_mqtt_client_t *client,
                                 const pi_mqtt_batch_config_t *batch_cfg);
void pi_mqtt_flush_batch(pi_mqtt_client_t *client);

/* --- Store and Forward (L5: Reliability algorithm) --- */

void pi_mqtt_store_forward_init(pi_mqtt_client_t *client,
                                 const pi_mqtt_sf_config_t *sf_config);
size_t pi_mqtt_store_forward_queue_size(const pi_mqtt_client_t *client);
int pi_mqtt_store_forward_drain(pi_mqtt_client_t *client);
void pi_mqtt_store_forward_purge(pi_mqtt_client_t *client);

/* --- Will Message (L4: MQTT 3.1.1 §3.1.2.5) --- */

void pi_mqtt_configure_will(pi_mqtt_client_t *client, const pi_mqtt_will_t *will);

/* --- Statistics & Diagnostics --- */

void pi_mqtt_get_stats(const pi_mqtt_client_t *client,
                       pi_mqtt_stream_stats_t *stats);

/* --- Utility Functions --- */

bool pi_mqtt_topic_filter_is_valid(const char *topic_filter);
bool pi_mqtt_topic_matches(const char *topic, const char *filter);
bool pi_mqtt_topic_is_valid(const char *topic);
void pi_mqtt_message_init(pi_mqtt_message_t *msg);
void pi_mqtt_message_free(pi_mqtt_message_t *msg);
pi_mqtt_message_t *pi_mqtt_message_clone(const pi_mqtt_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* PI_MQTT_STREAM_H */
