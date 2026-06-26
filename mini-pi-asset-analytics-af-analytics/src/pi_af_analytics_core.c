/**
 * @file src/pi_af_analytics_core.c
 * @brief PI AF Analytics Core Engine — Scheduling, Registration, Execution Loop
 *
 * Implements the central analytics engine that mirrors the OSIsoft PI AF
 * Analytics Service. Handles analytic registration, priority-queue-based
 * scheduling, dependency resolution via Kahn's algorithm, and the main
 * execution loop.
 *
 * Knowledge Coverage: L1 (Definitions), L2 (Scheduling Concepts),
 *                     L3 (Priority Queue, Hash Table),
 *                     L5 (Topological Sort — Kahn's Algorithm)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pi_af_analytics_core.h"

/* --------------------------------------------------------------------------
 * Helpers: String tables
 * ------------------------------------------------------------------------*/

const char *pi_af_error_string(pi_af_error_t err) {
    switch (err) {
        case PI_AF_OK:                      return "OK";
        case PI_AF_ERR_NOT_FOUND:           return "Not found";
        case PI_AF_ERR_EXPRESSION_SYNTAX:   return "Expression syntax error";
        case PI_AF_ERR_EXPRESSION_EVAL:     return "Expression evaluation error";
        case PI_AF_ERR_DIVIDE_BY_ZERO:      return "Division by zero";
        case PI_AF_ERR_CIRCULAR_DEPENDENCY: return "Circular dependency detected";
        case PI_AF_ERR_SCHEDULE_FULL:       return "Schedule queue full";
        case PI_AF_ERR_TIMEOUT:             return "Calculation timeout";
        case PI_AF_ERR_DATA_QUALITY:        return "Data quality below threshold";
        case PI_AF_ERR_BAD_TIMESTAMP:       return "Bad or out-of-order timestamp";
        case PI_AF_ERR_OUTPUT_FAILED:       return "Failed to write output";
        case PI_AF_ERR_OUT_OF_MEMORY:       return "Out of memory";
        case PI_AF_ERR_INVALID_ARGUMENT:    return "Invalid argument";
        case PI_AF_ERR_OVERFLOW:            return "Numeric overflow";
        case PI_AF_ERR_NOT_INITIALIZED:     return "Engine not initialized";
        case PI_AF_ERR_DUPLICATE_ID:        return "Duplicate analytic ID";
        default:                            return "Unknown error";
    }
}

const char *pi_af_schedule_type_string(pi_af_schedule_type_t t) {
    switch (t) {
        case PI_AF_SCHEDULE_PERIODIC:       return "Periodic";
        case PI_AF_SCHEDULE_EVENT_TRIGGERED: return "Event-Triggered";
        case PI_AF_SCHEDULE_NATURAL:        return "Natural";
        case PI_AF_SCHEDULE_ON_DEMAND:      return "On-Demand";
        default:                            return "Unknown";
    }
}

const char *pi_af_calc_status_string(pi_af_calc_status_t s) {
    switch (s) {
        case PI_AF_CALC_STATUS_IDLE:     return "Idle";
        case PI_AF_CALC_STATUS_QUEUED:   return "Queued";
        case PI_AF_CALC_STATUS_RUNNING:  return "Running";
        case PI_AF_CALC_STATUS_COMPLETE: return "Complete";
        case PI_AF_CALC_STATUS_ERROR:    return "Error";
        case PI_AF_CALC_STATUS_SKIPPED:  return "Skipped";
        default:                         return "Unknown";
    }
}

/* --------------------------------------------------------------------------
 * L5: Priority Queue (Min-Heap) for Scheduling
 * ------------------------------------------------------------------------*/

/**
 * @brief Min-heap operations for pi_af_schedule_entry_t.
 *
 * The schedule queue is a binary min-heap ordered by scheduled_time,
 * with priority as a tie-breaker.
 */

static int heap_compare(const pi_af_schedule_entry_t *a,
                        const pi_af_schedule_entry_t *b) {
    if (a->scheduled_time < b->scheduled_time) return -1;
    if (a->scheduled_time > b->scheduled_time) return 1;
    /* Tie-break: higher priority first (so negate) */
    if (a->priority > b->priority) return -1;
    if (a->priority < b->priority) return 1;
    return 0;
}

/**
 * @brief Sift up to maintain min-heap invariant after insertion.
 *
 * @param heap  The heap array
 * @param idx   Index of the newly inserted element
 *
 * Complexity: O(log n)
 */
static void heap_sift_up(pi_af_schedule_entry_t *heap, uint32_t idx) {
    while (idx > 0) {
        uint32_t parent = (idx - 1) / 2;
        if (heap_compare(&heap[idx], &heap[parent]) >= 0) break;
        /* Swap */
        pi_af_schedule_entry_t tmp = heap[idx];
        heap[idx] = heap[parent];
        heap[parent] = tmp;
        idx = parent;
    }
}

/**
 * @brief Sift down to restore min-heap invariant after removal.
 *
 * @param heap  The heap array
 * @param size  Current heap size
 * @param idx   Index to sift down from
 *
 * Complexity: O(log n)
 */
static void heap_sift_down(pi_af_schedule_entry_t *heap, uint32_t size,
                            uint32_t idx) {
    while (1) {
        uint32_t left  = 2 * idx + 1;
        uint32_t right = 2 * idx + 2;
        uint32_t smallest = idx;

        if (left  < size && heap_compare(&heap[left],  &heap[smallest]) < 0)
            smallest = left;
        if (right < size && heap_compare(&heap[right], &heap[smallest]) < 0)
            smallest = right;

        if (smallest == idx) break;

        pi_af_schedule_entry_t tmp = heap[idx];
        heap[idx] = heap[smallest];
        heap[smallest] = tmp;
        idx = smallest;
    }
}

/* --------------------------------------------------------------------------
 * Engine Lifecycle
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_init(pi_af_analytics_context_t *ctx, uint32_t queue_cap) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;
    if (queue_cap == 0 || queue_cap > PI_AF_MAX_ANALYTICS) {
        queue_cap = PI_AF_MAX_ANALYTICS;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->schedule_queue = (pi_af_schedule_entry_t *)
        calloc(queue_cap, sizeof(pi_af_schedule_entry_t));
    if (!ctx->schedule_queue) return PI_AF_ERR_OUT_OF_MEMORY;

    /* Allocate dependency graph adjacency matrix */
    uint32_t N = PI_AF_MAX_ANALYTICS;
    ctx->dependency_graph = (uint32_t *)calloc(N * N, sizeof(uint32_t));
    if (!ctx->dependency_graph) {
        free(ctx->schedule_queue);
        return PI_AF_ERR_OUT_OF_MEMORY;
    }

    ctx->execution_order = (uint32_t *)calloc(N, sizeof(uint32_t));
    if (!ctx->execution_order) {
        free(ctx->schedule_queue);
        free(ctx->dependency_graph);
        return PI_AF_ERR_OUT_OF_MEMORY;
    }
    ctx->execution_order_count = 0;

    ctx->queue_capacity = queue_cap;
    ctx->queue_size = 0;
    ctx->engine_running = false;
    ctx->engine_current_time = time(NULL);
    ctx->order_valid = false;
    ctx->max_concurrent = 1;
    ctx->min_period_seconds = 1.0;
    ctx->strict_timing = true;
    ctx->skip_on_error = true;

    return PI_AF_OK;
}

pi_af_error_t pi_af_shutdown(pi_af_analytics_context_t *ctx) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    ctx->engine_running = false;
    free(ctx->schedule_queue);
    free(ctx->dependency_graph);
    free(ctx->execution_order);
    ctx->schedule_queue = NULL;
    ctx->dependency_graph = NULL;
    ctx->execution_order = NULL;
    ctx->queue_capacity = 0;
    ctx->queue_size = 0;

    memset(ctx, 0, sizeof(*ctx));
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Analytic Registration
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_register_analytic(pi_af_analytics_context_t *ctx,
                                       const pi_af_analytic_t *analytic,
                                       uint32_t *out_id) {
    if (!ctx || !analytic) return PI_AF_ERR_INVALID_ARGUMENT;
    if (analytic->analytic_id != 0) {
        /* Check for duplicate ID */
        for (uint32_t i = 0; i < ctx->analytics_count; i++) {
            if (ctx->analytics_in_use[i] &&
                ctx->analytics[i].analytic_id == analytic->analytic_id) {
                return PI_AF_ERR_DUPLICATE_ID;
            }
        }
    }

    /* Find a free slot */
    uint32_t slot = (uint32_t)(-1);
    for (uint32_t i = 0; i < PI_AF_MAX_ANALYTICS; i++) {
        if (!ctx->analytics_in_use[i]) {
            slot = i;
            break;
        }
    }
    if (slot == (uint32_t)(-1)) {
        return PI_AF_ERR_OUT_OF_MEMORY; /* No slots available */
    }

    /* Copy the analytic definition */
    memcpy(&ctx->analytics[slot], analytic, sizeof(pi_af_analytic_t));

    /* Assign ID if not provided */
    if (ctx->analytics[slot].analytic_id == 0) {
        uint32_t max_id = 0;
        for (uint32_t i = 0; i < PI_AF_MAX_ANALYTICS; i++) {
            if (ctx->analytics_in_use[i] &&
                ctx->analytics[i].analytic_id > max_id) {
                max_id = ctx->analytics[i].analytic_id;
            }
        }
        ctx->analytics[slot].analytic_id = max_id + 1;
    }

    ctx->analytics[slot].last_status = PI_AF_CALC_STATUS_IDLE;
    ctx->analytics[slot].last_execution = 0;
    ctx->analytics[slot].execution_count = 0;
    ctx->analytics[slot].error_count = 0;
    ctx->analytics[slot].last_error[0] = '\0';

    ctx->analytics_in_use[slot] = true;
    ctx->analytics_count++;
    ctx->order_valid = false; /* New analytic invalidates execution order */

    if (out_id) *out_id = ctx->analytics[slot].analytic_id;
    return PI_AF_OK;
}

pi_af_error_t pi_af_unregister_analytic(pi_af_analytics_context_t *ctx,
                                         uint32_t analytic_id) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < PI_AF_MAX_ANALYTICS; i++) {
        if (ctx->analytics_in_use[i] &&
            ctx->analytics[i].analytic_id == analytic_id) {

            /* Remove pending schedule entries for this analytic */
            uint32_t write = 0;
            for (uint32_t j = 0; j < ctx->queue_size; j++) {
                if (ctx->schedule_queue[j].analytic_id != analytic_id) {
                    if (write != j) {
                        ctx->schedule_queue[write] = ctx->schedule_queue[j];
                    }
                    write++;
                }
            }
            ctx->queue_size = write;
            /* Re-heapify */
            if (ctx->queue_size > 1) {
                for (int32_t k = (int32_t)(ctx->queue_size / 2) - 1; k >= 0; k--) {
                    heap_sift_down(ctx->schedule_queue, ctx->queue_size,
                                   (uint32_t)k);
                }
            }

            /* Clear dependency references from other analytics */
            for (uint32_t a = 0; a < PI_AF_MAX_ANALYTICS; a++) {
                if (!ctx->analytics_in_use[a]) continue;
                pi_af_analytic_t *other = &ctx->analytics[a];
                uint32_t new_dep_count = 0;
                for (uint32_t d = 0; d < other->dependency_count; d++) {
                    if (other->dependency_ids[d] != analytic_id) {
                        other->dependency_ids[new_dep_count++] =
                            other->dependency_ids[d];
                    }
                }
                other->dependency_count = new_dep_count;
            }

            /* Clear adjacency matrix rows/cols */
            {
                uint32_t N = PI_AF_MAX_ANALYTICS;
                for (uint32_t r = 0; r < N; r++) {
                    ctx->dependency_graph[i * N + r] = 0;
                    ctx->dependency_graph[r * N + i] = 0;
                }
            }

            ctx->analytics_in_use[i] = false;
            ctx->analytics_count--;
            ctx->order_valid = false;

            memset(&ctx->analytics[i], 0, sizeof(pi_af_analytic_t));
            return PI_AF_OK;
        }
    }
    return PI_AF_ERR_NOT_FOUND;
}

pi_af_error_t pi_af_set_analytic_enabled(pi_af_analytics_context_t *ctx,
                                          uint32_t analytic_id, bool enabled) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    pi_af_analytic_t *a = pi_af_get_analytic(ctx, analytic_id);
    if (!a) return PI_AF_ERR_NOT_FOUND;

    a->enabled = enabled;
    return PI_AF_OK;
}

pi_af_analytic_t *pi_af_get_analytic(pi_af_analytics_context_t *ctx,
                                      uint32_t analytic_id) {
    if (!ctx) return NULL;
    for (uint32_t i = 0; i < PI_AF_MAX_ANALYTICS; i++) {
        if (ctx->analytics_in_use[i] &&
            ctx->analytics[i].analytic_id == analytic_id) {
            return &ctx->analytics[i];
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Scheduling
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_schedule_analytic(pi_af_analytics_context_t *ctx,
                                       uint32_t analytic_id, time_t trigger_ts) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;
    if (ctx->queue_size >= ctx->queue_capacity) {
        return PI_AF_ERR_SCHEDULE_FULL;
    }

    pi_af_analytic_t *a = pi_af_get_analytic(ctx, analytic_id);
    if (!a) return PI_AF_ERR_NOT_FOUND;
    if (!a->enabled) return PI_AF_OK; /* Silently skip disabled */

    pi_af_schedule_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.analytic_id = analytic_id;
    entry.enqueue_time = ctx->engine_current_time;

    /* Compute scheduled time based on schedule type */
    switch (a->schedule_type) {
        case PI_AF_SCHEDULE_PERIODIC: {
            if (trigger_ts != 0) {
                entry.scheduled_time = trigger_ts;
            } else if (a->last_execution == 0) {
                entry.scheduled_time = ctx->engine_current_time;
            } else {
                entry.scheduled_time = (time_t)((double)a->last_execution
                    + a->period_seconds);
            }
            entry.priority = 0;
            snprintf(entry.trigger_reason, sizeof(entry.trigger_reason),
                     "Periodic: %.1fs", a->period_seconds);
            break;
        }
        case PI_AF_SCHEDULE_EVENT_TRIGGERED:
            entry.scheduled_time = (trigger_ts != 0) ? trigger_ts
                                                      : ctx->engine_current_time;
            entry.priority = 5; /* Higher priority than periodic */
            snprintf(entry.trigger_reason, sizeof(entry.trigger_reason),
                     "Event: %s", a->trigger_event_frame);
            break;
        case PI_AF_SCHEDULE_NATURAL:
            entry.scheduled_time = ctx->engine_current_time;
            entry.priority = 10; /* Highest priority — react immediately */
            snprintf(entry.trigger_reason, sizeof(entry.trigger_reason),
                     "Natural: input changed");
            break;
        case PI_AF_SCHEDULE_ON_DEMAND:
            entry.scheduled_time = (trigger_ts != 0) ? trigger_ts
                                                      : ctx->engine_current_time;
            entry.priority = 8;
            snprintf(entry.trigger_reason, sizeof(entry.trigger_reason),
                     "On-Demand");
            break;
        default:
            return PI_AF_ERR_INVALID_ARGUMENT;
    }

    /* Insert into min-heap */
    ctx->schedule_queue[ctx->queue_size] = entry;
    ctx->queue_size++;
    heap_sift_up(ctx->schedule_queue, ctx->queue_size - 1);

    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Execution Loop
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_process_next(pi_af_analytics_context_t *ctx) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;
    if (ctx->queue_size == 0) return PI_AF_OK; /* Nothing to do */

    /* Pop the next entry from the min-heap */
    pi_af_schedule_entry_t entry = ctx->schedule_queue[0];
    ctx->queue_size--;
    if (ctx->queue_size > 0) {
        ctx->schedule_queue[0] = ctx->schedule_queue[ctx->queue_size];
        heap_sift_down(ctx->schedule_queue, ctx->queue_size, 0);
    }

    pi_af_analytic_t *a = pi_af_get_analytic(ctx, entry.analytic_id);
    if (!a) return PI_AF_ERR_NOT_FOUND;
    if (!a->enabled) return PI_AF_OK;

    /* Update engine current time to the scheduled time */
    ctx->engine_current_time = entry.scheduled_time;

    /* Mark as running */
    a->last_status = PI_AF_CALC_STATUS_RUNNING;

    /* For a real implementation, we would evaluate the expression here.
     * In this engine, expression evaluation is delegated to the expression
     * module. We simulate a successful evaluation by incrementing counters.
     * The expression evaluation result would come from pi_af_expression_evaluate().
     *
     * For now, we flag as complete if the expression is not empty.
     */
    if (a->expression[0] != '\0') {
        a->last_status = PI_AF_CALC_STATUS_COMPLETE;
        a->last_execution = entry.scheduled_time;
        a->execution_count++;
        ctx->total_calculations++;

        /* Re-schedule periodic analytics */
        if (a->schedule_type == PI_AF_SCHEDULE_PERIODIC) {
            pi_af_schedule_analytic(ctx, entry.analytic_id, 0);
        }
    } else {
        a->last_status = PI_AF_CALC_STATUS_ERROR;
        a->error_count++;
        ctx->total_errors++;
        snprintf(a->last_error, sizeof(a->last_error),
                 "Empty expression — nothing to evaluate");
    }

    return PI_AF_OK;
}

pi_af_error_t pi_af_run(pi_af_analytics_context_t *ctx,
                         uint32_t max_calcs, time_t until_time,
                         uint32_t *out_processed) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    ctx->engine_running = true;
    ctx->engine_start_time = ctx->engine_current_time;
    uint32_t processed = 0;

    while (ctx->queue_size > 0) {
        /* Check limits */
        if (max_calcs > 0 && processed >= max_calcs) break;
        if (until_time > 0 && ctx->schedule_queue[0].scheduled_time > until_time)
            break;

        pi_af_error_t ret = pi_af_process_next(ctx);
        if (ret == PI_AF_OK) {
            processed++;
        } else if (ret != 0) {
            /* Stop on error (non-zero and not "nothing to do") */
            ctx->engine_running = false;
            if (out_processed) *out_processed = processed;
            return ret;
        }
    }

    ctx->engine_running = false;
    if (out_processed) *out_processed = processed;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Topological Sort — Kahn's Algorithm
 * ------------------------------------------------------------------------*/

/**
 * @brief Kahn's algorithm for topological sorting of the dependency graph.
 *
 * This is the standard BFS-based topological sort that handles directed
 * acyclic graphs (DAGs). It detects cycles by checking if all nodes
 * were processed.
 *
 * Algorithm:
 *   1. Compute in-degree for each node (incoming dependency edges).
 *   2. Enqueue all nodes with in-degree 0.
 *   3. Dequeue a node, append to result; decrement in-degree of all neighbors.
 *   4. If a neighbor's in-degree becomes 0, enqueue it.
 *   5. If any node remains unprocessed → cycle detected.
 *
 * @see Kahn, A.B. (1962) "Topological sorting of large networks",
 *      Communications of the ACM, 5(11), 558-562.
 */
pi_af_error_t pi_af_build_execution_order(pi_af_analytics_context_t *ctx) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    uint32_t N = 0;
    uint32_t node_map[PI_AF_MAX_ANALYTICS]; /* slot → logical index */
    uint32_t reverse_map[PI_AF_MAX_ANALYTICS]; /* logical index → slot */
    memset(node_map, 0, sizeof(node_map));
    memset(reverse_map, 0, sizeof(reverse_map));

    /* Map active analytics to contiguous logical indices */
    for (uint32_t i = 0; i < PI_AF_MAX_ANALYTICS; i++) {
        if (ctx->analytics_in_use[i]) {
            node_map[i] = N;
            reverse_map[N] = i;
            N++;
        }
    }

    if (N == 0) {
        ctx->order_valid = true;
        return PI_AF_OK;
    }

    /* Compute in-degree for each node */
    uint32_t in_degree[PI_AF_MAX_ANALYTICS];
    memset(in_degree, 0, N * sizeof(uint32_t));

    for (uint32_t u = 0; u < N; u++) {
        pi_af_analytic_t *a = &ctx->analytics[reverse_map[u]];
        for (uint32_t d = 0; d < a->dependency_count; d++) {
            /* Find logical index of dependency */
            pi_af_analytic_t *dep = pi_af_get_analytic(ctx, a->dependency_ids[d]);
            if (dep) {
                for (uint32_t k = 0; k < PI_AF_MAX_ANALYTICS; k++) {
                    if (ctx->analytics_in_use[k] &&
                        ctx->analytics[k].analytic_id == dep->analytic_id) {
                        /* a depends on dep → a's in-degree goes up */
                        in_degree[u]++;
                        /* Edge from dep to a: dep → a */
                        ctx->dependency_graph[k * N + reverse_map[u]] = 1;
                        break;
                    }
                }
            }
        }
    }

    /* Enqueue all nodes with in-degree 0 */
    uint32_t queue[PI_AF_MAX_ANALYTICS];
    uint32_t qh = 0, qt = 0;
    for (uint32_t u = 0; u < N; u++) {
        if (in_degree[u] == 0) {
            queue[qt++] = u;
        }
    }

    /* Process queue */
    uint32_t order_idx = 0;
    while (qh < qt) {
        uint32_t u = queue[qh++];
        ctx->execution_order[order_idx++] = u;

        /* Decrement in-degree of all vertices that depend on u */
        for (uint32_t v = 0; v < N; v++) {
            if (ctx->dependency_graph[reverse_map[u] * N + reverse_map[v]]) {
                in_degree[v]--;
                if (in_degree[v] == 0) {
                    queue[qt++] = v;
                }
            }
        }
    }

    if (order_idx != N) {
        /* Cycle detected — not all nodes were processed */
        return PI_AF_ERR_CIRCULAR_DEPENDENCY;
    }

    ctx->execution_order_count = order_idx;
    ctx->order_valid = true;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Time Range Operations
 * ------------------------------------------------------------------------*/

/**
 * @brief Check if two closed time intervals [start1, end1] and [start2, end2]
 *        intersect.
 *
 * Intervals intersect iff: start1 ≤ end2 AND start2 ≤ end1
 *
 * This is the fundamental geometric interval overlap test, used extensively
 * in PI AF Analytics for determining if event-frame analytics should execute.
 */
bool pi_af_time_range_overlaps(time_t start1, time_t end1,
                                time_t start2, time_t end2) {
    /* Guard against inverted intervals */
    if (start1 > end1 || start2 > end2) return false;
    return (start1 <= end2) && (start2 <= end1);
}

/**
 * @brief Compute the intersection of two time intervals.
 *
 * The intersection is: [max(start1, start2), min(end1, end2)]
 *
 * Returns false if the intersection is empty.
 */
bool pi_af_time_range_intersection(time_t start1, time_t end1,
                                    time_t start2, time_t end2,
                                    time_t *out_start, time_t *out_end) {
    if (!out_start || !out_end) return false;
    if (start1 > end1 || start2 > end2) return false;

    *out_start = (start1 > start2) ? start1 : start2;
    *out_end   = (end1   < end2)   ? end1   : end2;
    return (*out_start <= *out_end);
}
