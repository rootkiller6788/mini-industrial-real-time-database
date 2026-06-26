/**
 * @file pi_opcua_bridge.h
 * @brief PI System to OPC UA Bridge — Core Types and API
 *
 * 知识覆盖:
 *   L1 Definitions:   OPC UA NodeId, QualifiedName, BrowsePath, Variant, DataValue
 *   L2 Core Concepts: Client-Server bridge, namespace mapping, monitored items
 *   L3 Structures:    OPC UA binary type encoding, Variant union, Read/Write services
 *   L4 Standards:     IEC 62541 (OPC UA) Part 3 Address Space, Part 4 Services, Part 8 DA
 *   L5 Algorithms:    MonitoredItem sampling, deadband filtering, subscription pacing
 *   L7 Applications:  OSIsoft PI Integrator for OPC UA — real industrial product
 *
 * 九校课程对标:
 *   MIT 6.302: Feedback over networked control systems
 *   RWTH Aachen: OPC UA in Industrie 4.0
 *   ISA/IEC 62541: OPC UA standard family
 *
 * @license MIT
 */

#ifndef PI_OPCUA_BRIDGE_H
#define PI_OPCUA_BRIDGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * L1 — OPC UA Core Type Definitions
 *   IEC 62541-3: Address Space Model
 *   IEC 62541-6: Mappings (UA Binary)
 *===========================================================================*/

/** OPC UA built-in data types (IEC 62541-6 §5.2) */
typedef enum {
    PI_OPCUA_TYPE_NULL       = 0,
    PI_OPCUA_TYPE_BOOLEAN    = 1,
    PI_OPCUA_TYPE_SBYTE      = 2,
    PI_OPCUA_TYPE_BYTE       = 3,
    PI_OPCUA_TYPE_INT16      = 4,
    PI_OPCUA_TYPE_UINT16     = 5,
    PI_OPCUA_TYPE_INT32      = 6,
    PI_OPCUA_TYPE_UINT32     = 7,
    PI_OPCUA_TYPE_INT64      = 8,
    PI_OPCUA_TYPE_UINT64     = 9,
    PI_OPCUA_TYPE_FLOAT      = 10,
    PI_OPCUA_TYPE_DOUBLE     = 11,
    PI_OPCUA_TYPE_STRING     = 12,
    PI_OPCUA_TYPE_DATETIME   = 13,
    PI_OPCUA_TYPE_GUID       = 14,
    PI_OPCUA_TYPE_BYTESTRING = 15,
    PI_OPCUA_TYPE_XMLELEMENT = 16,
    PI_OPCUA_TYPE_NODEID     = 17,
    PI_OPCUA_TYPE_STATUSCODE = 19,
    PI_OPCUA_TYPE_QUALIFIEDNAME = 20,
    PI_OPCUA_TYPE_LOCALIZEDTEXT = 21,
    PI_OPCUA_TYPE_VARIANT    = 24,
    PI_OPCUA_TYPE_DIAGNOSTICINFO = 25,
    PI_OPCUA_TYPE_COUNT      = 30
} pi_opcua_builtin_type_t;

/** OPC UA NodeId identifier type (IEC 62541-3 §7.2) */
typedef enum {
    PI_OPCUA_ID_NUMERIC   = 0,
    PI_OPCUA_ID_STRING    = 1,
    PI_OPCUA_ID_GUID      = 2,
    PI_OPCUA_ID_OPAQUE    = 3
} pi_opcua_id_type_t;

/** OPC UA NodeId (IEC 62541-3 §5.2.2.2) */
typedef struct {
    uint16_t            namespace_index;
    pi_opcua_id_type_t  identifier_type;
    union {
        uint32_t        numeric;
        char           *string;
        uint8_t         guid[16];
        struct {
            uint8_t    *data;
            size_t      length;
        } opaque;
    } identifier;
} pi_opcua_node_id_t;

/** OPC UA QualifiedName (IEC 62541-3 §7.3) */
typedef struct {
    uint16_t  namespace_index;
    char     *name;
} pi_opcua_qualified_name_t;

/** OPC UA LocalizedText (IEC 62541-3 §7.4) */
typedef struct {
    char *locale;
    char *text;
} pi_opcua_localized_text_t;

/** OPC UA Variant (IEC 62541-6 §5.2.2.16) */
typedef struct {
    pi_opcua_builtin_type_t  type;
    int32_t                  array_dimensions;
    union {
        bool            bool_val;
        int8_t          sbyte_val;
        uint8_t         byte_val;
        int16_t         int16_val;
        uint16_t        uint16_val;
        int32_t         int32_val;
        uint32_t        uint32_val;
        int64_t         int64_val;
        uint64_t        uint64_val;
        float           float_val;
        double          double_val;
        char           *string_val;
        int64_t         datetime_val;
        uint8_t         guid_val[16];
        struct {
            uint8_t    *data;
            size_t      length;
        } byte_string;
        pi_opcua_node_id_t  node_id_val;
        uint32_t            status_code_val;
    } value;
} pi_opcua_variant_t;

/** OPC UA DataValue (IEC 62541-4 §7) */
typedef struct {
    pi_opcua_variant_t  value;
    uint32_t            status_code;
    int64_t             source_timestamp;
    uint16_t            source_picoseconds;
    int64_t             server_timestamp;
    uint16_t            server_picoseconds;
} pi_opcua_data_value_t;

/** OPC UA BrowsePath element (IEC 62541-4 §7.2) */
typedef struct {
    pi_opcua_node_id_t          starting_node;
    pi_opcua_qualified_name_t   target_name;
    bool                        is_forward;
    bool                        include_subtypes;
} pi_opcua_browse_path_t;

/** OPC UA ReferenceDescription (IEC 62541-4 §7.4) */
typedef struct {
    pi_opcua_node_id_t        node_id;
    pi_opcua_qualified_name_t browse_name;
    pi_opcua_localized_text_t display_name;
    pi_opcua_node_id_t        type_definition;
    uint32_t                  node_class;
    bool                      is_forward;
} pi_opcua_reference_desc_t;

/** OPC UA ReadValueId (IEC 62541-4 §7.25) */
typedef struct {
    pi_opcua_node_id_t  node_id;
    uint32_t            attribute_id;
    char               *index_range;
    pi_opcua_qualified_name_t data_encoding;
} pi_opcua_read_value_id_t;

/*===========================================================================
 * L1 — PI Data Quality & Point Types
 *===========================================================================*/

typedef enum {
    PI_QUALITY_GOOD              = 0,
    PI_QUALITY_BAD               = 1,
    PI_QUALITY_UNCERTAIN         = 2,
    PI_QUALITY_SUBSTITUTED       = 3,
    PI_QUALITY_QUESTIONABLE      = 4,
    PI_QUALITY_CONVERSION_ERROR  = 5,
    PI_QUALITY_SENSOR_FAILURE    = 6,
    PI_QUALITY_LAST_KNOWN        = 7,
    PI_QUALITY_COMM_FAILURE      = 8,
    PI_QUALITY_OUT_OF_SERVICE    = 9
} pi_quality_code_t;

typedef struct {
    char       *tag_name;
    char       *descriptor;
    char       *eng_units;
    double      zero;
    double      span;
    double      typical_value;
    uint32_t    point_id;
    uint32_t    point_type;
    bool        is_step;
    bool        is_compressing;
    double      compdev;
    double      compmin;
    double      compmax;
    char       *digit_set;
    int32_t     archiving;
} pi_point_attributes_t;

typedef struct {
    char              *tag_name;
    double             value;
    int64_t            timestamp;
    int32_t            subsecond;
    pi_quality_code_t  quality;
    bool               is_annotated;
    char              *annotation;
} pi_data_point_t;

typedef struct {
    pi_opcua_node_id_t  opcua_node_id;
    char               *pi_tag_name;
    uint32_t            update_rate_ms;
    double              deadband;
    bool                is_bidirectional;
    uint8_t            *last_raw_value;
    size_t              last_raw_len;
    int64_t             last_update_time;
    bool                is_valid;
} pi_opcua_pi_mapping_t;

/*===========================================================================
 * L2 — OPC UA Session & Client Bridge
 *===========================================================================*/

typedef struct {
    char     *endpoint_url;
    char     *application_name;
    char     *application_uri;
    uint32_t  session_timeout_ms;
    uint32_t  connect_timeout_ms;
    uint32_t  requested_lifetime_ms;
    uint32_t  request_timeout_ms;
    bool      use_security;
    char     *security_policy;
    char     *certificate_path;
    char     *private_key_path;
    bool      auto_reconnect;
    uint32_t  max_retry_count;
    uint32_t  retry_interval_ms;
} pi_opcua_session_config_t;

typedef struct pi_opcua_session_s pi_opcua_session_t;

typedef enum {
    PI_OPCUA_SESSION_DISCONNECTED = 0,
    PI_OPCUA_SESSION_CONNECTING   = 1,
    PI_OPCUA_SESSION_CONNECTED    = 2,
    PI_OPCUA_SESSION_ACTIVATED    = 3,
    PI_OPCUA_SESSION_CLOSING      = 4,
    PI_OPCUA_SESSION_RECONNECTING = 5,
    PI_OPCUA_SESSION_ERROR        = 99
} pi_opcua_session_state_t;

/*===========================================================================
 * L2 / L3 — OPC UA Subscription & MonitoredItem
 *===========================================================================*/

typedef enum {
    PI_OPCUA_MONITOR_STATUS  = 0,
    PI_OPCUA_MONITOR_VALUE   = 1,
    PI_OPCUA_MONITOR_STATUS_VALUE = 2
} pi_opcua_monitor_mode_t;

typedef enum {
    PI_OPCUA_DEADBAND_NONE      = 0,
    PI_OPCUA_DEADBAND_ABSOLUTE  = 1,
    PI_OPCUA_DEADBAND_PERCENT   = 2
} pi_opcua_deadband_type_t;

typedef void (*pi_opcua_monitor_callback_t)(
    void                      *user_data,
    const pi_opcua_node_id_t  *node_id,
    const pi_opcua_data_value_t *data_value
);

typedef struct pi_opcua_subscription_s pi_opcua_subscription_t;

/*===========================================================================
 * L1 — OPC UA Endpoint Description
 *===========================================================================*/

typedef struct {
    char              *endpoint_url;
    char              *server_uri;
    char              *server_name;
    char              *server_certificate;
    uint32_t           security_level;
    char              *security_policy_uri;
    char              *transport_profile;
    bool               supports_binary;
} pi_opcua_endpoint_t;

/*===========================================================================
 * Results & Statistics
 *===========================================================================*/

typedef enum {
    PI_BRIDGE_OK                   = 0,
    PI_BRIDGE_ERR_SESSION          = 1,
    PI_BRIDGE_ERR_TIMEOUT          = 2,
    PI_BRIDGE_ERR_BAD_PARAM        = 3,
    PI_BRIDGE_ERR_NO_MEMORY        = 4,
    PI_BRIDGE_ERR_NODE_NOT_FOUND   = 5,
    PI_BRIDGE_ERR_BAD_ATTRIBUTE    = 6,
    PI_BRIDGE_ERR_TYPE_MISMATCH    = 7,
    PI_BRIDGE_ERR_WRITE_DENIED     = 8,
    PI_BRIDGE_ERR_CONNECTION_LOST  = 9,
    PI_BRIDGE_ERR_BAD_SECURITY     = 10,
    PI_BRIDGE_ERR_SERVER_FAULT     = 11,
    PI_BRIDGE_ERR_BUFFER_OVERFLOW  = 12,
    PI_BRIDGE_ERR_NOT_IMPLEMENTED  = 13,
    PI_BRIDGE_ERR_INVALID_STATE    = 14
} pi_bridge_result_t;

typedef struct {
    uint64_t  total_reads;
    uint64_t  total_writes;
    uint64_t  total_subscriptions;
    uint64_t  total_data_changes;
    uint64_t  total_errors;
    uint64_t  total_reconnects;
    uint64_t  bytes_received;
    uint64_t  bytes_sent;
    double    avg_read_latency_ms;
    double    avg_write_latency_ms;
    double    uptime_seconds;
    uint32_t  active_monitored_items;
    uint32_t  active_subscriptions;
} pi_opcua_bridge_stats_t;

/*===========================================================================
 * API Declarations — Session Management (L2, L4)
 *===========================================================================*/

void pi_opcua_session_config_init(pi_opcua_session_config_t *cfg);
pi_opcua_session_t *pi_opcua_session_connect(const pi_opcua_session_config_t *cfg);
void pi_opcua_session_disconnect(pi_opcua_session_t *session);
pi_opcua_session_state_t pi_opcua_session_get_state(const pi_opcua_session_t *session);
const char *pi_opcua_session_get_last_error(const pi_opcua_session_t *session);

int pi_opcua_discover_endpoints(const char *url,
                                pi_opcua_endpoint_t *endpoints,
                                size_t max_count);
void pi_opcua_endpoint_free(pi_opcua_endpoint_t *ep);

/* --- Read/Write Services (L3, IEC 62541-4 §7.25/7.26) --- */

pi_bridge_result_t pi_opcua_read_value(pi_opcua_session_t *session,
                                        const pi_opcua_node_id_t *node_id,
                                        pi_opcua_data_value_t *value_out);
pi_bridge_result_t *pi_opcua_read_values(pi_opcua_session_t *session,
                                          const pi_opcua_read_value_id_t *read_items,
                                          size_t count,
                                          pi_opcua_data_value_t *values_out);
pi_bridge_result_t pi_opcua_write_value(pi_opcua_session_t *session,
                                         const pi_opcua_node_id_t *node_id,
                                         const pi_opcua_variant_t *value);

/* --- Browse Services (L3, IEC 62541-4 §7.2) --- */

int pi_opcua_browse(pi_opcua_session_t *session,
                    const pi_opcua_node_id_t *start_node,
                    size_t max_results,
                    pi_opcua_reference_desc_t *results_out);
void pi_opcua_reference_descs_free(pi_opcua_reference_desc_t *refs, size_t count);

/* --- Subscription Services (L5, IEC 62541-4 §5.13) --- */

pi_opcua_subscription_t *pi_opcua_subscription_create(
    pi_opcua_session_t *session,
    double publishing_interval_ms,
    uint32_t lifetime_count,
    uint32_t max_keepalive_count);
pi_bridge_result_t pi_opcua_subscription_delete(pi_opcua_subscription_t *sub);
pi_bridge_result_t pi_opcua_monitored_item_add(
    pi_opcua_subscription_t *sub,
    const pi_opcua_node_id_t *node_id,
    double sampling_interval_ms,
    uint32_t queue_size,
    pi_opcua_deadband_type_t deadband_type,
    double deadband_value,
    pi_opcua_monitor_callback_t callback,
    void *user_data);
pi_bridge_result_t pi_opcua_monitored_item_remove(
    pi_opcua_subscription_t *sub,
    const pi_opcua_node_id_t *node_id);

/* --- PI-OPC UA Bridge (L6: Canonical Problem) --- */

pi_opcua_pi_mapping_t *pi_opcua_pi_map_tag(
    pi_opcua_session_t *session,
    const char *pi_tag_name,
    const pi_opcua_node_id_t *opcua_node_id,
    uint32_t update_rate_ms,
    double deadband,
    bool bidirectional);
pi_bridge_result_t pi_opcua_pi_write_to_opcua(
    pi_opcua_session_t *session,
    pi_opcua_pi_mapping_t *mapping,
    const pi_data_point_t *data_point);
pi_bridge_result_t pi_opcua_pi_read_from_opcua(
    pi_opcua_session_t *session,
    pi_opcua_pi_mapping_t *mapping,
    pi_data_point_t *data_point_out);
void pi_opcua_pi_mapping_free(pi_opcua_pi_mapping_t *mapping);

/* --- Historical Access (L4: IEC 62541-11 HA profile) --- */

int pi_opcua_history_read_raw(pi_opcua_session_t *session,
                               const pi_opcua_node_id_t *node_id,
                               int64_t start_time,
                               int64_t end_time,
                               uint32_t max_values,
                               pi_opcua_data_value_t **values_out);
int pi_opcua_history_read_processed(pi_opcua_session_t *session,
                                     const pi_opcua_node_id_t *node_id,
                                     int64_t start_time,
                                     int64_t end_time,
                                     double processing_interval_ms,
                                     uint32_t aggregate_id,
                                     pi_opcua_data_value_t **values_out);

/* --- Statistics --- */

void pi_opcua_bridge_get_stats(const pi_opcua_session_t *session,
                               pi_opcua_bridge_stats_t *stats);

/* --- NodeId Helpers (L1) --- */

pi_opcua_node_id_t *pi_opcua_node_id_new_numeric(uint16_t ns, uint32_t id);
pi_opcua_node_id_t *pi_opcua_node_id_new_string(uint16_t ns, const char *id);
pi_opcua_node_id_t *pi_opcua_node_id_clone(const pi_opcua_node_id_t *src);
bool pi_opcua_node_id_equals(const pi_opcua_node_id_t *a, const pi_opcua_node_id_t *b);
void pi_opcua_node_id_free(pi_opcua_node_id_t *node_id);
uint64_t pi_opcua_node_id_hash(const pi_opcua_node_id_t *node_id);

int64_t pi_opcua_datetime_to_unix(int64_t ua_datetime);
int64_t pi_opcua_datetime_from_unix(int64_t unix_seconds);
uint32_t pi_quality_to_opcua_status(pi_quality_code_t quality);
pi_quality_code_t pi_opcua_status_to_quality(uint32_t status_code);

/* --- Variant Helpers (L3) --- */

void pi_opcua_variant_init(pi_opcua_variant_t *var);
void pi_opcua_variant_set_double(pi_opcua_variant_t *var, double value);
void pi_opcua_variant_set_int32(pi_opcua_variant_t *var, int32_t value);
pi_bridge_result_t pi_opcua_variant_set_string(pi_opcua_variant_t *var, const char *value);
void pi_opcua_variant_set_bool(pi_opcua_variant_t *var, bool value);
double pi_opcua_variant_as_double(const pi_opcua_variant_t *var);
void pi_opcua_variant_clear(pi_opcua_variant_t *var);
void pi_opcua_data_value_clear(pi_opcua_data_value_t *dv);

/* --- PI Point Helpers --- */

pi_data_point_t *pi_data_point_new(const char *tag_name, double value,
                                    int64_t timestamp, pi_quality_code_t quality);
void pi_data_point_free(pi_data_point_t *dp);

#ifdef __cplusplus
}
#endif

#endif /* PI_OPCUA_BRIDGE_H */
