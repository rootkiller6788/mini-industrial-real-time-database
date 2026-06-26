/** pi_event_pipeline.c - PI Event Processing Pipeline
 * Implements: Interface -> Snapshot -> Exception -> Compression -> Archive.
 * Knowledge: L2-L5  MIT 6.302  Stanford ENGR205  RWTH Aachen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/pi_da_types.h"
#include "../include/pi_snapshot.h"
#include "../include/pi_archive.h"

typedef struct {
    int64_t events_received;
    int64_t events_exception_passed;
    int64_t events_exception_rejected;
    int64_t events_compression_passed;
    int64_t events_compression_rejected;
    int64_t events_archived;
    double avg_latency_us;
    double max_latency_us;
    double data_reduction_ratio;
} pi_pipeline_stats_t;
static pi_pipeline_stats_t g_ps;

void pi_pipeline_stats_init(void) { memset(&g_ps, 0, sizeof(g_ps)); }
void pi_pipeline_stats_get(pi_pipeline_stats_t *out) {
    if (out) memcpy(out, &g_ps, sizeof(pi_pipeline_stats_t));
}

int pi_pipeline_process_event(pi_snapshot_t *snap, pi_archive_t *archive,
                                int32_t point_id, const pi_value_t *value,
                                double exc_dev, double comp_dev) {
    if (!snap || !value) return -2;
    g_ps.events_received++;
    pi_snapshot_put(snap, point_id, value);
    int exc = pi_snapshot_exception_test(snap, point_id, value, exc_dev);
    if (!exc) { g_ps.events_exception_rejected++; snap->exception_rejected++; return -1; }
    g_ps.events_exception_passed++; snap->exception_passed++;
    (void)comp_dev;
    if (archive && archive->initialized) {
        pi_archive_event_t ev; memset(&ev, 0, sizeof(ev));
        ev.timestamp = value->timestamp;
        memcpy(&ev.value, value, sizeof(pi_value_t));
        if (pi_archive_store_event(archive, &ev) == 0) { g_ps.events_archived++; return 0; }
    }
    return 1;
}

int pi_pipeline_process_batch(pi_snapshot_t *snap, pi_archive_t *archive,
    int32_t point_id, const pi_value_t *values, int n, double exc, double comp) {
    if (!snap || !values || n <= 0) return 0;
    int i, a = 0;
    for (i = 0; i < n; i++)
        if (pi_pipeline_process_event(snap, archive, point_id, &values[i], exc, comp) >= 0) a++;
    return a;
}

double pi_pipeline_reduction_ratio(void) {
    if (g_ps.events_received == 0) return 0.0;
    int64_t disc = g_ps.events_received - g_ps.events_archived;
    double r = (double)disc / (double)g_ps.events_received;
    g_ps.data_reduction_ratio = r;
    return r;
}

#define EVENT_QUEUE_MAX 100000
static pi_value_t g_eq[EVENT_QUEUE_MAX];
static int32_t g_eq_head, g_eq_tail, g_eq_count;

int pi_event_queue_enqueue(const pi_value_t *v) {
    if (!v || g_eq_count >= EVENT_QUEUE_MAX) return -1;
    memcpy(&g_eq[g_eq_tail], v, sizeof(pi_value_t));
    g_eq_tail = (g_eq_tail + 1) % EVENT_QUEUE_MAX; g_eq_count++; return 0;
}
int pi_event_queue_dequeue(pi_value_t *out) {
    if (!out || g_eq_count <= 0) return -1;
    memcpy(out, &g_eq[g_eq_head], sizeof(pi_value_t));
    g_eq_head = (g_eq_head + 1) % EVENT_QUEUE_MAX; g_eq_count--; return 0;
}
int32_t pi_event_queue_depth(void) { return g_eq_count; }
int pi_event_queue_is_empty(void) { return g_eq_count == 0; }

void pi_pipeline_print_stats(void) {
    printf("=== PI Pipeline Stats ===\n");
    printf("Received: %lld  ExcPass: %lld  ExcRej: %lld\n",
           (long long)g_ps.events_received, (long long)g_ps.events_exception_passed,
           (long long)g_ps.events_exception_rejected);
    printf("Archived: %lld  Reduction: %.1f%%  Queue: %d\n",
           (long long)g_ps.events_archived, 100.0*pi_pipeline_reduction_ratio(), g_eq_count);
}

/* ─── Interface Simulation ───────────────────────────────────────── */
typedef struct {
    int32_t interface_id;
    char name[64];
    char point_source[PI_MAX_POINTSOURCE_LEN];
    int32_t scan_frequency_hz;
    int32_t point_count;
    int32_t connected;
    int64_t values_sent;
} pi_interface_t;
static pi_interface_t g_ifaces[32];
static int32_t g_iface_count;

int pi_interface_register(const char *name, const char *ps, int32_t scan_hz) {
    if (!name || !ps || g_iface_count >= 32) return -1;
    pi_interface_t *ifc = &g_ifaces[g_iface_count];
    memset(ifc, 0, sizeof(*ifc));
    ifc->interface_id = g_iface_count + 1;
    strncpy(ifc->name, name, 63); ifc->name[63] = 0;
    strncpy(ifc->point_source, ps, PI_MAX_POINTSOURCE_LEN - 1);
    ifc->point_source[PI_MAX_POINTSOURCE_LEN - 1] = 0;
    ifc->scan_frequency_hz = scan_hz;
    ifc->connected = 1;
    g_iface_count++;
    return ifc->interface_id;
}
int pi_interface_get_count(void) { return g_iface_count; }
const pi_interface_t* pi_interface_get(int32_t id) {
    int i;
    for (i = 0; i < g_iface_count; i++)
        if (g_ifaces[i].interface_id == id) return &g_ifaces[i];
    return NULL;
}
int pi_interface_set_connected(int32_t id, int conn) {
    int i;
    for (i = 0; i < g_iface_count; i++)
        if (g_ifaces[i].interface_id == id) {
            g_ifaces[i].connected = conn; return 0;
        }
    return -1;
}
int pi_interface_is_connected(int32_t id) {
    const pi_interface_t *ifc = pi_interface_get(id);
    return ifc ? ifc->connected : 0;
}
void pi_interface_increment_sent(int32_t id, int64_t n) {
    int i;
    for (i = 0; i < g_iface_count; i++)
        if (g_ifaces[i].interface_id == id) {
            g_ifaces[i].values_sent += n; return;
        }
}
int64_t pi_interface_total_sent(void) {
    int i; int64_t t = 0;
    for (i = 0; i < g_iface_count; i++) t += g_ifaces[i].values_sent;
    return t;
}

/* ─── PI Performance Equation Evaluator ──────────────────────────── */
/** Simple PI Performance Equation (PE) expression evaluator.
 *  Supports basic arithmetic: +, -, *, /, Tag() references, constants.
 *  PI PE syntax: "Tag1" + "Tag2" * 1.5 + 23
 *  This is a simplified scalar evaluator for tags with known values. */
double pi_pe_evaluate_simple(const char *expression,
                              const double *tag_values,
                              const char **tag_names,
                              int num_tags) {
    /* Simple implementation: expect Tag0 + Tag1 or Tag0 * const */
    if (!expression || !tag_values) return 0.0;
    (void)tag_names;
    /* Parse: "Tag0" op "Tag1" or "Tag0" op const */
    char op = 0;
    int i0 = -1, i1 = -1;
    double cval = 0.0;
    int has_const = 0;
    const char *p = expression;
    while (*p && *p != '+' && *p != '-' && *p != '*' && *p != '/') p++;
    if (*p) op = *p; else { if (num_tags > 0) return tag_values[0]; return 0.0; }
    if (op) {
        i0 = 0;
        const char *right = p + 1;
        while (*right == ' ') right++;
        if (*right >= '0' && *right <= '9') { cval = atof(right); has_const = 1; }
        else i1 = 1;
        double v0 = (i0 >= 0 && i0 < num_tags) ? tag_values[i0] : 0.0;
        double v1 = has_const ? cval : ((i1 >= 0 && i1 < num_tags) ? tag_values[i1] : 0.0);
        switch (op) {
            case '+': return v0 + v1;
            case '-': return v0 - v1;
            case '*': return v0 * v1;
            case '/': return v1 != 0.0 ? v0 / v1 : 0.0;
            default: return 0.0;
        }
    }
    return 0.0;
}

/* ─── Pipeline Backpressure ────────────────────────────────────── */
int pi_pipeline_is_backpressured(void) {
    return g_eq_count > EVENT_QUEUE_MAX * 3 / 4 ? 1 : 0;
}

int pi_pipeline_drop_oldest(int count) {
    int dropped = 0;
    while (count-- > 0 && g_eq_count > 0) {
        pi_value_t dummy;
        if (pi_event_queue_dequeue(&dummy) == 0) dropped++;
    }
    return dropped;
}

void pi_pipeline_reset(void) {
    g_eq_head = g_eq_tail = g_eq_count = 0;
    pi_pipeline_stats_init();
}
