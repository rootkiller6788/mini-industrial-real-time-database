/**
 * @file pi_integrator_data_model.h
 * @brief PI Integrator Data Model header
 */
#ifndef PI_INTEGRATOR_DATA_MODEL_H
#define PI_INTEGRATOR_DATA_MODEL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PI_POINT_TYPE_FLOAT16 = 0, PI_POINT_TYPE_FLOAT32 = 2, PI_POINT_TYPE_FLOAT64 = 3,
    PI_POINT_TYPE_INT16 = 4, PI_POINT_TYPE_INT32 = 5, PI_POINT_TYPE_DIGITAL = 6,
    PI_POINT_TYPE_STRING = 7, PI_POINT_TYPE_BLOB = 8, PI_POINT_TYPE_TIMESTAMP = 9
} pi_point_type_t;

typedef struct {
    uint32_t point_id; char *tag; char *descriptor; char *eng_units;
    char *point_source; pi_point_type_t point_type;
    double zero; double span; double typical_value;
    bool is_step; int32_t archiving;
    double compdev; double compmin; double compmax;
    int32_t excdev; double excmax; double excmin;
    char *filter_code; double filter_constant;
    char *digit_set_name; bool is_totalizer;
    char *instrument_tag;
    char *location1; char *location2; char *location3; char *location4; char *location5;
} pi_point_config_t;

typedef enum {
    PI_AF_ELEMENT_TYPE_UNKNOWN = 0, PI_AF_ELEMENT_TYPE_EQUIPMENT = 1,
    PI_AF_ELEMENT_TYPE_PROCESS = 2, PI_AF_ELEMENT_TYPE_SYSTEM = 3,
    PI_AF_ELEMENT_TYPE_SITE = 4, PI_AF_ELEMENT_TYPE_ENTERPRISE = 5,
    PI_AF_ELEMENT_TYPE_UNIT = 6, PI_AF_ELEMENT_TYPE_LINE = 7,
    PI_AF_ELEMENT_TYPE_CELL = 8
} pi_af_element_type_t;

typedef struct {
    char *name; char *description; pi_af_element_type_t element_type;
    char *template_name; bool is_root; uint64_t unique_id;
} pi_af_element_t;

typedef enum {
    PI_AF_ATTR_TYPE_NONE = 0, PI_AF_ATTR_TYPE_BOOLEAN = 1,
    PI_AF_ATTR_TYPE_INT32 = 2, PI_AF_ATTR_TYPE_DOUBLE = 3,
    PI_AF_ATTR_TYPE_STRING = 4, PI_AF_ATTR_TYPE_DATETIME = 5,
    PI_AF_ATTR_TYPE_ENUM = 6, PI_AF_ATTR_TYPE_BYTE = 7,
    PI_AF_ATTR_TYPE_SINGLE = 8
} pi_af_attr_type_t;

typedef struct {
    char *name; char *description; pi_af_attr_type_t attr_type;
    char *eng_units; char *pi_point_name;
    bool is_configuration; bool is_hidden; bool is_excluded;
    double default_upper; double default_lower;
    bool has_limits; uint64_t unique_id;
} pi_af_attribute_t;

typedef enum {
    PI_AF_DR_NONE = 0, PI_AF_DR_PI_POINT = 1, PI_AF_DR_FORMULA = 2,
    PI_AF_DR_TABLE = 3, PI_AF_DR_ANALYSIS = 4, PI_AF_DR_URI = 5
} pi_af_data_ref_type_t;

typedef struct {
    pi_af_data_ref_type_t ref_type; char *pi_point_name;
    char *formula; char *table_name; char *analysis_name;
    char *uri; bool is_valid; uint32_t point_id;
} pi_af_data_reference_t;

typedef enum {
    PI_COLLECTIVE_TYPE_INTERPOLATED = 0,
    PI_COLLECTIVE_TYPE_STEPPED = 1,
    PI_COLLECTIVE_TYPE_RETAINED = 2
} pi_collective_type_t;

typedef struct {
    int64_t timestamp; double value; pi_collective_type_t interp_type;
    bool is_questionable; bool is_annotated; char *annotation;
} pi_collective_value_t;

typedef struct {
    char *tag_name; uint32_t point_id;
    int num_values; pi_collective_value_t *values;
    int64_t start_time; int64_t end_time; int32_t archive_record_count;
    double min_value; double max_value; double avg_value; double std_dev_value;
    bool is_complete;
} pi_collective_data_t;

typedef enum {
    PI_DTO_FORMAT_JSON = 0, PI_DTO_FORMAT_XML = 1,
    PI_DTO_FORMAT_CSV = 2, PI_DTO_FORMAT_BINARY = 3,
    PI_DTO_FORMAT_PROTOBUF = 4
} pi_dto_format_t;

typedef enum {
    PI_DTO_DIRECTION_NONE = 0, PI_DTO_DIRECTION_TO_OPCUA = 1,
    PI_DTO_DIRECTION_TO_MQTT = 2, PI_DTO_DIRECTION_BOTH = 3
} pi_dto_direction_t;

typedef struct {
    char *source_system; char *target_system;
    pi_dto_direction_t direction; pi_dto_format_t format;
    char *schema_version; uint32_t batch_id;
    int64_t creation_time; char *payload; size_t payload_length;
    bool is_compressed; bool is_encrypted; bool is_signed;
} pi_transfer_object_t;

typedef enum {
    PI_MAP_OK = 0, PI_MAP_ERR_NO_MAPPING = 1, PI_MAP_ERR_AMBIGUOUS = 2,
    PI_MAP_ERR_INCOMPATIBLE = 3, PI_MAP_ERR_RANGE_OVERFLOW = 4,
    PI_MAP_ERR_PRECISION_LOSS = 5
} pi_map_result_t;

typedef struct {
    pi_point_type_t pi_type; int opcua_type;
    int mqtt_encoding_type; int sparkplug_type;
    bool is_lossless; char *description;
    double min_range; double max_range; int32_t precision_digits;
} pi_type_mapping_entry_t;

typedef struct {
    int num_entries; pi_type_mapping_entry_t *entries;
    bool strict_mode; bool nan_as_null; bool inf_as_max;
} pi_type_mapping_registry_t;

typedef struct {
    char *schema_name; char *schema_version;
    int num_fields; char **field_names;
    int *field_types; bool *field_required; char **field_defaults;
} pi_data_schema_t;

typedef struct {
    char *name; char *version; char *namespace_uri;
    int num_types; pi_type_mapping_entry_t *type_map; bool validated;
} pi_opcua_type_dictionary_t;

typedef struct {
    int64_t pi_time; int32_t subsecond; bool is_valid; bool is_local;
} pi_timestamp_t;

typedef struct {
    double input_value; double input_zero; double input_span;
    double output_zero; double output_span; double output_value;
    bool is_clamped; bool is_inverted;
} pi_scale_conversion_t;

typedef enum {
    PI_CONV_LINEAR = 0, PI_CONV_SQRT = 1, PI_CONV_LOGARITHMIC = 2,
    PI_CONV_POLYNOMIAL = 3, PI_CONV_TABLE = 4
} pi_conversion_type_t;

typedef struct {
    pi_conversion_type_t conv_type;
    double coeff_a; double coeff_b; double coeff_c; double coeff_d; double coeff_e;
    double *table_inputs; double *table_outputs; int table_size;
} pi_conversion_spec_t;

typedef struct {
    char *name; char *time_series_query;
    double sampling_interval_ms; bool is_aggregate;
    int aggregate_type; char *aggregate_column;
    bool is_rolling; double rolling_window_sec;
} pi_view_definition_t;

typedef struct {
    char *name; int num_views; pi_view_definition_t *views;
    bool is_materialized; int64_t last_refresh_time;
    double refresh_interval_sec; bool is_streaming; char *mqtt_output_topic;
} pi_asset_view_t;

void pi_point_config_init(pi_point_config_t *cfg);
void pi_point_config_free(pi_point_config_t *cfg);
pi_af_element_t *pi_af_element_new(const char *name, const char *description, pi_af_element_type_t etype);
void pi_af_element_free(pi_af_element_t *elem);
pi_af_attribute_t *pi_af_attribute_new(const char *name, pi_af_attr_type_t type, const char *uom);
void pi_af_attribute_free(pi_af_attribute_t *attr);
void pi_af_data_reference_init(pi_af_data_reference_t *ref);
void pi_af_data_reference_free(pi_af_data_reference_t *ref);
pi_collective_data_t *pi_collective_data_new(const char *tag, int num_vals);
void pi_collective_data_free(pi_collective_data_t *cd);
void pi_collective_data_add_value(pi_collective_data_t *cd, int64_t ts, double val, pi_collective_type_t itype);
void pi_collective_data_compute_stats(pi_collective_data_t *cd);
pi_transfer_object_t *pi_dto_new(const char *src, const char *tgt, pi_dto_direction_t dir, pi_dto_format_t fmt);
void pi_dto_free(pi_transfer_object_t *dto);
void pi_dto_set_json_payload(pi_transfer_object_t *dto, const char *json);
void pi_dto_set_csv_payload(pi_transfer_object_t *dto, const char *csv);
void pi_dto_set_binary_payload(pi_transfer_object_t *dto, const uint8_t *data, size_t len);
pi_type_mapping_registry_t *pi_type_mapping_registry_new(void);
void pi_type_mapping_registry_free(pi_type_mapping_registry_t *reg);
void pi_type_mapping_registry_add_entry(pi_type_mapping_registry_t *reg, const pi_type_mapping_entry_t *entry);
pi_map_result_t pi_type_map_pi_to_opcua(const pi_type_mapping_registry_t *reg, pi_point_type_t pi_type, int *opcua_type);
pi_map_result_t pi_type_map_pi_to_sparkplug(const pi_type_mapping_registry_t *reg, pi_point_type_t pi_type, int *sp_type);
pi_timestamp_t pi_timestamp_now(void);
pi_timestamp_t pi_timestamp_from_pi_time(int64_t pi_time_sec, int32_t subsec_ms);
int64_t pi_timestamp_to_unix_ms(pi_timestamp_t ts);
double pi_scale_convert(double val, double in_zero, double in_span, double out_zero, double out_span);
double pi_conversion_apply(const pi_conversion_spec_t *spec, double input);
void pi_conversion_spec_init(pi_conversion_spec_t *spec);
void pi_conversion_spec_free(pi_conversion_spec_t *spec);
pi_asset_view_t *pi_asset_view_new(const char *name);
void pi_asset_view_free(pi_asset_view_t *av);
void pi_asset_view_add_view_def(pi_asset_view_t *av, const pi_view_definition_t *view);

#ifdef __cplusplus
}
#endif
#endif
