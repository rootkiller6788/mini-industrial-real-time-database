/** pi_system_mgmt.h - PI System Management */
#ifndef PI_SYSTEM_MGMT_H
#define PI_SYSTEM_MGMT_H
#include "pi_da_types.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PI_MAX_SUBSYSTEMS 16
#define PI_MAX_LICENSES 32
typedef enum { PI_LICENSE_PI_SERVER=0, PI_LICENSE_PI_ACE=1, PI_LICENSE_PI_ANALYTICS=2, PI_LICENSE_PI_AF=3, PI_LICENSE_PI_NOTIFICATIONS=4, PI_LICENSE_PI_INTEGRATOR=5, PI_LICENSE_PI_VISION=6, PI_LICENSE_PI_DATALINK=7 } pi_license_type_t;
typedef enum { PI_LICENSE_STATUS_VALID=0, PI_LICENSE_STATUS_GRACE=1, PI_LICENSE_STATUS_EXPIRED=2 } pi_license_status_t;
typedef struct { pi_license_type_t type; char name[64]; int32_t total_units; int32_t used_units; pi_license_status_t status; pi_timestamp_t expiration; char serial[32]; } pi_license_t;
typedef struct { double snapshot_avg_latency_us; double archive_avg_latency_us; double event_rate_per_sec; double exception_rate_per_sec; double compression_rate_per_sec; int64_t events_received_total; int64_t events_archived_total; double cpu_usage_pct; double memory_usage_mb; double disk_usage_mb; int32_t connected_interfaces; int32_t connected_clients; pi_timestamp_t server_start_time; pi_timestamp_t last_counter_reset; } pi_perf_counters_t;
typedef struct { pi_subsystem_info_t subsystems[PI_MAX_SUBSYSTEMS]; int32_t subsystem_count; pi_license_t licenses[PI_MAX_LICENSES]; int32_t license_count; pi_perf_counters_t perf; int32_t overall_health; } pi_system_mgmt_t;
void pi_mgmt_init(pi_system_mgmt_t *mgmt);
void pi_mgmt_destroy(pi_system_mgmt_t *mgmt);
int pi_mgmt_register_subsystem(pi_system_mgmt_t *mgmt, const char *name, int32_t tid);
int pi_mgmt_update_subsystem_state(pi_system_mgmt_t *mgmt, const char *name, pi_subsystem_state_t state);
int pi_mgmt_update_subsystem_health(pi_system_mgmt_t *mgmt, const char *name, double cpu, int64_t mem, int64_t ops, int64_t errs);
int pi_mgmt_get_subsystem(const pi_system_mgmt_t *mgmt, const char *name, pi_subsystem_info_t *out);
int pi_mgmt_add_license(pi_system_mgmt_t *mgmt, const pi_license_t *lic);
int pi_mgmt_check_license(const pi_system_mgmt_t *mgmt, pi_license_type_t type, pi_license_status_t *out);
int pi_mgmt_is_license_valid(const pi_system_mgmt_t *mgmt, pi_license_type_t type);
int pi_mgmt_consume_license_unit(pi_system_mgmt_t *mgmt, pi_license_type_t type);
int pi_mgmt_release_license_unit(pi_system_mgmt_t *mgmt, pi_license_type_t type);
void pi_mgmt_update_perf_counters(pi_system_mgmt_t *mgmt, const pi_perf_counters_t *c);
int pi_mgmt_get_perf_counters(const pi_system_mgmt_t *mgmt, pi_perf_counters_t *out);
int pi_mgmt_calculate_overall_health(pi_system_mgmt_t *mgmt);
void pi_mgmt_print_status(const pi_system_mgmt_t *mgmt);
#ifdef __cplusplus
}
#endif
#endif
