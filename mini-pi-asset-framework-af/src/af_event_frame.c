/** @file af_event_frame.c - PI AF Event Frame implementation */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "af_event_frame.h"
#include "af_attribute.h"

static void gen_uuid(char *buf, size_t sz)
{
    static uint64_t c = 0;
    snprintf(buf, sz, "ef-%016llx-%04llx",
             (unsigned long long)(uintptr_t)buf, (unsigned long long)(c++));
}

af_event_frame_t* af_ef_create(const char *name)
{
    if (!name || name[0] == 0) return NULL;
    af_event_frame_t *ef = (af_event_frame_t*)calloc(1, sizeof(*ef));
    if (!ef) return NULL;
    gen_uuid(ef->id, sizeof(ef->id));
    strncpy(ef->name, name, AF_MAX_EF_NAME_LEN - 1);
    ef->name[AF_MAX_EF_NAME_LEN - 1] = 0;
    ef->status = AF_EF_STATUS_CLOSED;
    ef->severity = AF_EF_SEVERITY_INFO;
    ef->has_end_condition = false;
    ef->start_time = 0;
    ef->end_time = 0;
    ef->created_time = (uint64_t)(time(NULL) * 1000);
    return ef;
}

void af_ef_destroy(af_event_frame_t *ef)
{
    if (!ef) return;
    for (int i = 0; i < ef->captured_count; i++) {
        if (ef->captured_attr_values[i].type == AF_VAL_STRING &&
            ef->captured_attr_values[i].value.v_string) {
            free(ef->captured_attr_values[i].value.v_string);
        }
    }
    free(ef);
}

bool af_ef_set_description(af_event_frame_t *ef, const char *desc)
{
    if (!ef || !desc) return false;
    strncpy(ef->description, desc, AF_MAX_EF_DESC_LEN - 1);
    ef->description[AF_MAX_EF_DESC_LEN - 1] = 0;
    return true;
}
bool af_ef_set_start_trigger(af_event_frame_t *ef,
                               const char *attr_name, af_trigger_op_t op,
                               double threshold, double hysteresis)
{
    if (!ef || !attr_name) return false;
    strncpy(ef->start_condition.attr_name, attr_name,
            sizeof(ef->start_condition.attr_name) - 1);
    ef->start_condition.op = op;
    ef->start_condition.threshold = threshold;
    ef->start_condition.hysteresis = hysteresis;
    strncpy(ef->trigger_attr_name, attr_name,
            sizeof(ef->trigger_attr_name) - 1);
    return true;
}

bool af_ef_set_end_trigger(af_event_frame_t *ef,
                             const char *attr_name, af_trigger_op_t op,
                             double threshold, double hysteresis)
{
    if (!ef || !attr_name) return false;
    strncpy(ef->end_condition.attr_name, attr_name,
            sizeof(ef->end_condition.attr_name) - 1);
    ef->end_condition.op = op;
    ef->end_condition.threshold = threshold;
    ef->end_condition.hysteresis = hysteresis;
    ef->has_end_condition = true;
    return true;
}

bool af_ef_set_source(af_event_frame_t *ef, AFElement *element)
{
    if (!ef || !element) return false;
    ef->source_element = element;
    return true;
}

static bool eval_cond(const af_trigger_condition_t *cond,
                        const AFAttribute *attr)
{
    if (!cond || !attr) return false;
    const af_value_t *val = af_attribute_get_value(attr);
    if (!val || !val->is_good) return false;
    double v = 0.0;
    if (val->type == AF_VAL_FLOAT64) v = val->value.v_float64;
    else if (val->type == AF_VAL_INT32) v = (double)val->value.v_int32;
    else if (val->type == AF_VAL_BOOLEAN) v = val->value.v_boolean ? 1.0 : 0.0;
    else return false;
    switch (cond->op) {
    case AF_TRIGGER_GT:  return v > cond->threshold;
    case AF_TRIGGER_GE:  return v >= cond->threshold;
    case AF_TRIGGER_LT:  return v < cond->threshold;
    case AF_TRIGGER_LE:  return v <= cond->threshold;
    case AF_TRIGGER_EQ:  return v == cond->threshold;
    case AF_TRIGGER_NEQ: return v != cond->threshold;
    case AF_TRIGGER_TRUE:return v != 0.0;
    default: return false;
    }
}

bool af_ef_evaluate(af_event_frame_t *ef)
{
    if (!ef || !ef->source_element) return false;
    AFAttribute *attr = af_element_get_attribute(
        ef->source_element, ef->start_condition.attr_name);
    if (!attr) return false;
    if (ef->status == AF_EF_STATUS_CLOSED ||
        ef->status == AF_EF_STATUS_CANCELLED) {
        if (eval_cond(&ef->start_condition, attr)) {
            ef->status = AF_EF_STATUS_ACTIVE;
            ef->start_time = time(NULL);
            ef->end_time = 0;
            ef->duration_seconds = 0.0;
            return true;
        }
    } else if (ef->status == AF_EF_STATUS_ACTIVE) {
        if (ef->has_end_condition &&
            eval_cond(&ef->end_condition, attr)) {
            ef->end_time = time(NULL);
            ef->duration_seconds = difftime(ef->end_time, ef->start_time);
            ef->status = AF_EF_STATUS_CLOSED;
            return true;
        }
    }
    return false;
}

bool af_ef_close(af_event_frame_t *ef, time_t end_t)
{
    if (!ef || ef->status != AF_EF_STATUS_ACTIVE) return false;
    ef->end_time = end_t ? end_t : time(NULL);
    ef->duration_seconds = difftime(ef->end_time, ef->start_time);
    ef->status = AF_EF_STATUS_CLOSED;
    return true;
}

bool af_ef_acknowledge(af_event_frame_t *ef, const char *ack_by)
{
    if (!ef || ef->status != AF_EF_STATUS_CLOSED) return false;
    ef->status = AF_EF_STATUS_ACKNOWLEDGED;
    ef->acknowledged_time = time(NULL);
    if (ack_by) {
        strncpy(ef->acknowledged_by, ack_by,
                sizeof(ef->acknowledged_by) - 1);
        ef->acknowledged_by[sizeof(ef->acknowledged_by) - 1] = 0;
    }
    return true;
}

void af_ef_set_severity(af_event_frame_t *ef, af_ef_severity_t sev)
{ if (ef) ef->severity = sev; }

af_ef_severity_t af_ef_get_severity(const af_event_frame_t *ef)
{ return ef ? ef->severity : AF_EF_SEVERITY_INFO; }

int af_ef_capture_attributes(af_event_frame_t *ef,
                               const char *attr_names[], int name_count)
{
    if (!ef || !attr_names || name_count <= 0) return 0;
    if (!ef->source_element) return 0;
    int captured = 0;
    for (int i = 0; i < name_count && ef->captured_count < AF_MAX_EF_ATTRS; i++) {
        AFAttribute *attr = af_element_get_attribute(ef->source_element, attr_names[i]);
        if (!attr) continue;
        int ci = ef->captured_count;
        strncpy(ef->captured_attr_names[ci], attr_names[i], AF_MAX_ATTR_NAME_LEN - 1);
        ef->captured_attr_values[ci] = attr->current_value;
        if (attr->current_value.type == AF_VAL_STRING &&
            attr->current_value.value.v_string) {
            ef->captured_attr_values[ci].value.v_string =
                strdup(attr->current_value.value.v_string);
        }
        ef->captured_count++;
        captured++;
    }
    return captured;
}

const af_value_t* af_ef_get_captured(const af_event_frame_t *ef,
                                       const char *attr_name)
{
    if (!ef || !attr_name) return NULL;
    for (int i = 0; i < ef->captured_count; i++)
        if (strcmp(ef->captured_attr_names[i], attr_name) == 0)
            return &ef->captured_attr_values[i];
    return NULL;
}

double af_ef_get_duration(const af_event_frame_t *ef)
{
    if (!ef || ef->end_time == 0) return -1.0;
    return ef->duration_seconds;
}

const char* af_ef_status_string(af_ef_status_t status)
{
    switch (status) {
    case AF_EF_STATUS_ACTIVE:       return "ACTIVE";
    case AF_EF_STATUS_CLOSED:       return "CLOSED";
    case AF_EF_STATUS_ACKNOWLEDGED: return "ACKNOWLEDGED";
    case AF_EF_STATUS_CANCELLED:    return "CANCELLED";
    default: return "UNKNOWN";
    }
}

const char* af_ef_severity_string(af_ef_severity_t sev)
{
    switch (sev) {
    case AF_EF_SEVERITY_DEBUG:    return "DEBUG";
    case AF_EF_SEVERITY_INFO:     return "INFO";
    case AF_EF_SEVERITY_WARNING:  return "WARNING";
    case AF_EF_SEVERITY_MINOR:    return "MINOR";
    case AF_EF_SEVERITY_MAJOR:    return "MAJOR";
    case AF_EF_SEVERITY_CRITICAL: return "CRITICAL";
    default: return "UNKNOWN";
    }
}

bool af_ef_is_active(const af_event_frame_t *ef)
{ return ef && ef->status == AF_EF_STATUS_ACTIVE; }

bool af_ef_add_note(af_event_frame_t *ef, const char *note)
{
    if (!ef || !note) return false;
    size_t ex = strlen(ef->notes);
    if (ex + strlen(note) + 3 >= sizeof(ef->notes)) return false;
    if (ex > 0) strncat(ef->notes, "; ", sizeof(ef->notes) - ex - 1);
    strncat(ef->notes, note, sizeof(ef->notes) - strlen(ef->notes) - 1);
    return true;
}
