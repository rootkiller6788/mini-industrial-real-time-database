/**
 * @file trigger_engine.h
 * @brief Event Frame trigger evaluation engine
 *
 * Continuously evaluates conditions that generate Event Frames.
 * Supports threshold crossing, digital state change, scheduled time,
 * expression evaluation, and composite correlation triggers.
 *
 * Knowledge Levels:
 *   L1 Definitions:  Trigger condition types, trigger evaluation context
 *   L2 Core Concepts: Edge detection, hysteresis, debouncing
 *   L3 Engineering:   Expression parser (shunting-yard), rate-of-change calc
 *   L4 Standards:     ISA-18.2 section 8 Alarm state transition detection
 *   L5 Algorithms:    Deadband filtering, Schmitt trigger thresholding
 *   L7 Application:   PI Analysis Service trigger evaluation
 *
 * MIT 6.302 - State-machine based event detection
 * Berkeley ME233 - Trigger logic for discrete event systems
 */

#ifndef TRIGGER_ENGINE_H
#define TRIGGER_ENGINE_H

#include "event_frame.h"
#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef enum {
    TRIG_STATE_IDLE       = 0,
    TRIG_STATE_ARMED      = 1,
    TRIG_STATE_FIRING     = 2,
    TRIG_STATE_COOLDOWN   = 3,
    TRIG_STATE_DISABLED   = 4
} trig_state_t;

typedef enum {
    THRESH_DIR_RISING     = 0,
    THRESH_DIR_FALLING    = 1,
    THRESH_DIR_EITHER     = 2,
    THRESH_DIR_INSIDE     = 3,
    THRESH_DIR_OUTSIDE    = 4
} thresh_direction_t;

typedef struct {
    char        tag_name[128];
    double      threshold;
    double      hysteresis;
    double      deadband;
    thresh_direction_t direction;
    double      previous_value;
    double      filtered_value;
    double      alpha;
    int         was_above;
    int         debounce_count;
    int         debounce_required;
    trig_state_t state;
    time_t      last_eval_time;
    time_t      cooldown_until;
    int         cooldown_ms;
} trig_value_change_t;

typedef struct {
    char        tag_name[128];
    int32_t     trigger_state;
    int32_t     reset_state;
    double      min_state_duration_s;
    int32_t     current_state;
    time_t      state_entered_at;
    int         trigger_fired;
    trig_state_t state;
} trig_digital_state_t;

typedef struct {
    char        schedule_expr[256];
    time_t      next_fire_time;
    time_t      last_fire_time;
    int         recurring;
    int         interval_s;
    trig_state_t state;
} trig_schedule_t;

#define TRIG_EXPR_MAX_TOKENS    64
#define TRIG_EXPR_MAX_LEN       512

typedef enum {
    EXPR_TOK_NUMBER    = 0,
    EXPR_TOK_TAG_REF   = 1,
    EXPR_TOK_OPERATOR  = 2,
    EXPR_TOK_FUNCTION  = 3,
    EXPR_TOK_LPAREN    = 4,
    EXPR_TOK_RPAREN    = 5,
    EXPR_TOK_COMMA     = 6
} expr_token_type_t;

typedef struct {
    expr_token_type_t type;
    union {
        double   number;
        char     tag_ref[128];
        char     op[4];
        char     func_name[16];
    } data;
} expr_token_t;

typedef struct {
    char            source[TRIG_EXPR_MAX_LEN];
    expr_token_t    rpn[TRIG_EXPR_MAX_TOKENS];
    int             rpn_count;
    int             compiled;
    trig_state_t    state;
    time_t          last_eval_time;
} trig_expression_t;

typedef struct {
    char            name[128];
    int             sub_trigger_count;
    int             sub_triggers_fired[16];
    int             fired_count;
    double          time_window_s;
    time_t          first_fire_time;
    trig_state_t    state;
} trig_correlation_t;

#define TRIG_MAX_VALUE_CHANGE   64
#define TRIG_MAX_DIGITAL_STATE  32
#define TRIG_MAX_SCHEDULE       16
#define TRIG_MAX_EXPRESSION     32
#define TRIG_MAX_CORRELATION    16

typedef struct {
    trig_value_change_t  value_triggers[TRIG_MAX_VALUE_CHANGE];
    int                  value_trigger_count;
    trig_digital_state_t digital_triggers[TRIG_MAX_DIGITAL_STATE];
    int                  digital_trigger_count;
    trig_schedule_t      schedule_triggers[TRIG_MAX_SCHEDULE];
    int                  schedule_trigger_count;
    trig_expression_t    expression_triggers[TRIG_MAX_EXPRESSION];
    int                  expression_trigger_count;
    trig_correlation_t   correlation_triggers[TRIG_MAX_CORRELATION];
    int                  correlation_trigger_count;
    time_t               last_scan_time;
    int                  scan_interval_ms;
    uint64_t             total_evaluations;
    uint64_t             total_triggers_fired;
} trig_engine_t;

int trig_engine_init(trig_engine_t *engine, int scan_interval_ms);
int trig_add_value_change(trig_engine_t *engine, const char *tag_name,
                          double threshold, double hysteresis, thresh_direction_t direction);
int trig_eval_value_change(trig_value_change_t *trigger, double value, time_t timestamp);
int trig_add_digital_state(trig_engine_t *engine, const char *tag_name, int32_t trigger_state);
int trig_eval_digital_state(trig_digital_state_t *trigger, int32_t state, time_t timestamp);
int trig_add_schedule(trig_engine_t *engine, const char *schedule_expr, int recurring);
int trig_eval_schedule(trig_schedule_t *trigger, time_t now);
int trig_compile_expression(trig_expression_t *expr, const char *source);
int trig_eval_expression(trig_expression_t *expr,
                         double (*get_value)(const char *tag_name), time_t timestamp);
int trig_engine_scan(trig_engine_t *engine,
                     double (*get_value)(const char *tag_name),
                     int32_t (*get_state)(const char *tag_name),
                     trig_expression_t **events, int max_events, time_t now);
int trig_engine_stats(const trig_engine_t *engine,
                      uint64_t *total_evals, uint64_t *total_fired, int *scan_interval_ms);

#endif
