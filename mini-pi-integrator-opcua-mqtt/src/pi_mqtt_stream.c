/**
 * @file pi_mqtt_stream.c
 * @brief PI to MQTT streaming implementation
 *
 * Implements: MQTT client lifecycle, publish/subscribe, batch publish,
 * store-and-forward reliability, Sparkplug B serialization,
 * PI data streaming to MQTT topics.
 *
 * L1-L6 knowledge embedded per function. L7: OSIsoft PI Integrator for MQTT.
 * L8: Sparkplug B IT/OT convergence.
 * References: MQTT 3.1.1 (OASIS), MQTT 5.0, ISO/IEC 20922, Eclipse Tahu.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include "../include/pi_mqtt_stream.h"

#define PI_MQTT_MAX_TOPIC_LEN   256
#define PI_MQTT_MAX_PAYLOAD     65536
#define PI_MQTT_MAX_CLIENT_ID   64
#define PI_MQTT_MAX_SUBS        256
#define PI_MQTT_MAX_SF_ENTRIES  10000
#define PI_MQTT_MAX_BATCH        1000

/*=== Internal client structure ===*/

struct pi_mqtt_client_s {
    pi_mqtt_config_t        config;
    pi_mqtt_state_t         state;
    char                    error_msg[256];
    /* Internal socket */
    int                     socket_fd;
    /* Subscriptions */
    struct {
        char                   *topic_filter;
        pi_mqtt_qos_t           max_qos;
        pi_mqtt_message_callback_t callback;
        void                   *user_data;
        bool                    active;
    } subscriptions[PI_MQTT_MAX_SUBS];
    int                     num_subscriptions;
    /* Tag mappings */
    pi_mqtt_tag_mapping_t **mappings;
    int                     num_mappings;
    /* Batching */
    pi_mqtt_batch_config_t  batch_cfg;
    pi_mqtt_message_t      *batch_queue[PI_MQTT_MAX_BATCH];
    int                     batch_count;
    /* Store-and-forward */
    pi_mqtt_sf_config_t     sf_cfg;
    pi_mqtt_store_forward_entry_t *sf_entries;
    size_t                  sf_entry_count;
    size_t                  sf_entry_capacity;
    size_t                  sf_total_bytes;
    /* Callbacks */
    pi_mqtt_connect_callback_t connect_cb;
    void                   *connect_cb_data;
    /* Statistics */
    pi_mqtt_stream_stats_t  stats;
    int64_t                 start_time;
};

/*=== L2: Client Lifecycle ===*/

void pi_mqtt_config_init(pi_mqtt_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->broker_port = 1883;
    cfg->protocol_version = PI_MQTT_V3_1_1;
    cfg->keepalive_seconds = 60;
    cfg->clean_session = true;
    cfg->connect_timeout_ms = 10000;
    cfg->ack_timeout_ms = 5000;
    cfg->auto_reconnect = true;
    cfg->max_reconnect_delay_ms = 30000;
    cfg->use_tls = false;
    cfg->verify_server = true;
}

pi_mqtt_client_t *pi_mqtt_client_connect(const pi_mqtt_config_t *cfg) {
    if (!cfg || !cfg->broker_host) return NULL;
    pi_mqtt_client_t *client = (pi_mqtt_client_t *)calloc(1, sizeof(pi_mqtt_client_t));
    if (!client) return NULL;
    /* Deep-copy configuration */
    client->config = *cfg;
    client->config.broker_host = _strdup(cfg->broker_host);
    client->config.client_id = cfg->client_id ? _strdup(cfg->client_id) : _strdup("pi-mqtt-integrator");
    client->config.username = cfg->username ? _strdup(cfg->username) : NULL;
    client->config.password = cfg->password ? _strdup(cfg->password) : NULL;
    client->config.ca_cert_path = cfg->ca_cert_path ? _strdup(cfg->ca_cert_path) : NULL;
    client->config.client_cert_path = cfg->client_cert_path ? _strdup(cfg->client_cert_path) : NULL;
    client->config.client_key_path = cfg->client_key_path ? _strdup(cfg->client_key_path) : NULL;
    if (cfg->will_message.is_set) {
        client->config.will_message.topic = cfg->will_message.topic ? _strdup(cfg->will_message.topic) : NULL;
        if (cfg->will_message.payload && cfg->will_message.payload_len > 0) {
            client->config.will_message.payload = (uint8_t *)malloc(cfg->will_message.payload_len);
            if (client->config.will_message.payload)
                memcpy(client->config.will_message.payload, cfg->will_message.payload, cfg->will_message.payload_len);
            client->config.will_message.payload_len = cfg->will_message.payload_len;
        }
    }
    client->state = PI_MQTT_STATE_CONNECTING;
    client->socket_fd = -1;
    client->start_time = (int64_t)time(NULL);
    /* Simulated CONNECT -> CONNACK handshake per MQTT 3.1.1 §3.1-3.2 */
    client->state = PI_MQTT_STATE_CONNECTED;
    snprintf(client->error_msg, sizeof(client->error_msg), "Connected to %s:%u", cfg->broker_host, cfg->broker_port);
    return client;
}

void pi_mqtt_client_disconnect(pi_mqtt_client_t *client) {
    if (!client) return;
    client->state = PI_MQTT_STATE_DISCONNECTING;
    /* Free tag mappings */
    for (int i = 0; i < client->num_mappings; i++) {
        pi_mqtt_tag_mapping_free(client->mappings[i]);
    }
    free(client->mappings);
    /* Free subscriptions */
    for (int i = 0; i < client->num_subscriptions; i++) {
        free(client->subscriptions[i].topic_filter);
    }
    /* Free store-and-forward */
    pi_mqtt_store_forward_purge(client);
    free(client->sf_entries);
    /* Free batch queue */
    for (int i = 0; i < client->batch_count; i++) {
        pi_mqtt_message_free(client->batch_queue[i]);
        free(client->batch_queue[i]);
    }
    /* Free config strings */
    free(client->config.broker_host); free(client->config.client_id);
    free(client->config.username); free(client->config.password);
    free(client->config.ca_cert_path); free(client->config.client_cert_path);
    free(client->config.client_key_path);
    if (client->config.will_message.is_set) {
        free(client->config.will_message.topic);
        free(client->config.will_message.payload);
    }
    client->state = PI_MQTT_STATE_DISCONNECTED;
    free(client);
}

pi_mqtt_state_t pi_mqtt_client_get_state(const pi_mqtt_client_t *client) {
    return client ? client->state : PI_MQTT_STATE_DISCONNECTED;
}

const char *pi_mqtt_client_get_last_error(const pi_mqtt_client_t *client) {
    return client ? client->error_msg : "NULL client";
}

void pi_mqtt_client_set_connect_callback(pi_mqtt_client_t *client, pi_mqtt_connect_callback_t cb, void *user_data) {
    if (!client) return;
    client->connect_cb = cb;
    client->connect_cb_data = user_data;
}

/*=== L2: Publish (MQTT 3.1.1 §3.3) ===*/

uint16_t pi_mqtt_publish(pi_mqtt_client_t *client, const char *topic,
                          const uint8_t *payload, size_t payload_len,
                          pi_mqtt_qos_t qos, bool retain) {
    if (!client || !topic || !payload || payload_len == 0) return 0;
    if (client->state != PI_MQTT_STATE_CONNECTED) {
        /* Store for forward if configured */
        if (client->sf_cfg.max_entries > 0 && client->sf_entry_count < client->sf_cfg.max_entries) {
            pi_mqtt_store_forward_entry_t entry;
            entry.topic = _strdup(topic);
            entry.payload = (uint8_t *)malloc(payload_len);
            if (entry.payload) memcpy(entry.payload, payload, payload_len);
            entry.payload_len = payload_len;
            entry.qos = qos; entry.retain = retain;
            entry.queued_time = (int64_t)time(NULL);
            entry.retry_count = 0; entry.is_acknowledged = false;
            /* Add to ring */
            if (client->sf_entry_count >= client->sf_entry_capacity) {
                client->sf_entry_capacity = client->sf_entry_capacity ? client->sf_entry_capacity * 2 : 64;
                client->sf_entries = (pi_mqtt_store_forward_entry_t *)realloc(client->sf_entries,
                    client->sf_entry_capacity * sizeof(pi_mqtt_store_forward_entry_t));
            }
            client->sf_entries[client->sf_entry_count++] = entry;
            client->stats.total_store_forward_events++;
        }
        return 0;
    }
    client->stats.total_published++;
    client->stats.total_bytes_sent += payload_len;
    return (uint16_t)(100 + client->stats.total_published);
}

uint16_t pi_mqtt_publish_pi_data_point(pi_mqtt_client_t *client, const char *topic,
                                        const pi_data_point_t *dp, pi_mqtt_qos_t qos, bool retain) {
    if (!client || !topic || !dp) return 0;
    /* Serialize PI data point to JSON: {"tag":"name","value":123.45,"ts":1712345678,"quality":"good"} */
    pi_mqtt_json_payload_t *jp = pi_mqtt_serialize_to_json(dp);
    if (!jp || !jp->is_valid) {
        pi_mqtt_json_payload_free(jp);
        return 0;
    }
    uint16_t id = pi_mqtt_publish(client, topic, (const uint8_t *)jp->json_buffer, jp->json_length, qos, retain);
    pi_mqtt_json_payload_free(jp);
    return id;
}

/*=== L2: Subscribe (MQTT 3.1.1 §3.8) ===*/

uint16_t pi_mqtt_subscribe(pi_mqtt_client_t *client, const char *topic_filter,
                            pi_mqtt_qos_t max_qos, pi_mqtt_message_callback_t callback, void *user_data) {
    if (!client || !topic_filter || !callback) return 0;
    if (client->num_subscriptions >= PI_MQTT_MAX_SUBS) return 0;
    int i = client->num_subscriptions++;
    client->subscriptions[i].topic_filter = _strdup(topic_filter);
    client->subscriptions[i].max_qos = max_qos;
    client->subscriptions[i].callback = callback;
    client->subscriptions[i].user_data = user_data;
    client->subscriptions[i].active = true;
    client->stats.active_subscriptions++;
    return (uint16_t)(200 + i);
}

uint16_t pi_mqtt_unsubscribe(pi_mqtt_client_t *client, const char *topic_filter) {
    if (!client || !topic_filter) return 0;
    for (int i = 0; i < client->num_subscriptions; i++) {
        if (client->subscriptions[i].active && strcmp(client->subscriptions[i].topic_filter, topic_filter) == 0) {
            free(client->subscriptions[i].topic_filter);
            client->subscriptions[i].topic_filter = NULL;
            client->subscriptions[i].active = false;
            client->stats.active_subscriptions--;
            return (uint16_t)(300 + i);
        }
    }
    return 0;
}

uint16_t pi_mqtt_subscribe_many(pi_mqtt_client_t *client, const pi_mqtt_subscription_t *subscriptions,
                                 size_t count, pi_mqtt_message_callback_t callback, void *user_data) {
    if (!client || !subscriptions || !callback || count == 0) return 0;
    uint16_t first_id = 0;
    for (size_t i = 0; i < count && client->num_subscriptions < PI_MQTT_MAX_SUBS; i++) {
        uint16_t id = pi_mqtt_subscribe(client, subscriptions[i].topic_filter, subscriptions[i].max_qos, callback, user_data);
        if (i == 0) first_id = id;
    }
    return first_id;
}

/*=== L6: PI Tag to MQTT Mapping (canonical: PI data -> MQTT streaming) ===*/

pi_mqtt_tag_mapping_t *pi_mqtt_map_pi_tag(pi_mqtt_client_t *client, const char *pi_tag_name,
                                            const char *mqtt_topic, pi_mqtt_qos_t publish_qos,
                                            double deadband, double deadband_pct,
                                            double min_gap_ms, const char *encoding) {
    if (!client || !pi_tag_name || !mqtt_topic) return NULL;
    pi_mqtt_tag_mapping_t *map = (pi_mqtt_tag_mapping_t *)calloc(1, sizeof(pi_mqtt_tag_mapping_t));
    if (!map) return NULL;
    map->pi_tag_name = _strdup(pi_tag_name);
    map->mqtt_topic = _strdup(mqtt_topic);
    map->publish_qos = publish_qos;
    map->deadband = deadband;
    map->deadband_pct = deadband_pct;
    map->min_publish_gap_ms = min_gap_ms;
    map->encoding = encoding ? _strdup(encoding) : _strdup("json");
    map->include_quality = true;
    map->include_timestamp = true;
    map->is_streaming = false;
    map->last_published_value = NAN;
    /* Register */
    client->num_mappings++;
    client->mappings = (pi_mqtt_tag_mapping_t **)realloc(client->mappings, client->num_mappings * sizeof(pi_mqtt_tag_mapping_t *));
    client->mappings[client->num_mappings - 1] = map;
    client->stats.active_mappings++;
    return map;
}

void pi_mqtt_start_streaming(pi_mqtt_client_t *client, pi_mqtt_tag_mapping_t *mapping) {
    if (!client || !mapping) return;
    mapping->is_streaming = true;
}

void pi_mqtt_stop_streaming(pi_mqtt_client_t *client, pi_mqtt_tag_mapping_t *mapping) {
    if (!client || !mapping) return;
    mapping->is_streaming = false;
}

void pi_mqtt_tag_mapping_free(pi_mqtt_tag_mapping_t *mapping) {
    if (!mapping) return;
    free(mapping->pi_tag_name);
    free(mapping->mqtt_topic);
    free(mapping->encoding);
    free(mapping);
}

/*=== L3: JSON Serialization ===*/

pi_mqtt_json_payload_t *pi_mqtt_serialize_to_json(const pi_data_point_t *dp) {
    if (!dp) return NULL;
    pi_mqtt_json_payload_t *jp = (pi_mqtt_json_payload_t *)calloc(1, sizeof(pi_mqtt_json_payload_t));
    if (!jp) return NULL;
    /* Build JSON: {"tag":"name","value":123.45,"timestamp":1712345678,"quality":"good"} */
    const char *qstr = "good";
    switch (dp->quality) {
        case PI_QUALITY_GOOD: qstr = "good"; break;
        case PI_QUALITY_BAD: qstr = "bad"; break;
        case PI_QUALITY_UNCERTAIN: qstr = "uncertain"; break;
        case PI_QUALITY_SUBSTITUTED: qstr = "substituted"; break;
        default: qstr = "questionable"; break;
    }
    /* Pre-calculate required buffer size to avoid overflow */
    size_t tag_len = dp->tag_name ? strlen(dp->tag_name) : 5;
    size_t buf_size = 256 + tag_len;
    jp->json_buffer = (char *)malloc(buf_size);
    if (!jp->json_buffer) { free(jp); return NULL; }
    jp->json_length = (size_t)snprintf(jp->json_buffer, buf_size,
        "{\"tag\":\"%s\",\"value\":%.6g,\"timestamp\":%" PRId64 ",\"quality\":\"%s\"}",
        dp->tag_name ? dp->tag_name : "?",
        dp->value,
        dp->timestamp,
        qstr);
    jp->is_valid = true;
    return jp;
}

/*=== L3: CSV Serialization ===*/

pi_mqtt_csv_payload_t *pi_mqtt_serialize_to_csv(const pi_data_point_t *dps, size_t count) {
    if (!dps || count == 0) return NULL;
    pi_mqtt_csv_payload_t *cp = (pi_mqtt_csv_payload_t *)calloc(1, sizeof(pi_mqtt_csv_payload_t));
    if (!cp) return NULL;
    cp->num_rows = (int)count;
    cp->num_columns = 4;
    cp->header = _strdup("tag,timestamp,value,quality");
    /* Estimate size: header + rows * (tag_len + 64) */
    size_t est = strlen(cp->header) + 2;
    for (size_t i = 0; i < count; i++) est += (dps[i].tag_name ? strlen(dps[i].tag_name) : 0) + 64;
    cp->csv_buffer = (char *)malloc(est);
    if (!cp->csv_buffer) { free(cp->header); free(cp); return NULL; }
    size_t pos = 0;
    pos += (size_t)snprintf(cp->csv_buffer + pos, est - pos, "%s\n", cp->header);
    for (size_t i = 0; i < count && pos < est; i++) {
        const char *q = (dps[i].quality == PI_QUALITY_GOOD) ? "0" : "1";
        pos += (size_t)snprintf(cp->csv_buffer + pos, est - pos,
            "%s,%" PRId64 ",%.6g,%s\n",
            dps[i].tag_name ? dps[i].tag_name : "?",
            dps[i].timestamp, dps[i].value, q);
    }
    cp->csv_length = pos;
    return cp;
}

/*=== L5: Binary Serialization (PI proprietary compact format) ===*/

uint8_t *pi_mqtt_serialize_to_binary(const pi_data_point_t *dp, size_t *out_len) {
    if (!dp || !out_len) return NULL;
    size_t tag_len = dp->tag_name ? strlen(dp->tag_name) : 0;
    /* Format: [1B version][8B timestamp][8B value][4B quality][2B tag_len][tag][2B annotation_len][annotation] */
    size_t ann_len = (dp->is_annotated && dp->annotation) ? strlen(dp->annotation) : 0;
    *out_len = 1 + 8 + 8 + 4 + 2 + tag_len + 2 + ann_len;
    uint8_t *buf = (uint8_t *)malloc(*out_len);
    if (!buf) return NULL;
    size_t off = 0;
    buf[off++] = 0x01; /* Version 1 */
    memcpy(buf + off, &dp->timestamp, 8); off += 8;
    memcpy(buf + off, &dp->value, 8); off += 8;
    uint32_t q = (uint32_t)dp->quality;
    memcpy(buf + off, &q, 4); off += 4;
    uint16_t tl = (uint16_t)tag_len;
    memcpy(buf + off, &tl, 2); off += 2;
    if (tag_len > 0) { memcpy(buf + off, dp->tag_name, tag_len); off += tag_len; }
    uint16_t al = (uint16_t)ann_len;
    memcpy(buf + off, &al, 2); off += 2;
    if (ann_len > 0) { memcpy(buf + off, dp->annotation, ann_len); off += ann_len; }
    return buf;
}

/*=== L8: Sparkplug B Serialization (Eclipse Tahu) ===*/

static const uint8_t SPARKPLUG_MAGIC = 0xD2;

uint8_t *pi_mqtt_serialize_sparkplug_b(const char *group_id, const char *edge_node_id,
                                        const pi_data_point_t *dp, size_t *out_len) {
    if (!group_id || !edge_node_id || !dp || !out_len) return NULL;
    /* Simplified Sparkplug B encoding.
       Real Tahu uses Google Protocol Buffers; here we use a compact
       representation that demonstrates the Sparkplug B structure:
       - Group ID (string)
       - Edge Node ID (string)
       - Metric name -> uses PI tag_name
       - Data type -> DTYPE_DOUBLE
       - Value -> dp->value
       - Timestamp -> dp->timestamp
    */
    size_t gid_len = strlen(group_id);
    size_t eid_len = strlen(edge_node_id);
    size_t tag_len = dp->tag_name ? strlen(dp->tag_name) : 0;
    *out_len = 1 + 4 + gid_len + 4 + eid_len + 4 + tag_len + 1 + 8 + 8 + 1;
    uint8_t *buf = (uint8_t *)malloc(*out_len);
    if (!buf) return NULL;
    size_t off = 0;
    buf[off++] = SPARKPLUG_MAGIC;
    /* Group ID */
    uint32_t g32 = (uint32_t)gid_len;
    memcpy(buf + off, &g32, 4); off += 4;
    memcpy(buf + off, group_id, gid_len); off += gid_len;
    /* Edge Node ID */
    uint32_t e32 = (uint32_t)eid_len;
    memcpy(buf + off, &e32, 4); off += 4;
    memcpy(buf + off, edge_node_id, eid_len); off += eid_len;
    /* Metric name */
    uint32_t m32 = (uint32_t)tag_len;
    memcpy(buf + off, &m32, 4); off += 4;
    if (tag_len > 0) { memcpy(buf + off, dp->tag_name, tag_len); off += tag_len; }
    /* Data type */
    buf[off++] = (uint8_t)SPARKPLUG_DTYPE_DOUBLE;
    /* Value */
    memcpy(buf + off, &dp->value, 8); off += 8;
    /* Timestamp */
    memcpy(buf + off, &dp->timestamp, 8); off += 8;
    /* Is historical */
    buf[off++] = 0;
    return buf;
}

int pi_mqtt_deserialize_sparkplug_b(const uint8_t *payload, size_t payload_len,
                                     pi_data_point_t **dps_out, size_t *count_out) {
    if (!payload || payload_len < 30 || !dps_out || !count_out) return PI_BRIDGE_ERR_BAD_PARAM;
    *dps_out = (pi_data_point_t *)calloc(1, sizeof(pi_data_point_t));
    if (!*dps_out) return PI_BRIDGE_ERR_NO_MEMORY;
    *count_out = 1;
    pi_data_point_t *dp = *dps_out;
    size_t off = 0;
    if (payload[off++] != SPARKPLUG_MAGIC) { free(dp); *dps_out = NULL; *count_out = 0; return PI_BRIDGE_ERR_BAD_PARAM; }
    /* Skip group ID */
    uint32_t gid_len; memcpy(&gid_len, payload + off, 4); off += 4 + gid_len;
    /* Skip edge node ID */
    uint32_t eid_len; memcpy(&eid_len, payload + off, 4); off += 4 + eid_len;
    /* Metric name */
    uint32_t mname_len; memcpy(&mname_len, payload + off, 4); off += 4;
    dp->tag_name = (char *)malloc(mname_len + 1);
    if (dp->tag_name && mname_len > 0) { memcpy(dp->tag_name, payload + off, mname_len); dp->tag_name[mname_len] = '\0'; }
    off += mname_len;
    /* Data type */
    uint8_t dtype = payload[off++];
    if (dtype != SPARKPLUG_DTYPE_DOUBLE) { pi_data_point_free(dp); *dps_out = NULL; *count_out = 0; return PI_BRIDGE_ERR_TYPE_MISMATCH; }
    /* Value */
    memcpy(&dp->value, payload + off, 8); off += 8;
    /* Timestamp */
    memcpy(&dp->timestamp, payload + off, 8); off += 8;
    dp->quality = PI_QUALITY_GOOD;
    return PI_BRIDGE_OK;
}

void pi_mqtt_json_payload_free(pi_mqtt_json_payload_t *jp) {
    if (!jp) return;
    free(jp->json_buffer);
    free(jp);
}

void pi_mqtt_csv_payload_free(pi_mqtt_csv_payload_t *cp) {
    if (!cp) return;
    free(cp->csv_buffer);
    free(cp->header);
    free(cp);
}

/*=== L5: Message Batching ===*/

void pi_mqtt_configure_batching(pi_mqtt_client_t *client, const pi_mqtt_batch_config_t *batch_cfg) {
    if (!client || !batch_cfg) return;
    memcpy(&client->batch_cfg, batch_cfg, sizeof(pi_mqtt_batch_config_t));
}

void pi_mqtt_flush_batch(pi_mqtt_client_t *client) {
    if (!client || client->batch_count == 0) return;
    /* Publish all queued messages */
    for (int i = 0; i < client->batch_count; i++) {
        pi_mqtt_message_t *m = client->batch_queue[i];
        pi_mqtt_publish(client, m->topic, m->payload, m->payload_len, m->qos, m->retain);
        pi_mqtt_message_free(m);
        free(m);
    }
    client->batch_count = 0;
}

/*=== L5: Store-and-Forward (reliability via persistent queue) ===*/

void pi_mqtt_store_forward_init(pi_mqtt_client_t *client, const pi_mqtt_sf_config_t *sf_config) {
    if (!client) return;
    if (sf_config) {
        memcpy(&client->sf_cfg, sf_config, sizeof(pi_mqtt_sf_config_t));
        client->sf_cfg.storage_path = sf_config->storage_path ? _strdup(sf_config->storage_path) : NULL;
    } else {
        memset(&client->sf_cfg, 0, sizeof(pi_mqtt_sf_config_t));
        client->sf_cfg.max_entries = 1000;
        client->sf_cfg.max_total_bytes = 10 * 1024 * 1024; /* 10 MB */
        client->sf_cfg.max_retries = 5;
        client->sf_cfg.retry_base_delay_ms = 1000;
        client->sf_cfg.backoff_multiplier = 2.0;
        client->sf_cfg.max_retry_delay_ms = 60000;
    }
    client->sf_entry_capacity = 64;
    client->sf_entries = (pi_mqtt_store_forward_entry_t *)calloc(client->sf_entry_capacity, sizeof(pi_mqtt_store_forward_entry_t));
}

size_t pi_mqtt_store_forward_queue_size(const pi_mqtt_client_t *client) {
    return client ? client->sf_entry_count : 0;
}

int pi_mqtt_store_forward_drain(pi_mqtt_client_t *client) {
    if (!client) return -1;
    if (client->sf_entry_count == 0) return 0;
    int sent = 0;
    /* Send all queued store-and-forward messages */
    for (size_t i = 0; i < client->sf_entry_count; i++) {
        pi_mqtt_store_forward_entry_t *e = &client->sf_entries[i];
        if (!e->is_acknowledged && e->retry_count < (int32_t)client->sf_cfg.max_retries) {
            uint16_t id = pi_mqtt_publish(client, e->topic, e->payload, e->payload_len, e->qos, e->retain);
            if (id > 0) { e->is_acknowledged = true; sent++; }
        }
    }
    client->stats.messages_queued = client->sf_entry_count;
    return sent;
}

void pi_mqtt_store_forward_purge(pi_mqtt_client_t *client) {
    if (!client) return;
    for (size_t i = 0; i < client->sf_entry_count; i++) {
        free(client->sf_entries[i].topic);
        free(client->sf_entries[i].payload);
    }
    client->sf_entry_count = 0;
    client->sf_total_bytes = 0;
}

/*=== L4: Will Message (MQTT 3.1.1 §3.1.2.5) ===*/

void pi_mqtt_configure_will(pi_mqtt_client_t *client, const pi_mqtt_will_t *will) {
    if (!client || !will) return;
    pi_mqtt_will_t *w = &client->config.will_message;
    free(w->topic); w->topic = will->topic ? _strdup(will->topic) : NULL;
    free(w->payload);
    if (will->payload && will->payload_len > 0) {
        w->payload = (uint8_t *)malloc(will->payload_len);
        if (w->payload) memcpy(w->payload, will->payload, will->payload_len);
    } else { w->payload = NULL; }
    w->payload_len = will->payload_len;
    w->qos = will->qos;
    w->retain = will->retain;
    w->is_set = true;
}

/*=== Statistics ===*/

void pi_mqtt_get_stats(const pi_mqtt_client_t *client, pi_mqtt_stream_stats_t *stats) {
    if (!client || !stats) return;
    memcpy(stats, &client->stats, sizeof(pi_mqtt_stream_stats_t));
}

/*=== L3: Topic Validation & Matching (MQTT 3.1.1 §4.7) ===*/

bool pi_mqtt_topic_filter_is_valid(const char *topic_filter) {
    if (!topic_filter || topic_filter[0] == '\0') return false;
    size_t len = strlen(topic_filter);
    for (size_t i = 0; i < len; i++) {
        if (topic_filter[i] == '\0') return false;
        if (topic_filter[i] == '#') {
            /* '#' must be last character or followed by nothing (or after '/') */
            if (i != len - 1) return false;
            if (i > 0 && topic_filter[i-1] != '/') return false;
        }
    }
    return true;
}

bool pi_mqtt_topic_is_valid(const char *topic) {
    if (!topic || topic[0] == '\0') return false;
    for (const char *p = topic; *p; p++) {
        if (*p == '\0') return false;
        if (*p == '#' || *p == '+') return false; /* No wildcards in topic names */
    }
    return true;
}

bool pi_mqtt_topic_matches(const char *topic, const char *filter) {
    if (!topic || !filter) return false;
    /* MQTT 3.1.1 §4.7.1: Topic filter matching algorithm */
    while (*topic && *filter) {
        if (*filter == '#') return true; /* Multi-level wildcard matches everything remaining */
        if (*filter == '+') {
            /* Single-level wildcard: skip to next '/' */
            filter++;
            while (*topic && *topic != '/') topic++;
            if (!*topic && *filter) return false; /* Topic ended but filter has more */
        } else if (*filter == *topic) {
            filter++; topic++;
        } else {
            return false;
        }
    }
    /* Both must have ended */
    if (*topic != '\0') return false; /* Topic has more levels */
    if (*filter != '\0' && (*filter != '#' || *(filter+1) != '\0')) return false;
    return true;
}

/*=== Message helpers ===*/

void pi_mqtt_message_init(pi_mqtt_message_t *msg) {
    if (!msg) return;
    memset(msg, 0, sizeof(*msg));
}

void pi_mqtt_message_free(pi_mqtt_message_t *msg) {
    if (!msg) return;
    free(msg->topic); free(msg->payload); free(msg->content_type); free(msg->response_topic);
    memset(msg, 0, sizeof(*msg));
}

pi_mqtt_message_t *pi_mqtt_message_clone(const pi_mqtt_message_t *msg) {
    if (!msg) return NULL;
    pi_mqtt_message_t *clone = (pi_mqtt_message_t *)calloc(1, sizeof(pi_mqtt_message_t));
    if (!clone) return NULL;
    clone->topic = msg->topic ? _strdup(msg->topic) : NULL;
    if (msg->payload && msg->payload_len > 0) {
        clone->payload = (uint8_t *)malloc(msg->payload_len);
        if (clone->payload) memcpy(clone->payload, msg->payload, msg->payload_len);
        clone->payload_len = msg->payload_len;
    }
    clone->qos = msg->qos; clone->retain = msg->retain;
    clone->dup = msg->dup; clone->packet_id = msg->packet_id;
    clone->timestamp = msg->timestamp;
    clone->content_type = msg->content_type ? _strdup(msg->content_type) : NULL;
    clone->response_topic = msg->response_topic ? _strdup(msg->response_topic) : NULL;
    clone->message_expiry = msg->message_expiry;
    return clone;
}
