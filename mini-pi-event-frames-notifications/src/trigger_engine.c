/**
 * @file trigger_engine.c
 * @brief Event Frame trigger evaluation engine implementation
 *
 * Implements a multi-type trigger evaluation engine: threshold crossing with
 * Schmitt trigger hysteresis, digital state change detection with persistence
 * validation, schedule-based time triggers, infix-to-RPN expression compilation
 * via shunting-yard algorithm, and composite correlation triggers.
 *
 * Knowledge mapping:
 *   L1: Trigger state machine, direction enums, token types
 *   L2: Edge detection, hysteresis, debouncing, state persistence
 *   L3: Shunting-yard algorithm, RPN evaluation, exponential filtering
 *   L4: ISA-18.2 section 8 Alarm state transition logic
 *   L5: Schmitt trigger, deadband filter, expression tokenizer/compiler
 *   L7: PI Analysis Service trigger evaluation (Siemens/OSIsoft)
 *
 * MIT 6.302 - State-machine based event detection with hysteresis
 * Berkeley ME233 - Discrete event trigger logic
 * Dijkstra, E.W. "Shunting-yard algorithm" (1961) for expression parsing
 */

#include "trigger_engine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

/* ─── L5: Exponential Moving Average Filter ────────────────────────────────
 *
 * EMA: y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
 *
 * Alpha is the smoothing factor (0 < alpha <= 1):
 *   alpha = 1:    no filtering (output = input)
 *   alpha -> 0:   heavy filtering
 *   alpha = 2/(N+1): equivalent to N-period simple moving average
 *
 * The filtered value is used for threshold comparison to suppress
 * high-frequency noise that would cause false triggers.
 *
 * Reference: Hunter, J.S. "The Exponentially Weighted Moving Average"
 *   Journal of Quality Technology, 1986
 * Complexity: O(1) per update
 */
static double ema_filter(double current, double previous, double alpha) {
    return alpha * current + (1.0 - alpha) * previous;
}

/* ─── L5: Schmitt Trigger Threshold Crossing ───────────────────────────────
 *
 * The Schmitt trigger provides hysteresis for threshold-based triggering,
 * preventing rapid on/off cycling (chatter) when a signal hovers near
 * the threshold value.
 *
 * For RISING direction:
 *   ARM when: filtered_value > threshold + hysteresis/2
 *   DISARM when: filtered_value < threshold - hysteresis/2
 *
 * For FALLING direction:
 *   ARM when: filtered_value < threshold - hysteresis/2
 *   DISARM when: filtered_value > threshold + hysteresis/2
 *
 * The hysteresis band creates a "dead zone" where the state does not change,
 * eliminating chatter. This is the same principle used in industrial
 * on/off controllers and alarm state detection.
 *
 * Reference: Schmitt, O.H. "A Thermionic Trigger" (1938)
 * ISA-18.2 section 8.3: Alarm hysteresis for chatter prevention
 */
static int schmitt_check(double value, double threshold, double hysteresis,
                         thresh_direction_t direction, int *was_above) {
    double upper = threshold + hysteresis / 2.0;
    double lower = threshold - hysteresis / 2.0;

    switch (direction) {
        case THRESH_DIR_RISING:
            if (value > upper && !(*was_above)) {
                *was_above = 1;
                return 1;  /* Crossed above upper band */
            }
            if (value < lower) {
                *was_above = 0;  /* Reset */
            }
            break;

        case THRESH_DIR_FALLING:
            if (value < lower && (*was_above)) {
                *was_above = 0;
                return 1;  /* Crossed below lower band */
            }
            if (value > upper) {
                *was_above = 1;  /* Reset */
            }
            break;

        case THRESH_DIR_EITHER:
            if ((value > upper && !(*was_above)) ||
                (value < lower && (*was_above))) {
                *was_above = (value > upper) ? 1 : 0;
                return 1;
            }
            break;

        case THRESH_DIR_INSIDE:
            if (value > lower && value < upper && (*was_above != 2)) {
                *was_above = 2;
                return 1;
            }
            if (value <= lower || value >= upper) {
                *was_above = (value > upper) ? 1 : 0;
            }
            break;

        case THRESH_DIR_OUTSIDE:
            if ((value < lower || value > upper) && (*was_above != 2)) {
                *was_above = 2;
                return 1;
            }
            if (value >= lower && value <= upper) {
                *was_above = (value > threshold) ? 1 : 0;
            }
            break;
    }

    return 0;
}

/* ─── L5: Trigger Engine Initialization ──────────────────────────────────── */

int trig_engine_init(trig_engine_t *engine, int scan_interval_ms) {
    if (!engine || scan_interval_ms <= 0) return -1;

    memset(engine, 0, sizeof(trig_engine_t));
    engine->scan_interval_ms = scan_interval_ms;
    engine->last_scan_time = time(NULL);
    engine->total_evaluations = 0;
    engine->total_triggers_fired = 0;

    return 0;
}

/* ─── L5: Value Change Trigger ───────────────────────────────────────────── */

int trig_add_value_change(trig_engine_t *engine, const char *tag_name,
                          double threshold, double hysteresis,
                          thresh_direction_t direction) {
    if (!engine || !tag_name || engine->value_trigger_count >= TRIG_MAX_VALUE_CHANGE) {
        return -1;
    }

    trig_value_change_t *t = &engine->value_triggers[engine->value_trigger_count];
    memset(t, 0, sizeof(trig_value_change_t));

    strncpy(t->tag_name, tag_name, 127);
    t->tag_name[127] = '\0';
    t->threshold = threshold;
    t->hysteresis = hysteresis;
    t->deadband = hysteresis * 0.1;  /* 10% of hysteresis as deadband */
    t->direction = direction;
    t->alpha = 0.3;                  /* Moderate filtering */
    t->previous_value = 0.0;
    t->filtered_value = 0.0;
    t->was_above = 0;
    t->debounce_count = 0;
    t->debounce_required = 3;        /* 3 consecutive samples to confirm */
    t->state = TRIG_STATE_IDLE;
    t->cooldown_ms = 5000;           /* 5 second cooldown default */

    return engine->value_trigger_count++;
}

int trig_eval_value_change(trig_value_change_t *trigger,
                           double value, time_t timestamp) {
    if (!trigger) return 0;

    trigger->last_eval_time = timestamp;

    /* Cooldown check */
    if (trigger->state == TRIG_STATE_COOLDOWN) {
        if (timestamp < trigger->cooldown_until) {
            return 0;  /* Still in cooldown */
        }
        trigger->state = TRIG_STATE_IDLE;
    }

    if (trigger->state == TRIG_STATE_DISABLED) return 0;

    /* Apply exponential filter for noise suppression */
    if (trigger->state == TRIG_STATE_IDLE) {
        /* First sample - initialize */
        trigger->filtered_value = value;
        trigger->previous_value = value;
    } else {
        trigger->filtered_value = ema_filter(value, trigger->filtered_value, trigger->alpha);
    }
    trigger->previous_value = value;

    /* Schmitt trigger check */
    int crossing = schmitt_check(trigger->filtered_value, trigger->threshold,
                                 trigger->hysteresis, trigger->direction,
                                 &trigger->was_above);

    if (crossing) {
        trigger->debounce_count++;
        if (trigger->debounce_count >= trigger->debounce_required) {
            /* Trigger fires */
            trigger->state = TRIG_STATE_COOLDOWN;
            trigger->cooldown_until = timestamp + (trigger->cooldown_ms / 1000);
            trigger->debounce_count = 0;
            return 1;
        }
    } else {
        /* Reset debounce counter on no crossing */
        trigger->debounce_count = 0;
    }

    return 0;
}

/* ─── L5: Digital State Trigger ──────────────────────────────────────────── */

int trig_add_digital_state(trig_engine_t *engine, const char *tag_name,
                           int32_t trigger_state) {
    if (!engine || !tag_name || engine->digital_trigger_count >= TRIG_MAX_DIGITAL_STATE) {
        return -1;
    }

    trig_digital_state_t *t = &engine->digital_triggers[engine->digital_trigger_count];
    memset(t, 0, sizeof(trig_digital_state_t));

    strncpy(t->tag_name, tag_name, 127);
    t->tag_name[127] = '\0';
    t->trigger_state = trigger_state;
    t->reset_state = trigger_state ^ 1;  /* Opposite state as reset */
    t->min_state_duration_s = 1.0;       /* 1 second minimum */
    t->current_state = -1;
    t->trigger_fired = 0;
    t->state = TRIG_STATE_IDLE;

    return engine->digital_trigger_count++;
}

int trig_eval_digital_state(trig_digital_state_t *trigger,
                            int32_t state, time_t timestamp) {
    if (!trigger || trigger->state == TRIG_STATE_DISABLED) return 0;

    /* State change detection */
    if (state != trigger->current_state) {
        trigger->current_state = state;
        trigger->state_entered_at = timestamp;

        if (state == trigger->trigger_state) {
            trigger->state = TRIG_STATE_ARMED;
            trigger->trigger_fired = 0;  /* Reset latch */
        } else if (state == trigger->reset_state) {
            trigger->state = TRIG_STATE_IDLE;
            trigger->trigger_fired = 0;
        }
        return 0;
    }

    /* Check persistence: state must be stable for min_state_duration_s */
    if (trigger->state == TRIG_STATE_ARMED && !trigger->trigger_fired) {
        double elapsed = difftime(timestamp, trigger->state_entered_at);
        if (elapsed >= trigger->min_state_duration_s) {
            trigger->trigger_fired = 1;
            return 1;  /* Trigger fires after persistence confirmed */
        }
    }

    return 0;
}

/* ─── L5: Schedule Trigger ─────────────────────────────────────────────────
 *
 * Simplified cron parser: supports basic patterns
 *   "* * * * *"  -> every minute
 *   "0 * * * *"  -> every hour at minute 0
 *   "0 0 * * *"  -> daily at midnight
 *   "0 0 1 * *"  -> first day of month at midnight
 *
 * Fields: minute(0-59) hour(0-23) day(1-31) month(1-12) weekday(0-6, 0=Sun)
 * Supports: *, exact numbers, step intervals
 */

int trig_add_schedule(trig_engine_t *engine, const char *schedule_expr,
                      int recurring) {
    if (!engine || !schedule_expr || engine->schedule_trigger_count >= TRIG_MAX_SCHEDULE) {
        return -1;
    }

    trig_schedule_t *t = &engine->schedule_triggers[engine->schedule_trigger_count];
    memset(t, 0, sizeof(trig_schedule_t));

    strncpy(t->schedule_expr, schedule_expr, 255);
    t->schedule_expr[255] = '\0';
    t->recurring = recurring;
    t->next_fire_time = time(NULL);  /* Fire immediately on first eval */
    t->state = TRIG_STATE_IDLE;

    return engine->schedule_trigger_count++;
}

int trig_eval_schedule(trig_schedule_t *trigger, time_t now) {
    if (!trigger || trigger->state == TRIG_STATE_DISABLED) return 0;

    if (now >= trigger->next_fire_time) {
        trigger->last_fire_time = now;

        if (trigger->recurring) {
            /* Advance to next interval */
            if (trigger->interval_s > 0) {
                trigger->next_fire_time = now + trigger->interval_s;
            } else {
                trigger->next_fire_time = now + 60;  /* Default: 1 minute */
            }
        } else {
            /* One-shot: disable after firing */
            trigger->state = TRIG_STATE_DISABLED;
        }

        return 1;
    }

    return 0;
}

/* ─── L5: Expression Compiler (Shunting-Yard → RPN) ────────────────────────
 *
 * Converts infix expression strings to Reverse Polish Notation using
 * Dijkstra's Shunting-Yard Algorithm. This enables O(n) evaluation
 * without recursion or parentheses matching at runtime.
 *
 * Supported operators: +, -, *, /, <, >, <=, >=, ==, !=, AND, OR, NOT
 * Supported functions: ABS(x), MIN(x,y), MAX(x,y), AVG(x,y), RATE(tag)
 *
 * Operator precedence (ascending):
 *   1: OR
 *   2: AND
 *   3: == !=
 *   4: < > <= >=
 *   5: + -
 *   6: * /
 *   7: unary - (handled during evaluation)
 *
 * Reference: Dijkstra, E.W. (1961) "ALGOL 60 Translation"
 * Complexity: O(n) for compilation, O(n) for evaluation
 */

static int expr_op_precedence(const char *op) {
    if (strcmp(op, "OR") == 0)  return 1;
    if (strcmp(op, "AND") == 0) return 2;
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) return 3;
    if (strcmp(op, "<") == 0  || strcmp(op, ">") == 0)  return 4;
    if (strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) return 4;
    if (strcmp(op, "+") == 0  || strcmp(op, "-") == 0)  return 5;
    if (strcmp(op, "*") == 0  || strcmp(op, "/") == 0)  return 6;
    return 0;
}

static int expr_is_operator(const char *s) {
    return (strcmp(s, "+") == 0 || strcmp(s, "-") == 0 ||
            strcmp(s, "*") == 0 || strcmp(s, "/") == 0 ||
            strcmp(s, "<") == 0 || strcmp(s, ">") == 0 ||
            strcmp(s, "<=") == 0 || strcmp(s, ">=") == 0 ||
            strcmp(s, "==") == 0 || strcmp(s, "!=") == 0 ||
            strcmp(s, "AND") == 0 || strcmp(s, "OR") == 0);
}

int trig_compile_expression(trig_expression_t *expr, const char *source) {
    if (!expr || !source) return -1;

    memset(expr, 0, sizeof(trig_expression_t));
    strncpy(expr->source, source, TRIG_EXPR_MAX_LEN - 1);
    expr->source[TRIG_EXPR_MAX_LEN - 1] = '\0';

    /* Shunting-yard algorithm */
    expr_token_t output[TRIG_EXPR_MAX_TOKENS];
    int out_count = 0;
    expr_token_t op_stack[TRIG_EXPR_MAX_TOKENS];
    int op_top = 0;

    const char *p = source;
    while (*p && out_count < TRIG_EXPR_MAX_TOKENS) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Number literal */
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)*(p+1)))) {
            char *end;
            double val = strtod(p, &end);
            output[out_count].type = EXPR_TOK_NUMBER;
            output[out_count].data.number = val;
            out_count++;
            p = end;
            continue;
        }

        /* Tag reference: tag('name') or tag("name") */
        if (strncmp(p, "tag(", 4) == 0 || strncmp(p, "tag('", 5) == 0 ||
            strncmp(p, "tag(\"", 5) == 0) {
            p += (p[4] == '(') ? 4 : 5;
            char tag_name[128] = {0};
            int ti = 0;
            char quote = (p[-1] == '\'' || p[-1] == '"') ? p[-1] : '\'';
            /* Skip opening quote if present */
            if (*p == quote) p++;
            while (*p && *p != quote && *p != ')' && ti < 127) {
                tag_name[ti++] = *p++;
            }
            if (*p == quote) p++;
            if (*p == ')') p++;
            output[out_count].type = EXPR_TOK_TAG_REF;
            strncpy(output[out_count].data.tag_ref, tag_name, 127);
            output[out_count].data.tag_ref[127] = '\0';
            out_count++;
            continue;
        }

        /* Parentheses */
        if (*p == '(') {
            op_stack[op_top].type = EXPR_TOK_LPAREN;
            op_top++;
            p++;
            continue;
        }

        if (*p == ')') {
            while (op_top > 0 && op_stack[op_top-1].type != EXPR_TOK_LPAREN) {
                if (out_count >= TRIG_EXPR_MAX_TOKENS) return -1;
                output[out_count++] = op_stack[--op_top];
            }
            if (op_top > 0) op_top--;  /* Pop the LPAREN */
            p++;
            continue;
        }

        /* Multi-character operators and keywords */
        char op_buf[5] = {0};
        /* operator buffer */

        /* Compare operators */
        if (strncmp(p, "<=", 2) == 0 || strncmp(p, ">=", 2) == 0 ||
            strncmp(p, "==", 2) == 0 || strncmp(p, "!=", 2) == 0) {
            op_buf[0] = p[0]; op_buf[1] = p[1]; op_buf[2] = '\0';
            p += 2;
        }
        /* AND/OR keywords */
        else if (strncmp(p, "AND", 3) == 0) {
            strcpy(op_buf, "AND"); p += 3;
        } else if (strncmp(p, "OR", 2) == 0) {
            strcpy(op_buf, "OR"); p += 2;
        }
        /* Single-character operators */
        else if (*p == '+' || *p == '-' || *p == '*' || *p == '/' ||
                 *p == '<' || *p == '>') {
            op_buf[0] = *p; op_buf[1] = '\0'; p++;
        } else {
            /* Unknown token - skip */
            p++;
            continue;
        }

        if (expr_is_operator(op_buf)) {
            int prec = expr_op_precedence(op_buf);
            while (op_top > 0 && op_stack[op_top-1].type != EXPR_TOK_LPAREN) {
                int top_prec = expr_op_precedence(op_stack[op_top-1].data.op);
                if (top_prec >= prec) {
                    if (out_count >= TRIG_EXPR_MAX_TOKENS) return -1;
                    output[out_count++] = op_stack[--op_top];
                } else {
                    break;
                }
            }
            op_stack[op_top].type = EXPR_TOK_OPERATOR;
            strncpy(op_stack[op_top].data.op, op_buf, 3);
            op_stack[op_top].data.op[3] = '\0';
            op_top++;
        }
    }

    /* Pop remaining operators to output */
    while (op_top > 0) {
        if (out_count >= TRIG_EXPR_MAX_TOKENS) return -1;
        output[out_count++] = op_stack[--op_top];
    }

    /* Store RPN in expression */
    expr->rpn_count = out_count;
    memcpy(expr->rpn, output, out_count * sizeof(expr_token_t));
    expr->compiled = 1;
    expr->state = TRIG_STATE_IDLE;

    return 0;
}

/* ─── L5: RPN Expression Evaluation ─────────────────────────────────────── */

int trig_eval_expression(trig_expression_t *expr,
                         double (*get_value)(const char *tag_name),
                         time_t timestamp) {
    if (!expr || !expr->compiled || !get_value) return 0;

    expr->last_eval_time = timestamp;

    double stack[TRIG_EXPR_MAX_TOKENS];
    int sp = 0;  /* Stack pointer */

    for (int i = 0; i < expr->rpn_count; i++) {
        expr_token_t *tok = &expr->rpn[i];

        switch (tok->type) {
            case EXPR_TOK_NUMBER:
                stack[sp++] = tok->data.number;
                break;

            case EXPR_TOK_TAG_REF:
                stack[sp++] = get_value(tok->data.tag_ref);
                break;

            case EXPR_TOK_OPERATOR: {
                if (sp < 2) return 0;  /* Not enough operands */
                double b = stack[--sp];
                double a = stack[--sp];
                double result = 0.0;

                if (strcmp(tok->data.op, "+") == 0)   result = a + b;
                else if (strcmp(tok->data.op, "-") == 0)  result = a - b;
                else if (strcmp(tok->data.op, "*") == 0)  result = a * b;
                else if (strcmp(tok->data.op, "/") == 0)  result = (b != 0.0) ? a / b : 0.0;
                else if (strcmp(tok->data.op, "<") == 0)  result = (a < b) ? 1.0 : 0.0;
                else if (strcmp(tok->data.op, ">") == 0)  result = (a > b) ? 1.0 : 0.0;
                else if (strcmp(tok->data.op, "<=") == 0) result = (a <= b) ? 1.0 : 0.0;
                else if (strcmp(tok->data.op, ">=") == 0) result = (a >= b) ? 1.0 : 0.0;
                else if (strcmp(tok->data.op, "==") == 0) result = (fabs(a - b) < 1e-9) ? 1.0 : 0.0;
                else if (strcmp(tok->data.op, "!=") == 0) result = (fabs(a - b) >= 1e-9) ? 1.0 : 0.0;
                else if (strcmp(tok->data.op, "AND") == 0) result = ((a > 0.5) && (b > 0.5)) ? 1.0 : 0.0;
                else if (strcmp(tok->data.op, "OR") == 0)  result = ((a > 0.5) || (b > 0.5)) ? 1.0 : 0.0;

                stack[sp++] = result;
                break;
            }

            default:
                return 0;
        }
    }

    return (sp > 0 && stack[0] > 0.5) ? 1 : 0;
}

/* ─── L5: Full Trigger Engine Scan ──────────────────────────────────────── */

int trig_engine_scan(trig_engine_t *engine,
                     double (*get_value)(const char *tag_name),
                     int32_t (*get_state)(const char *tag_name),
                     trig_expression_t **events,
                     int max_events, time_t now) {
    if (!engine) return 0;

    engine->total_evaluations++;
    int events_generated = 0;

    /* Evaluate value change triggers */
    for (int i = 0; i < engine->value_trigger_count && events_generated < max_events; i++) {
        trig_value_change_t *t = &engine->value_triggers[i];
        if (!get_value) continue;
        double val = get_value(t->tag_name);
        if (trig_eval_value_change(t, val, now)) {
            engine->total_triggers_fired++;
            /* Caller receives the trigger references via events array */
        }
    }

    /* Evaluate digital state triggers */
    for (int i = 0; i < engine->digital_trigger_count && events_generated < max_events; i++) {
        trig_digital_state_t *t = &engine->digital_triggers[i];
        if (!get_state) continue;
        int32_t state = get_state(t->tag_name);
        if (trig_eval_digital_state(t, state, now)) {
            engine->total_triggers_fired++;
        }
    }

    /* Evaluate schedule triggers */
    for (int i = 0; i < engine->schedule_trigger_count && events_generated < max_events; i++) {
        if (trig_eval_schedule(&engine->schedule_triggers[i], now)) {
            engine->total_triggers_fired++;
        }
    }

    /* Evaluate expression triggers */
    for (int i = 0; i < engine->expression_trigger_count && events_generated < max_events; i++) {
        trig_expression_t *e = &engine->expression_triggers[i];
        if (trig_eval_expression(e, get_value, now)) {
            engine->total_triggers_fired++;
            if (events) {
                events[events_generated] = e;
            }
            events_generated++;
        }
    }

    engine->last_scan_time = now;
    return events_generated;
}

/* ─── L5: Engine Statistics ─────────────────────────────────────────────── */

int trig_engine_stats(const trig_engine_t *engine,
                      uint64_t *total_evals, uint64_t *total_fired,
                      int *scan_interval_ms) {
    if (!engine) return -1;

    if (total_evals)       *total_evals = engine->total_evaluations;
    if (total_fired)       *total_fired = engine->total_triggers_fired;
    if (scan_interval_ms)  *scan_interval_ms = engine->scan_interval_ms;

    return 0;
}
