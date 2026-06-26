/** pi_alarm_limits.c - PI Alarm & Limit Checking (ISA-18.2) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef enum { PI_ALARM_NONE=0, PI_ALARM_HIHI=1, PI_ALARM_HI=2, PI_ALARM_LO=3, PI_ALARM_LOLO=4, PI_ALARM_RATE=5, PI_ALARM_DEVIATION=6 } pi_alarm_type_t;
static const char* alarm_type_names[] = {"NONE","HIHI","HI","LO","LOLO","RATE","DEV"};

typedef struct {
    double hihi_limit, hi_limit, lo_limit, lolo_limit;
    double rate_limit, deviation_limit;
    double hihi_deadband, hi_deadband, lo_deadband, lolo_deadband;
    int hihi_enabled, hi_enabled, lo_enabled, lolo_enabled;
    int rate_enabled, deviation_enabled;
} pi_alarm_config_t;

typedef struct {
    pi_alarm_type_t active_alarm;
    double alarm_value, alarm_limit;
    int acknowledged, shelved, suppressed;
} pi_alarm_state_t;

void pi_alarm_config_init(pi_alarm_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->hihi_limit=90.0; cfg->hi_limit=80.0;
    cfg->lo_limit=20.0; cfg->lolo_limit=10.0;
    cfg->hihi_deadband=2.0; cfg->hi_deadband=1.0;
    cfg->lo_deadband=1.0; cfg->lolo_deadband=2.0;
}
void pi_alarm_state_init(pi_alarm_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

pi_alarm_type_t pi_alarm_check(double value, const pi_alarm_config_t *cfg, pi_alarm_state_t *state) {
    if(!cfg||!state) return PI_ALARM_NONE;
    if(cfg->hihi_enabled && value > cfg->hihi_limit) {
        state->active_alarm=PI_ALARM_HIHI; state->alarm_value=value; state->alarm_limit=cfg->hihi_limit; return PI_ALARM_HIHI;
    }
    if(state->active_alarm==PI_ALARM_HIHI) {
        if(value > cfg->hihi_limit - cfg->hihi_deadband) return PI_ALARM_HIHI;
        state->active_alarm=PI_ALARM_NONE;
    }
    if(cfg->hi_enabled && value > cfg->hi_limit) {
        state->active_alarm=PI_ALARM_HI; state->alarm_value=value; state->alarm_limit=cfg->hi_limit; return PI_ALARM_HI;
    }
    if(state->active_alarm==PI_ALARM_HI && value > cfg->hi_limit - cfg->hi_deadband) return PI_ALARM_HI;
    else if(state->active_alarm==PI_ALARM_HI) state->active_alarm=PI_ALARM_NONE;
    if(cfg->lo_enabled && value < cfg->lo_limit) {
        state->active_alarm=PI_ALARM_LO; state->alarm_value=value; state->alarm_limit=cfg->lo_limit; return PI_ALARM_LO;
    }
    if(state->active_alarm==PI_ALARM_LO && value < cfg->lo_limit + cfg->lo_deadband) return PI_ALARM_LO;
    else if(state->active_alarm==PI_ALARM_LO) state->active_alarm=PI_ALARM_NONE;
    if(cfg->lolo_enabled && value < cfg->lolo_limit) {
        state->active_alarm=PI_ALARM_LOLO; state->alarm_value=value; state->alarm_limit=cfg->lolo_limit; return PI_ALARM_LOLO;
    }
    if(state->active_alarm==PI_ALARM_LOLO && value < cfg->lolo_limit + cfg->lolo_deadband) return PI_ALARM_LOLO;
    else if(state->active_alarm==PI_ALARM_LOLO) state->active_alarm=PI_ALARM_NONE;
    return PI_ALARM_NONE;
}

pi_alarm_type_t pi_alarm_check_rate(double cv, double pv, double dt, const pi_alarm_config_t *cfg, pi_alarm_state_t *state) {
    if(!cfg||!state||!cfg->rate_enabled||dt<=0.0) return PI_ALARM_NONE;
    double rate=fabs(cv-pv)/dt;
    if(rate>cfg->rate_limit) { state->active_alarm=PI_ALARM_RATE; state->alarm_value=rate; state->alarm_limit=cfg->rate_limit; return PI_ALARM_RATE; }
    return PI_ALARM_NONE;
}
void pi_alarm_acknowledge(pi_alarm_state_t *state) { if(state) state->acknowledged=1; }
void pi_alarm_shelve(pi_alarm_state_t *state, int s) { if(state) state->shelved=s; }
void pi_alarm_suppress(pi_alarm_state_t *state, int s) { if(state) state->suppressed=s; }
int pi_alarm_is_active(const pi_alarm_state_t *state) {
    if(!state||state->suppressed||state->shelved) return 0;
    return state->active_alarm!=PI_ALARM_NONE?1:0;
}
void pi_alarm_print(const pi_alarm_state_t *state) {
    if (!state) { printf("No alarm.\n"); return; }
    if (state->active_alarm == PI_ALARM_NONE) { printf("OK.\n"); return; }
    printf("ALARM %s: val=%.3f lim=%.3f ack=%d\n",
        alarm_type_names[state->active_alarm], state->alarm_value, state->alarm_limit, state->acknowledged);
}

/* ─── Hysteresis / Deadband Settings ──────────────────────────── */
void pi_alarm_set_hysteresis(pi_alarm_config_t *cfg, pi_alarm_type_t alarm, double deadband) {
    if(!cfg) return;
    switch(alarm) {
        case PI_ALARM_HIHI: cfg->hihi_deadband=deadband; break;
        case PI_ALARM_HI:   cfg->hi_deadband=deadband; break;
        case PI_ALARM_LO:   cfg->lo_deadband=deadband; break;
        case PI_ALARM_LOLO: cfg->lolo_deadband=deadband; break;
        default: break;
    }
}
double pi_alarm_get_hysteresis(const pi_alarm_config_t *cfg, pi_alarm_type_t alarm) {
    if(!cfg) return 0.0;
    switch(alarm) {
        case PI_ALARM_HIHI: return cfg->hihi_deadband;
        case PI_ALARM_HI:   return cfg->hi_deadband;
        case PI_ALARM_LO:   return cfg->lo_deadband;
        case PI_ALARM_LOLO: return cfg->lolo_deadband;
        default: return 0.0;
    }
}

/* ─── EEMUA 191 Compliance ────────────────────────────────────── */
/** Check alarm rate compliance per EEMUA 191 guidelines.
 *  EEMUA 191 recommends:
 *    - Average alarm rate < 1 per 10 minutes (steady state)
 *    - Peak alarm rate < 10 per 10 minutes (upset)
 *  Returns 1 if rate is acceptable, 0 if excessive. */
int pi_alarm_eemua191_check(double alarms_per_10min, int is_upset) {
    double limit = is_upset ? 10.0 : 1.0;
    return alarms_per_10min <= limit ? 1 : 0;
}

/* ─── Alarm Statistics ─────────────────────────────────────────── */
void pi_alarm_count_reset(int *count) { if(count) *count=0; }
int pi_alarm_check_and_count(double value, const pi_alarm_config_t *cfg, pi_alarm_state_t *state, int *alarm_count) {
    pi_alarm_type_t result = pi_alarm_check(value, cfg, state);
    if (result != PI_ALARM_NONE && alarm_count) (*alarm_count)++;
    return result;
}
