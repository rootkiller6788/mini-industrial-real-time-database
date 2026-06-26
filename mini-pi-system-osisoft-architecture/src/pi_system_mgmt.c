/**
 * pi_system_mgmt.c - PI System Management Utilities
 * Subsystem registration, license management, performance counters.
 * Knowledge: L1-L5  L7 Applications  Stanford ENGR205  RWTH Aachen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/pi_system_mgmt.h"

void pi_mgmt_init(pi_system_mgmt_t *mgmt) {
    if (!mgmt) return;
    memset(mgmt, 0, sizeof(*mgmt));
    mgmt->overall_health = 100;
    pi_timestamp_now(&mgmt->perf.server_start_time);
}
void pi_mgmt_destroy(pi_system_mgmt_t *mgmt) {
    if (!mgmt) return;
    memset(mgmt, 0, sizeof(*mgmt));
}
int pi_mgmt_register_subsystem(pi_system_mgmt_t *mgmt, const char *name, int32_t tid) {
    if (!mgmt || !name || mgmt->subsystem_count >= PI_MAX_SUBSYSTEMS) return -1;
    int i; for (i = 0; i < mgmt->subsystem_count; i++)
        if (strcmp(mgmt->subsystems[i].name, name) == 0) return -2;
    pi_subsystem_info_t *s = &mgmt->subsystems[mgmt->subsystem_count];
    memset(s, 0, sizeof(*s)); strncpy(s->name, name, 31); s->name[31] = 0;
    s->state = PI_SUBSYSTEM_STARTING; s->thread_id = tid;
    mgmt->subsystem_count++; return 0;
}
int pi_mgmt_update_subsystem_state(pi_system_mgmt_t *mgmt, const char *name, pi_subsystem_state_t st) {
    if (!mgmt || !name) return -1;
    int i; for (i = 0; i < mgmt->subsystem_count; i++)
        if (strcmp(mgmt->subsystems[i].name, name) == 0) {
            mgmt->subsystems[i].state = st;
            pi_timestamp_now(&mgmt->subsystems[i].last_heartbeat); return 0;
        }
    return -2;
}
int pi_mgmt_update_subsystem_health(pi_system_mgmt_t *mgmt, const char *name, double cpu, int64_t mem, int64_t ops, int64_t errs) {
    if (!mgmt || !name) return -1;
    int i; for (i = 0; i < mgmt->subsystem_count; i++)
        if (strcmp(mgmt->subsystems[i].name, name) == 0) {
            mgmt->subsystems[i].cpu_usage_pct = cpu;
            mgmt->subsystems[i].bytes_allocated = mem;
            mgmt->subsystems[i].operations_count = ops;
            mgmt->subsystems[i].error_count = errs; return 0;
        }
    return -2;
}
int pi_mgmt_get_subsystem(const pi_system_mgmt_t *mgmt, const char *name, pi_subsystem_info_t *out) {
    if (!mgmt || !name || !out) return -1;
    int i; for (i = 0; i < mgmt->subsystem_count; i++)
        if (strcmp(mgmt->subsystems[i].name, name) == 0) {
            memcpy(out, &mgmt->subsystems[i], sizeof(pi_subsystem_info_t)); return 0;
        }
    return -2;
}
int pi_mgmt_add_license(pi_system_mgmt_t *mgmt, const pi_license_t *lic) {
    if (!mgmt || !lic || mgmt->license_count >= PI_MAX_LICENSES) return -1;
    memcpy(&mgmt->licenses[mgmt->license_count], lic, sizeof(pi_license_t));
    mgmt->license_count++; return 0;
}
int pi_mgmt_check_license(const pi_system_mgmt_t *mgmt, pi_license_type_t type, pi_license_status_t *out) {
    if (!mgmt || !out) return -1;
    int i; for (i = 0; i < mgmt->license_count; i++)
        if (mgmt->licenses[i].type == type) { *out = mgmt->licenses[i].status; return 0; }
    *out = PI_LICENSE_STATUS_EXPIRED; return -2;
}
int pi_mgmt_is_license_valid(const pi_system_mgmt_t *mgmt, pi_license_type_t type) {
    pi_license_status_t st;
    if (pi_mgmt_check_license(mgmt, type, &st) == 0) return st == PI_LICENSE_STATUS_VALID ? 1 : 0;
    return 0;
}

int pi_mgmt_consume_license_unit(pi_system_mgmt_t *mgmt, pi_license_type_t type) {
    if (!mgmt) return -1;
    int i; for (i = 0; i < mgmt->license_count; i++)
        if (mgmt->licenses[i].type == type) {
            if (mgmt->licenses[i].used_units < mgmt->licenses[i].total_units) {
                mgmt->licenses[i].used_units++; return 0;
            }
            return -3;
        }
    return -2;
}
int pi_mgmt_release_license_unit(pi_system_mgmt_t *mgmt, pi_license_type_t type) {
    if (!mgmt) return -1;
    int i; for (i = 0; i < mgmt->license_count; i++)
        if (mgmt->licenses[i].type == type) {
            if (mgmt->licenses[i].used_units > 0) {
                mgmt->licenses[i].used_units--; return 0;
            }
            return -3;
        }
    return -2;
}
void pi_mgmt_update_perf_counters(pi_system_mgmt_t *mgmt, const pi_perf_counters_t *c) {
    if (!mgmt || !c) return;
    memcpy(&mgmt->perf, c, sizeof(pi_perf_counters_t));
    pi_timestamp_now(&mgmt->perf.last_counter_reset);
}
int pi_mgmt_get_perf_counters(const pi_system_mgmt_t *mgmt, pi_perf_counters_t *out) {
    if (!mgmt || !out) return -1;
    memcpy(out, &mgmt->perf, sizeof(pi_perf_counters_t)); return 0;
}
int pi_mgmt_calculate_overall_health(pi_system_mgmt_t *mgmt) {
    if (!mgmt) return 0;
    int running = 0, total = 0, i;
    for (i = 0; i < mgmt->subsystem_count; i++) {
        total++;
        if (mgmt->subsystems[i].state == PI_SUBSYSTEM_RUNNING) running++;
    }
    if (total == 0) { mgmt->overall_health = 0; return 0; }
    mgmt->overall_health = (running * 100) / total;
    if (mgmt->perf.cpu_usage_pct > 90.0) mgmt->overall_health -= 20;
    if (mgmt->perf.memory_usage_mb > 16000.0) mgmt->overall_health -= 10;
    if (mgmt->overall_health < 0) mgmt->overall_health = 0;
    return mgmt->overall_health;
}
void pi_mgmt_print_status(const pi_system_mgmt_t *mgmt) {
    if (!mgmt) { printf("(null)\n"); return; }
    printf("==== PI System Status ====\n");
    printf("Health: %d%%  CPU: %.1f%%  Mem: %.1f MB\n",
           mgmt->overall_health, mgmt->perf.cpu_usage_pct, mgmt->perf.memory_usage_mb);
    printf("Events/s: %.1f  Interfaces: %d  Clients: %d\n",
           mgmt->perf.event_rate_per_sec, mgmt->perf.connected_interfaces,
           mgmt->perf.connected_clients);
    printf("Subsystems:\n");
    int i;
    for (i = 0; i < mgmt->subsystem_count; i++) {
        const char *sstr = "???";
        switch (mgmt->subsystems[i].state) {
            case PI_SUBSYSTEM_RUNNING: sstr = "RUN"; break;
            case PI_SUBSYSTEM_STOPPED: sstr = "STOP"; break;
            case PI_SUBSYSTEM_DEGRADED: sstr = "DEGR"; break;
            case PI_SUBSYSTEM_STARTING: sstr = "INIT"; break;
            case PI_SUBSYSTEM_STOPPING: sstr = "EXIT"; break;
        }
        printf("  %-16s %-5s CPU:%.1f%% Ops:%lld\n",
               mgmt->subsystems[i].name, sstr,
               mgmt->subsystems[i].cpu_usage_pct,
               (long long)mgmt->subsystems[i].operations_count);
    }
    printf("=========================\n");
}

/* ─── Uptime ───────────────────────────────────────────────────── */
double pi_mgmt_uptime_seconds(const pi_system_mgmt_t *mgmt) {
    if (!mgmt) return 0.0;
    pi_timestamp_t now; pi_timestamp_now(&now);
    return pi_timestamp_diff_seconds(&mgmt->perf.server_start_time, &now);
}

int pi_mgmt_reset_counters(pi_system_mgmt_t *mgmt) {
    if (!mgmt) return -1;
    memset(&mgmt->perf, 0, sizeof(mgmt->perf));
    pi_timestamp_now(&mgmt->perf.server_start_time);
    mgmt->overall_health = 100;
    return 0;
}

/* ─── License Summary ──────────────────────────────────────────── */
int pi_mgmt_license_available(const pi_system_mgmt_t *mgmt, pi_license_type_t type) {
    if (!mgmt) return 0;
    int i;
    for (i = 0; i < mgmt->license_count; i++)
        if (mgmt->licenses[i].type == type &&
            mgmt->licenses[i].status == PI_LICENSE_STATUS_VALID)
            return mgmt->licenses[i].total_units - mgmt->licenses[i].used_units;
    return 0;
}

/* ─── Event Rate Calculation ──────────────────────────────────── */
double pi_mgmt_event_rate(const pi_system_mgmt_t *mgmt) {
    if (!mgmt) return 0.0;
    return mgmt->perf.event_rate_per_sec;
}
void pi_mgmt_set_event_rate(pi_system_mgmt_t *mgmt, double rate) {
    if (!mgmt) return;
    mgmt->perf.event_rate_per_sec = rate;
}
