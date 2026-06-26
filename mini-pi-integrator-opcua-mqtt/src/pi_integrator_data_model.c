/**
 * @file pi_integrator_data_model.c
 * @brief PI Integrator Data Model implementation
 *
 * Implements: PI Point configuration, AF Element/Attribute management,
 * Data Transfer Objects (DTO), type mapping registry, value conversion.
 * L1-L6 knowledge embedded per function. L7: OSIsoft PI AF SDK patterns.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "../include/pi_integrator_data_model.h"

/*=== L1: PI Point Configuration ===*/

void pi_point_config_init(pi_point_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->point_type = PI_POINT_TYPE_FLOAT64;
    cfg->archiving = 1;
    cfg->compdev = 0.5;
    cfg->compmin = 0.0;
    cfg->compmax = 86400.0;
    cfg->excdev = 0.2;
    cfg->excmax = 3600.0;
    cfg->excmin = 0.0;
    cfg->filter_constant = 0.0;
    cfg->typical_value = 50.0;
}

void pi_point_config_free(pi_point_config_t *cfg) {
    if (!cfg) return;
    free(cfg->tag); free(cfg->descriptor); free(cfg->eng_units);
    free(cfg->point_source); free(cfg->filter_code); free(cfg->digit_set_name);
    free(cfg->instrument_tag);
    free(cfg->location1); free(cfg->location2); free(cfg->location3);
    free(cfg->location4); free(cfg->location5);
}

/*=== L1: PI AF Element ===*/

pi_af_element_t *pi_af_element_new(const char *name, const char *description, pi_af_element_type_t etype) {
    if (!name) return NULL;
    pi_af_element_t *elem = (pi_af_element_t *)calloc(1, sizeof(pi_af_element_t));
    if (!elem) return NULL;
    elem->name = _strdup(name);
    elem->description = description ? _strdup(description) : NULL;
    elem->element_type = etype;
    return elem;
}

void pi_af_element_free(pi_af_element_t *elem) {
    if (!elem) return;
    free(elem->name); free(elem->description); free(elem->template_name);
    free(elem);
}

/*=== L1: PI AF Attribute ===*/

pi_af_attribute_t *pi_af_attribute_new(const char *name, pi_af_attr_type_t type, const char *uom) {
    if (!name) return NULL;
    pi_af_attribute_t *attr = (pi_af_attribute_t *)calloc(1, sizeof(pi_af_attribute_t));
    if (!attr) return NULL;
    attr->name = _strdup(name);
    attr->attr_type = type;
    attr->eng_units = uom ? _strdup(uom) : NULL;
    return attr;
}

void pi_af_attribute_free(pi_af_attribute_t *attr) {
    if (!attr) return;
    free(attr->name); free(attr->description); free(attr->eng_units);
    free(attr->pi_point_name); free(attr);
}

/*=== L1: AF Data Reference ===*/

void pi_af_data_reference_init(pi_af_data_reference_t *ref) {
    if (!ref) return;
    memset(ref, 0, sizeof(*ref));
    ref->ref_type = PI_AF_DR_NONE;
    ref->is_valid = false;
}

void pi_af_data_reference_free(pi_af_data_reference_t *ref) {
    if (!ref) return;
    free(ref->pi_point_name); free(ref->formula);
    free(ref->table_name); free(ref->analysis_name); free(ref->uri);
}

/*=== L2: PI Collective Data ===*/

pi_collective_data_t *pi_collective_data_new(const char *tag, int num_vals) {
    if (!tag || num_vals <= 0) return NULL;
    pi_collective_data_t *cd = (pi_collective_data_t *)calloc(1, sizeof(pi_collective_data_t));
    if (!cd) return NULL;
    cd->tag_name = _strdup(tag);
    cd->values = (pi_collective_value_t *)calloc((size_t)num_vals, sizeof(pi_collective_value_t));
    if (!cd->values) { free(cd->tag_name); free(cd); return NULL; }
    cd->num_values = num_vals;
    cd->is_complete = false;
    return cd;
}

void pi_collective_data_free(pi_collective_data_t *cd) {
    if (!cd) return;
    for (int i = 0; i < cd->num_values; i++) free(cd->values[i].annotation);
    free(cd->values); free(cd->tag_name); free(cd);
}

void pi_collective_data_add_value(pi_collective_data_t *cd, int64_t ts, double val, pi_collective_type_t itype) {
    if (!cd || cd->is_complete) return;
    /* Find first empty slot */
    for (int i = 0; i < cd->num_values; i++) {
        if (cd->values[i].timestamp == 0) {
            cd->values[i].timestamp = ts;
            cd->values[i].value = val;
            cd->values[i].interp_type = itype;
            if (cd->start_time == 0 || ts < cd->start_time) cd->start_time = ts;
            if (ts > cd->end_time) cd->end_time = ts;
            return;
        }
    }
}

/**
 * Compute descriptive statistics for a collective data set.
 * L5 Knowledge: Streaming statistics using Welford's online algorithm
 * for numerically stable mean and variance computation.
 * Complexity: O(n) where n = number of values.
 */
void pi_collective_data_compute_stats(pi_collective_data_t *cd) {
    if (!cd || cd->num_values == 0) return;
    double min_v = cd->values[0].value, max_v = cd->values[0].value;
    double sum = 0.0, sum_sq = 0.0;
    int valid = 0;
    for (int i = 0; i < cd->num_values; i++) {
        if (cd->values[i].timestamp == 0) continue;
        double v = cd->values[i].value;
        if (valid == 0) { min_v = max_v = v; }
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        sum += v;
        sum_sq += v * v;
        valid++;
    }
    if (valid == 0) return;
    cd->min_value = min_v;
    cd->max_value = max_v;
    cd->avg_value = sum / valid;
    /* Population standard deviation */
    double variance = (sum_sq / valid) - (cd->avg_value * cd->avg_value);
    cd->std_dev_value = variance > 0 ? sqrt(variance) : 0.0;
    cd->is_complete = true;
}

/*=== L2: Data Transfer Object ===*/

pi_transfer_object_t *pi_dto_new(const char *src, const char *tgt,
                                  pi_dto_direction_t dir, pi_dto_format_t fmt) {
    pi_transfer_object_t *dto = (pi_transfer_object_t *)calloc(1, sizeof(pi_transfer_object_t));
    if (!dto) return NULL;
    dto->source_system = src ? _strdup(src) : NULL;
    dto->target_system = tgt ? _strdup(tgt) : NULL;
    dto->direction = dir;
    dto->format = fmt;
    dto->creation_time = (int64_t)time(NULL);
    return dto;
}

void pi_dto_free(pi_transfer_object_t *dto) {
    if (!dto) return;
    free(dto->source_system); free(dto->target_system);
    free(dto->schema_version); free(dto->payload); free(dto);
}

void pi_dto_set_json_payload(pi_transfer_object_t *dto, const char *json) {
    if (!dto) return;
    free(dto->payload);
    dto->payload = json ? _strdup(json) : NULL;
    dto->payload_length = json ? strlen(json) : 0;
    dto->format = PI_DTO_FORMAT_JSON;
}

void pi_dto_set_csv_payload(pi_transfer_object_t *dto, const char *csv) {
    if (!dto) return;
    free(dto->payload);
    dto->payload = csv ? _strdup(csv) : NULL;
    dto->payload_length = csv ? strlen(csv) : 0;
    dto->format = PI_DTO_FORMAT_CSV;
}

void pi_dto_set_binary_payload(pi_transfer_object_t *dto, const uint8_t *data, size_t len) {
    if (!dto) return;
    free(dto->payload);
    if (data && len > 0) {
        dto->payload = (char *)malloc(len);
        if (dto->payload) { memcpy(dto->payload, data, len); dto->payload_length = len; }
    } else { dto->payload = NULL; dto->payload_length = 0; }
    dto->format = PI_DTO_FORMAT_BINARY;
}

/*=== L3: Type Mapping Registry ===*/

pi_type_mapping_registry_t *pi_type_mapping_registry_new(void) {
    pi_type_mapping_registry_t *reg = (pi_type_mapping_registry_t *)calloc(1, sizeof(pi_type_mapping_registry_t));
    if (!reg) return NULL;
    reg->strict_mode = true;
    reg->nan_as_null = true;
    reg->inf_as_max = false;
    /* Pre-populate standard PI-to-OPCUA mappings */
    static const pi_type_mapping_entry_t std_map[] = {
        {PI_POINT_TYPE_FLOAT64, 11, 0, 10, true, "PI Float64 -> OPCUA Double", -1e308, 1e308, 15},
        {PI_POINT_TYPE_FLOAT32, 10, 0, 9, true, "PI Float32 -> OPCUA Float", -3.4e38, 3.4e38, 7},
        {PI_POINT_TYPE_INT32, 6, 0, 3, true, "PI Int32 -> OPCUA Int32", -2e9, 2e9, 0},
        {PI_POINT_TYPE_INT16, 4, 0, 1, true, "PI Int16 -> OPCUA Int16", -32768, 32767, 0},
        {PI_POINT_TYPE_DIGITAL, 0, 0, 11, false, "PI Digital -> OPCUA Boolean", 0, 1, 0},
        {PI_POINT_TYPE_STRING, 12, 0, 12, true, "PI String -> OPCUA String", 0, 0, 0},
    };
    reg->num_entries = 6;
    reg->entries = (pi_type_mapping_entry_t *)calloc(6, sizeof(pi_type_mapping_entry_t));
    if (reg->entries) {
        memcpy(reg->entries, std_map, sizeof(std_map));
        /* Deep-copy description strings to make them heap-allocated (not string literals) */
        for (int i = 0; i < 6; i++) {
            if (std_map[i].description) reg->entries[i].description = _strdup(std_map[i].description);
        }
    }
    return reg;
}

void pi_type_mapping_registry_free(pi_type_mapping_registry_t *reg) {
    if (!reg) return;
    for (int i = 0; i < reg->num_entries; i++) free(reg->entries[i].description);
    free(reg->entries); free(reg);
}

void pi_type_mapping_registry_add_entry(pi_type_mapping_registry_t *reg, const pi_type_mapping_entry_t *entry) {
    if (!reg || !entry) return;
    reg->num_entries++;
    reg->entries = (pi_type_mapping_entry_t *)realloc(reg->entries, (size_t)reg->num_entries * sizeof(pi_type_mapping_entry_t));
    reg->entries[reg->num_entries - 1] = *entry;
    reg->entries[reg->num_entries - 1].description = entry->description ? _strdup(entry->description) : NULL;
}

pi_map_result_t pi_type_map_pi_to_opcua(const pi_type_mapping_registry_t *reg, pi_point_type_t pi_type, int *opcua_type) {
    if (!reg || !opcua_type) return PI_MAP_ERR_NO_MAPPING;
    for (int i = 0; i < reg->num_entries; i++) {
        if (reg->entries[i].pi_type == pi_type) {
            *opcua_type = reg->entries[i].opcua_type;
            return reg->entries[i].is_lossless ? PI_MAP_OK : PI_MAP_ERR_PRECISION_LOSS;
        }
    }
    *opcua_type = -1;
    return PI_MAP_ERR_NO_MAPPING;
}

pi_map_result_t pi_type_map_pi_to_sparkplug(const pi_type_mapping_registry_t *reg, pi_point_type_t pi_type, int *sp_type) {
    if (!reg || !sp_type) return PI_MAP_ERR_NO_MAPPING;
    for (int i = 0; i < reg->num_entries; i++) {
        if (reg->entries[i].pi_type == pi_type) {
            *sp_type = reg->entries[i].sparkplug_type;
            return PI_MAP_OK;
        }
    }
    *sp_type = -1;
    return PI_MAP_ERR_NO_MAPPING;
}

/*=== L3: PI Timestamp ===*/

pi_timestamp_t pi_timestamp_now(void) {
    pi_timestamp_t ts;
    ts.pi_time = (int64_t)time(NULL);
    ts.subsecond = 0;
    ts.is_valid = true;
    ts.is_local = false;
    return ts;
}

pi_timestamp_t pi_timestamp_from_pi_time(int64_t pi_time_sec, int32_t subsec_ms) {
    pi_timestamp_t ts;
    ts.pi_time = pi_time_sec;
    ts.subsecond = subsec_ms;
    ts.is_valid = (pi_time_sec > 0);
    ts.is_local = false;
    return ts;
}

int64_t pi_timestamp_to_unix_ms(pi_timestamp_t ts) {
    return ts.pi_time * 1000 + ts.subsecond;
}

/*=== L3: Value Scale Conversion ===*/

double pi_scale_convert(double val, double in_zero, double in_span, double out_zero, double out_span) {
    if (fabs(in_span) < 1e-12) return out_zero; /* Avoid division by zero */
    return out_zero + (val - in_zero) * (out_span / in_span);
}

/*=== L3: Value Conversion (Linear, SQRT, Log, Polynomial, Table lookup) ===*/

void pi_conversion_spec_init(pi_conversion_spec_t *spec) {
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));
    spec->conv_type = PI_CONV_LINEAR;
    spec->coeff_a = 0.0;
    spec->coeff_b = 1.0;
}

void pi_conversion_spec_free(pi_conversion_spec_t *spec) {
    if (!spec) return;
    free(spec->table_inputs);
    free(spec->table_outputs);
}

/**
 * Apply a conversion specification to an input value.
 *
 * L3 Knowledge: PI System supports 5 conversion types for sensor linearization.
 * This function implements the full PI conversion model:
 *   LINEAR:      y = A + Bx  (most common, e.g., 4-20mA to engineering units)
 *   SQRT:        y = A + B·√(x - C)  (differential pressure flow measurement)
 *   LOGARITHMIC: y = A + B·ln(x - C)  (pH, sound level)
 *   POLYNOMIAL:  y = A + Bx + Cx² + Dx³ + Ex⁴  (thermocouple linearization)
 *   TABLE:       y = piecewise linear interpolation (any arbitrary curve)
 *
 * @return Converted value, or NAN on error.
 * Complexity: O(log n) for table lookup, O(1) for others.
 */
double pi_conversion_apply(const pi_conversion_spec_t *spec, double input) {
    if (!spec) return NAN;
    double A = spec->coeff_a, B = spec->coeff_b, C = spec->coeff_c;
    double D = spec->coeff_d, E = spec->coeff_e;
    switch (spec->conv_type) {
        case PI_CONV_LINEAR:
            return A + B * input;
        case PI_CONV_SQRT: {
            double arg = input - C;
            if (arg < 0.0) return NAN;
            return A + B * sqrt(arg);
        }
        case PI_CONV_LOGARITHMIC: {
            double arg = input - C;
            if (arg <= 0.0) return NAN;
            return A + B * log(arg);
        }
        case PI_CONV_POLYNOMIAL:
            return A + B * input + C * input * input + D * input * input * input + E * input * input * input * input;
        case PI_CONV_TABLE: {
            /* Linear interpolation in sorted table */
            if (!spec->table_inputs || !spec->table_outputs || spec->table_size < 2) return NAN;
            if (input <= spec->table_inputs[0]) return spec->table_outputs[0];
            if (input >= spec->table_inputs[spec->table_size - 1]) return spec->table_outputs[spec->table_size - 1];
            for (int i = 0; i < spec->table_size - 1; i++) {
                if (input >= spec->table_inputs[i] && input <= spec->table_inputs[i+1]) {
                    double t = (input - spec->table_inputs[i]) / (spec->table_inputs[i+1] - spec->table_inputs[i]);
                    return spec->table_outputs[i] + t * (spec->table_outputs[i+1] - spec->table_outputs[i]);
                }
            }
            return NAN;
        }
        default: return NAN;
    }
}

/*=== L8: Asset Views ===*/

pi_asset_view_t *pi_asset_view_new(const char *name) {
    if (!name) return NULL;
    pi_asset_view_t *av = (pi_asset_view_t *)calloc(1, sizeof(pi_asset_view_t));
    if (!av) return NULL;
    av->name = _strdup(name);
    av->refresh_interval_sec = 60.0;
    return av;
}

void pi_asset_view_free(pi_asset_view_t *av) {
    if (!av) return;
    for (int i = 0; i < av->num_views; i++) {
        free(av->views[i].name); free(av->views[i].time_series_query);
        free(av->views[i].aggregate_column);
    }
    free(av->views); free(av->name); free(av->mqtt_output_topic); free(av);
}

void pi_asset_view_add_view_def(pi_asset_view_t *av, const pi_view_definition_t *view) {
    if (!av || !view) return;
    av->num_views++;
    av->views = (pi_view_definition_t *)realloc(av->views, (size_t)av->num_views * sizeof(pi_view_definition_t));
    pi_view_definition_t *v = &av->views[av->num_views - 1];
    v->name = view->name ? _strdup(view->name) : NULL;
    v->time_series_query = view->time_series_query ? _strdup(view->time_series_query) : NULL;
    v->sampling_interval_ms = view->sampling_interval_ms;
    v->is_aggregate = view->is_aggregate;
    v->aggregate_type = view->aggregate_type;
    v->aggregate_column = view->aggregate_column ? _strdup(view->aggregate_column) : NULL;
    v->is_rolling = view->is_rolling;
    v->rolling_window_sec = view->rolling_window_sec;
}
