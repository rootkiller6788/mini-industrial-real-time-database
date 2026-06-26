/**
 * @file event_frame.h
 * @brief PI Event Frame core definitions
 *
 * OSIsoft PI Asset Framework (AF) Event Frames capture meaningful process events
 * as time-bound objects with start/end times, severity, and structured attributes.
 *
 * Knowledge Levels:
 *   L1 Definitions:  EventFrame struct, EFStatus enum, EFAttribute
 *   L2 Core Concepts: Event capture, time-bounded context, parent-child nesting
 *   L3 Engineering:   Hash-indexed attribute store, ring-buffer based active set
 *   L4 Standards:     ISA-106 (Procedure Automation), ISA-18.2 (Alarm Management)
 *   L6 Canonical:     Equipment downtime event, batch start/stop, production loss
 *
 * MIT 6.302 - State-space representation of discrete events
 * Purdue ME 575 - Industrial event-driven data capture
 */

#ifndef EVENT_FRAME_H
#define EVENT_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef char ef_guid_t[37];

typedef enum {
    EF_ATTR_INT32    = 0,
    EF_ATTR_FLOAT64  = 1,
    EF_ATTR_STRING   = 2,
    EF_ATTR_BOOLEAN  = 3,
    EF_ATTR_TIMESTAMP = 4,
    EF_ATTR_GUID     = 5,
    EF_ATTR_ENUM     = 6,
    EF_ATTR_COUNT
} ef_attr_type_t;

typedef enum {
    EF_STATUS_INACTIVE   = 0,
    EF_STATUS_ACTIVE     = 1,
    EF_STATUS_CLOSED     = 2,
    EF_STATUS_ACKED      = 3,
    EF_STATUS_ARCHIVED   = 4,
    EF_STATUS_DELETED    = 5
} ef_status_t;

typedef enum {
    EF_SEVERITY_DEBUG         = 0,
    EF_SEVERITY_INFO          = 1,
    EF_SEVERITY_ADVISORY      = 2,
    EF_SEVERITY_WARNING       = 3,
    EF_SEVERITY_SIGNIFICANT   = 4,
    EF_SEVERITY_CRITICAL      = 5,
    EF_SEVERITY_EMERGENCY     = 6
} ef_severity_t;

typedef enum {
    EF_TRIGGER_NONE          = 0,
    EF_TRIGGER_VALUE_CHANGE  = 1,
    EF_TRIGGER_THRESHOLD     = 2,
    EF_TRIGGER_DIGITAL_STATE = 3,
    EF_TRIGGER_EXPRESSION    = 4,
    EF_TRIGGER_SCHEDULE      = 5,
    EF_TRIGGER_CHILD_EVENT   = 6,
    EF_TRIGGER_CORRELATION   = 7
} ef_trigger_type_t;

typedef struct {
    char        name[128];
    uint32_t    name_hash;
    ef_attr_type_t type;
    union {
        int32_t     int_val;
        double      float_val;
        int         bool_val;
        int64_t     ts_val;
        char        str_val[256];
        ef_guid_t   guid_val;
        int32_t     enum_val;
    } value;
    time_t      modified_at;
    int         is_set;
} ef_attribute_t;

#define EF_MAX_ATTRIBUTES    64
#define EF_MAX_CHILDREN      128
#define EF_MAX_NAME_LEN      256
#define EF_MAX_DESC_LEN      1024

typedef struct event_frame {
    ef_guid_t    id;
    char         name[EF_MAX_NAME_LEN];
    char         description[EF_MAX_DESC_LEN];
    char         template_name[128];
    time_t       start_time;
    time_t       end_time;
    time_t       created_at;
    time_t       modified_at;
    time_t       acknowledged_at;
    ef_status_t  status;
    ef_severity_t severity;
    ef_trigger_type_t trigger_type;
    struct event_frame *parent;
    struct event_frame *children[EF_MAX_CHILDREN];
    int           child_count;
    ef_attribute_t attributes[EF_MAX_ATTRIBUTES];
    int           attr_count;
    char          acked_by[64];
    char          ack_comment[512];
    char          source_element[256];
    char          source_tag[128];
} event_frame_t;

#define EF_ACTIVE_SET_CAPACITY  4096

typedef struct {
    event_frame_t  *frames[EF_ACTIVE_SET_CAPACITY];
    int             count;
    int             capacity;
    uint64_t        total_created;
    uint64_t        total_closed;
    uint64_t        total_acknowledged;
} ef_active_set_t;

int ef_init(event_frame_t *ef, const char *name, const char *template_name);
int ef_start(event_frame_t *ef);
int ef_close(event_frame_t *ef);
int ef_acknowledge(event_frame_t *ef, const char *user, const char *comment);
int ef_set_attribute(event_frame_t *ef, const char *name, ef_attr_type_t type, const void *value);
int ef_get_attribute(const event_frame_t *ef, const char *name, ef_attr_type_t *type, void *value);
int ef_add_child(event_frame_t *parent, event_frame_t *child);
double ef_duration_seconds(const event_frame_t *ef);
int ef_summary(const event_frame_t *ef, char *buf, size_t buf_size);
int ef_active_set_init(ef_active_set_t *set, int capacity);
int ef_active_set_add(ef_active_set_t *set, event_frame_t *ef);
int ef_active_set_remove(ef_active_set_t *set, const ef_guid_t id);
event_frame_t *ef_active_set_find(const ef_active_set_t *set, const ef_guid_t id);
int ef_active_set_find_by_template(const ef_active_set_t *set, const char *tmpl, event_frame_t **results, int max_results);
int ef_active_set_find_in_window(const ef_active_set_t *set, time_t t_start, time_t t_end, event_frame_t **results, int max_results);
int ef_active_set_stats(const ef_active_set_t *set, int *active_count, int *closed_count, double *avg_duration_s);

#endif
