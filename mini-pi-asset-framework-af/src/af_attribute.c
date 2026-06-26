/**
 * @file af_attribute.c
 * @brief AFAttribute implementation — attribute value management
 *
 * Knowledge coverage:
 * L1: Attribute value types (Int32, Float64, String, DateTime, Boolean, Enum)
 * L2: Value get/set with type safety
 * L3: Value string conversion with localized formatting
 * L4: UOM (Unit of Measure) handling per ISA-88/95 conventions
 *
 * Reference: OSIsoft PI AF SDK Attributes Reference
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "af_attribute.h"
#include "af_element.h"

/* ─── Local UUID Generator ──────────────────────────────────── */
static void gen_uuid(char *buf, size_t sz)
{
    static uint64_t counter = 0;
    snprintf(buf, sz, "attr-%016llx-%04llx",
             (unsigned long long)(uintptr_t)buf,
             (unsigned long long)(counter++));
}

/* ─── Attribute Lifecycle ───────────────────────────────────── */
AFAttribute* af_attribute_create(const char *name, af_value_type_t type)
{
    if (!name || name[0] == '\0') return NULL;

    AFAttribute *attr = (AFAttribute*)calloc(1, sizeof(AFAttribute));
    if (!attr) return NULL;

    gen_uuid(attr->id, sizeof(attr->id));
    strncpy(attr->name, name, AF_MAX_ATTR_NAME_LEN - 1);
    attr->name[AF_MAX_ATTR_NAME_LEN - 1] = '\0';
    attr->value_type = type;
    attr->dr_type = AF_DR_NONE;
    attr->has_default = false;
    attr->is_hidden = false;
    attr->is_configuration_item = false;
    attr->owner_element = NULL;
    attr->current_value.type = type;
    attr->current_value.is_good = true;
    attr->current_value.timestamp = 0;

    /* Initialize value to reasonable default based on type */
    switch (type) {
    case AF_VAL_INT32:
        attr->current_value.value.v_int32 = 0;
        break;
    case AF_VAL_FLOAT64:
        attr->current_value.value.v_float64 = 0.0;
        break;
    case AF_VAL_STRING:
        attr->current_value.value.v_string = NULL;
        break;
    case AF_VAL_DATETIME:
        attr->current_value.value.v_datetime = 0;
        break;
    case AF_VAL_BOOLEAN:
        attr->current_value.value.v_boolean = false;
        break;
    case AF_VAL_ENUM:
        attr->current_value.value.v_enum_val = 0;
        break;
    case AF_VAL_BYTE_ARRAY:
        attr->current_value.value.v_int32 = 0;
        break;
    }

    strncpy(attr->uom, "", sizeof(attr->uom) - 1);
    return attr;
}

void af_attribute_destroy(AFAttribute *attr)
{
    if (!attr) return;

    /* Free string value if allocated */
    if (attr->current_value.type == AF_VAL_STRING &&
        attr->current_value.value.v_string) {
        free(attr->current_value.value.v_string);
        attr->current_value.value.v_string = NULL;
    }
    if (attr->default_value.type == AF_VAL_STRING &&
        attr->default_value.value.v_string) {
        free(attr->default_value.value.v_string);
        attr->default_value.value.v_string = NULL;
    }
    free(attr);
}

/* ─── Value Management ──────────────────────────────────────── */
bool af_attribute_set_value(AFAttribute *attr, const af_value_t *val)
{
    if (!attr || !val) return false;

    /* Free old string if present */
    if (attr->current_value.type == AF_VAL_STRING &&
        attr->current_value.value.v_string) {
        free(attr->current_value.value.v_string);
        attr->current_value.value.v_string = NULL;
    }

    /* Copy new value */
    attr->current_value.type = attr->value_type;
    switch (attr->value_type) {
    case AF_VAL_INT32:
        attr->current_value.value.v_int32 = val->value.v_int32;
        break;
    case AF_VAL_FLOAT64:
        attr->current_value.value.v_float64 = val->value.v_float64;
        break;
    case AF_VAL_STRING:
        if (val->value.v_string) {
            attr->current_value.value.v_string = strdup(val->value.v_string);
        }
        break;
    case AF_VAL_DATETIME:
        attr->current_value.value.v_datetime = val->value.v_datetime;
        break;
    case AF_VAL_BOOLEAN:
        attr->current_value.value.v_boolean = val->value.v_boolean;
        break;
    case AF_VAL_ENUM:
        attr->current_value.value.v_enum_val = val->value.v_enum_val;
        break;
    case AF_VAL_BYTE_ARRAY:
        attr->current_value.value.v_int32 = val->value.v_int32;
        break;
    }

    attr->current_value.is_good = val->is_good;
    attr->current_value.timestamp = val->timestamp;
    /* Copy UOM if provided */
    if (val->uom[0] != '\0') {
        strncpy(attr->current_value.uom, val->uom, AF_MAX_UOM_LEN - 1);
        attr->current_value.uom[AF_MAX_UOM_LEN - 1] = '\0';
    }
    return true;
}

const af_value_t* af_attribute_get_value(const AFAttribute *attr)
{
    if (!attr) return NULL;
    return &attr->current_value;
}

bool af_attribute_set_default(AFAttribute *attr, const af_value_t *val)
{
    if (!attr || !val) return false;

    /* Free old default string */
    if (attr->default_value.type == AF_VAL_STRING &&
        attr->default_value.value.v_string) {
        free(attr->default_value.value.v_string);
        attr->default_value.value.v_string = NULL;
    }

    attr->default_value = *val;
    attr->has_default = true;

    /* Deep copy string if needed */
    if (val->type == AF_VAL_STRING && val->value.v_string) {
        attr->default_value.value.v_string = strdup(val->value.v_string);
    }

    return true;
}

bool af_attribute_copy_value(const AFAttribute *src, AFAttribute *dst)
{
    if (!src || !dst) return false;
    return af_attribute_set_value(dst, &src->current_value);
}

/* ─── Data Reference Configuration ──────────────────────────── */
bool af_attribute_set_dr_type(AFAttribute *attr, af_data_reference_type_t dr_type)
{
    if (!attr) return false;
    attr->dr_type = dr_type;
    return true;
}

bool af_attribute_set_dr_config(AFAttribute *attr, const char *config)
{
    if (!attr || !config) return false;
    strncpy(attr->dr_config, config, AF_MAX_CONFIG_LEN - 1);
    attr->dr_config[AF_MAX_CONFIG_LEN - 1] = '\0';
    return true;
}

const char* af_attribute_get_dr_config(const AFAttribute *attr)
{
    if (!attr) return NULL;
    return attr->dr_config;
}

/* ─── UOM Management ────────────────────────────────────────── */
bool af_attribute_set_uom(AFAttribute *attr, const char *uom)
{
    if (!attr || !uom) return false;
    strncpy(attr->uom, uom, AF_MAX_UOM_LEN - 1);
    attr->uom[AF_MAX_UOM_LEN - 1] = '\0';
    return true;
}

const char* af_attribute_get_uom(const AFAttribute *attr)
{
    if (!attr) return NULL;
    return attr->uom;
}

/* ─── Value Conversion ──────────────────────────────────────── */
int af_attribute_value_to_string(const AFAttribute *attr, char *buf, size_t bufsz)
{
    if (!attr || !buf || bufsz == 0) return -1;

    const af_value_t *val = &attr->current_value;
    if (!val->is_good) {
        return snprintf(buf, bufsz, "BAD");
    }

    int n = 0;
    switch (val->type) {
    case AF_VAL_INT32:
        n = snprintf(buf, bufsz, "%d", val->value.v_int32);
        break;
    case AF_VAL_FLOAT64: {
        /* Format with up to 6 decimal places, trim trailing zeros */
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%.6f", val->value.v_float64);
        /* Trim trailing zeros */
        char *dot = strchr(tmp, '.');
        if (dot) {
            char *end = tmp + strlen(tmp) - 1;
            while (end > dot && *end == '0') end--;
            if (end == dot) end--; /* Remove dot if no fractional part */
            *(end + 1) = '\0';
        }
        n = snprintf(buf, bufsz, "%s", tmp);
        break;
    }
    case AF_VAL_STRING:
        n = snprintf(buf, bufsz, "%s",
                     val->value.v_string ? val->value.v_string : "");
        break;
    case AF_VAL_DATETIME: {
        struct tm *lt = localtime(&val->value.v_datetime);
        if (lt) {
            n = snprintf(buf, bufsz, "%04d-%02d-%02dT%02d:%02d:%02d",
                         lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                         lt->tm_hour, lt->tm_min, lt->tm_sec);
        } else {
            n = snprintf(buf, bufsz, "%lld", (long long)val->value.v_datetime);
        }
        break;
    }
    case AF_VAL_BOOLEAN:
        n = snprintf(buf, bufsz, "%s", val->value.v_boolean ? "true" : "false");
        break;
    case AF_VAL_ENUM:
        n = snprintf(buf, bufsz, "enum(%d)", val->value.v_enum_val);
        break;
    default:
        n = snprintf(buf, bufsz, "???");
        break;
    }

    return (n > 0 && (size_t)n < bufsz) ? n : -1;
}

bool af_attribute_parse_value(const AFAttribute *attr, const char *str, af_value_t *out_val)
{
    if (!attr || !str || !out_val) return false;

    out_val->type = attr->value_type;
    out_val->is_good = true;
    out_val->timestamp = 0;
    memset(out_val->uom, 0, sizeof(out_val->uom));

    switch (attr->value_type) {
    case AF_VAL_INT32:
        out_val->value.v_int32 = (int32_t)atol(str);
        return true;
    case AF_VAL_FLOAT64:
        out_val->value.v_float64 = atof(str);
        return true;
    case AF_VAL_STRING:
        out_val->value.v_string = strdup(str);
        return true;
    case AF_VAL_BOOLEAN:
        if (strcmp(str, "true") == 0 || strcmp(str, "1") == 0) {
            out_val->value.v_boolean = true;
        } else {
            out_val->value.v_boolean = false;
        }
        return true;
    case AF_VAL_ENUM:
        out_val->value.v_enum_val = (int32_t)atol(str);
        return true;
    default:
        return false;
    }
}

/* ─── Quality ───────────────────────────────────────────────── */
bool af_attribute_set_quality(AFAttribute *attr, bool is_good)
{
    if (!attr) return false;
    attr->current_value.is_good = is_good;
    return true;
}

bool af_attribute_get_quality(const AFAttribute *attr)
{
    if (!attr) return false;
    return attr->current_value.is_good;
}

/* ─── Timestamp ─────────────────────────────────────────────── */
void af_attribute_set_timestamp(AFAttribute *attr, uint64_t ts_ms)
{
    if (!attr) return;
    attr->current_value.timestamp = ts_ms;
}

uint64_t af_attribute_get_timestamp(const AFAttribute *attr)
{
    if (!attr) return 0;
    return attr->current_value.timestamp;
}

/* ─── Metadata ──────────────────────────────────────────────── */
bool af_attribute_set_hidden(AFAttribute *attr, bool hidden)
{
    if (!attr) return false;
    attr->is_hidden = hidden;
    return true;
}

bool af_attribute_is_hidden(const AFAttribute *attr)
{
    if (!attr) return false;
    return attr->is_hidden;
}

bool af_attribute_set_config_item(AFAttribute *attr, bool is_cfg)
{
    if (!attr) return false;
    attr->is_configuration_item = is_cfg;
    return true;
}

bool af_attribute_is_config_item(const AFAttribute *attr)
{
    if (!attr) return false;
    return attr->is_configuration_item;
}
