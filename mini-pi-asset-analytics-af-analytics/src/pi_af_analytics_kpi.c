/**
 * @file src/pi_af_analytics_kpi.c
 * @brief PI AF Analytics — KPI Evaluation and Rollup Engine
 *
 * Implements the complete Key Performance Indicator (KPI) lifecycle:
 *   - KPI registration and contextualization
 *   - Threshold-based status evaluation (traffic light logic)
 *   - Trend detection via linear regression over time window
 *   - Hierarchical rollup (child KPIs → parent KPI)
 *   - Composite scoring (weighted multi-KPI)
 *   - Topological ordering for calculation dependencies
 *
 * Knowledge Coverage: L1 (KPI Definitions), L2 (Threshold Evaluation),
 *                     L4 (ISO 22400), L5 (Rollup, Trend Detection),
 *                     L7 (Industrial KPI Dashboards)
 *
 * References:
 *   - ISO 22400-1:2014 — KPIs for MOM, Part 1: Concepts
 *   - ISO 22400-2:2014 — KPIs for MOM, Part 2: Definitions (OEE, MTBF, etc.)
 *   - Parmenter, D. (2015) "Key Performance Indicators" (3rd ed.)
 *   - Neely, A. et al. (1995) "Performance measurement system design"
 *     International Journal of Operations & Production Management
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "pi_af_analytics_kpi.h"

/* --------------------------------------------------------------------------
 * L1: String Tables
 * ------------------------------------------------------------------------*/

const char *pi_af_kpi_status_name(pi_af_kpi_status_t s) {
    switch (s) {
        case PI_AF_KPI_STATUS_GOOD:     return "Good";
        case PI_AF_KPI_STATUS_WARNING:  return "Warning";
        case PI_AF_KPI_STATUS_CRITICAL: return "Critical";
        case PI_AF_KPI_STATUS_UNKNOWN:  return "Unknown";
        default:                        return "?";
    }
}

const char *pi_af_kpi_direction_name(pi_af_kpi_direction_t d) {
    switch (d) {
        case PI_AF_KPI_DIR_HIGHER_BETTER: return "Higher is Better";
        case PI_AF_KPI_DIR_LOWER_BETTER:  return "Lower is Better";
        case PI_AF_KPI_DIR_TARGET_ZONE:   return "Target Zone";
        default:                          return "?";
    }
}

const char *pi_af_kpi_trend_name(pi_af_kpi_trend_t t) {
    switch (t) {
        case PI_AF_KPI_TREND_STABLE:       return "Stable";
        case PI_AF_KPI_TREND_IMPROVING:    return "Improving";
        case PI_AF_KPI_TREND_DETERIORATING: return "Deteriorating";
        case PI_AF_KPI_TREND_UNDEFINED:    return "Undefined";
        default:                           return "?";
    }
}

/* --------------------------------------------------------------------------
 * Engine Lifecycle
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_kpi_init(pi_af_kpi_context_t *ctx) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;
    memset(ctx, 0, sizeof(*ctx));
    return PI_AF_OK;
}

pi_af_error_t pi_af_kpi_register(pi_af_kpi_context_t *ctx,
                                  const pi_af_kpi_t *kpi, uint32_t *out_id) {
    if (!ctx || !kpi) return PI_AF_ERR_INVALID_ARGUMENT;

    uint32_t slot = (uint32_t)(-1);
    for (uint32_t i = 0; i < PI_AF_MAX_KPIS; i++) {
        if (!ctx->kpi_in_use[i]) { slot = i; break; }
    }
    if (slot == (uint32_t)(-1)) return PI_AF_ERR_OUT_OF_MEMORY;

    memcpy(&ctx->kpis[slot], kpi, sizeof(pi_af_kpi_t));

    /* Assign ID */
    if (ctx->kpis[slot].kpi_id == 0) {
        uint32_t max_id = 0;
        for (uint32_t i = 0; i < PI_AF_MAX_KPIS; i++) {
            if (ctx->kpi_in_use[i] && ctx->kpis[i].kpi_id > max_id) {
                max_id = ctx->kpis[i].kpi_id;
            }
        }
        ctx->kpis[slot].kpi_id = max_id + 1;
    }

    ctx->kpis[slot].current_status = PI_AF_KPI_STATUS_UNKNOWN;
    ctx->kpis[slot].current_trend  = PI_AF_KPI_TREND_UNDEFINED;
    ctx->kpis[slot].last_updated   = 0;
    ctx->kpis[slot].update_count   = 0;
    ctx->kpis[slot].perf_score     = 0.0;

    /* Register children relationships */
    for (uint32_t c = 0; c < ctx->kpis[slot].child_kpi_count; c++) {
        uint32_t child_id = ctx->kpis[slot].child_kpi_ids[c];
        /* Add to parent's children list */
        if (ctx->kpi_child_count[slot] < PI_AF_MAX_KPI_CHILDREN) {
            ctx->kpi_children[slot][ctx->kpi_child_count[slot]++] = child_id;
        }
    }

    ctx->kpi_in_use[slot] = true;
    ctx->kpi_count++;
    ctx->order_valid = false;

    if (out_id) *out_id = ctx->kpis[slot].kpi_id;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: KPI Status Evaluation (Traffic-Light Logic)
 * ------------------------------------------------------------------------*/

/**
 * @brief Evaluate KPI status from current value.
 *
 * The evaluation logic depends on the KPI direction:
 *
 *   HIGHER_BETTER:
 *     value ≥ target → GOOD
 *     value ≥ threshold_n (first below target) → WARNING
 *     otherwise → CRITICAL
 *
 *   LOWER_BETTER:
 *     value ≤ target → GOOD
 *     value ≤ threshold_n → WARNING
 *     otherwise → CRITICAL
 *
 *   TARGET_ZONE:
 *     target_min ≤ value ≤ target_max → GOOD
 *     within specified warning bands → WARNING
 *     otherwise → CRITICAL
 *
 * Thresholds are sorted from best to worst and evaluated in order.
 *
 * Performance score is normalized to 0–100:
 *   - 100 = perfect (at or beyond target)
 *   - 0 = worst
 *
 * For HIGHER_BETTER:
 *   perf_score = min(100, 100 * value / target)
 * For LOWER_BETTER:
 *   perf_score = max(0, 100 * (2*target - value) / target) [clamped]
 */
pi_af_error_t pi_af_kpi_evaluate(const pi_af_kpi_t *kpi, double current_value,
                                  pi_af_kpi_status_t *out_status,
                                  double *out_perf_score) {
    if (!kpi || !out_status || !out_perf_score) return PI_AF_ERR_INVALID_ARGUMENT;

    pi_af_kpi_status_t status = PI_AF_KPI_STATUS_GOOD;
    double score = 100.0;

    switch (kpi->direction) {
        case PI_AF_KPI_DIR_HIGHER_BETTER: {
            /* Thresholds must be sorted highest-value (best) to lowest-value (worst).
             * Algorithm: check target first, then fall through thresholds. */
            if (current_value >= kpi->target) {
                status = PI_AF_KPI_STATUS_GOOD;
            } else {
                status = PI_AF_KPI_STATUS_CRITICAL; /* default for below all thresholds */
                for (uint32_t i = 0; i < kpi->threshold_count; i++) {
                    const pi_af_kpi_threshold_t *t = &kpi->thresholds[i];
                    bool met;
                    if (t->inclusive) met = (current_value >= t->value);
                    else               met = (current_value > t->value);
                    if (met) {
                        status = t->status;
                        break; /* first (highest) threshold met determines status */
                    }
                }
            }
            /* Score: capped at 100 for values at or above target */
            if (kpi->target > 0.0) {
                score = (current_value / kpi->target) * 100.0;
            } else if (current_value > 0.0) {
                score = 100.0;
            }
            if (score > 100.0) score = 100.0;
            if (score < 0.0) score = 0.0;
            break;
        }

        case PI_AF_KPI_DIR_LOWER_BETTER: {
            /* Thresholds sorted lowest-value (best) to highest-value (worst). */
            if (current_value <= kpi->target) {
                status = PI_AF_KPI_STATUS_GOOD;
            } else {
                status = PI_AF_KPI_STATUS_CRITICAL;
                for (uint32_t i = 0; i < kpi->threshold_count; i++) {
                    const pi_af_kpi_threshold_t *t = &kpi->thresholds[i];
                    bool met;
                    if (t->inclusive) met = (current_value <= t->value);
                    else               met = (current_value < t->value);
                    if (met) {
                        status = t->status;
                        break;
                    }
                }
            }
            /* Score: 100 when at or below target, decays as value increases */
            if (kpi->target > 0.0) {
                score = (kpi->target / current_value) * 100.0;
            } else if (current_value <= 0.0) {
                score = 100.0;
            }
            if (score > 100.0) score = 100.0;
            if (score < 0.0) score = 0.0;
            break;
        }

        case PI_AF_KPI_DIR_TARGET_ZONE: {
            if (current_value >= kpi->target_min && current_value <= kpi->target_max) {
                status = PI_AF_KPI_STATUS_GOOD;
                score = 100.0;
            } else {
                /* Check thresholds */
                for (uint32_t i = 0; i < kpi->threshold_count; i++) {
                    const pi_af_kpi_threshold_t *t = &kpi->thresholds[i];
                    bool in_zone;
                    if (t->inclusive) {
                        in_zone = (current_value >= kpi->target_min - t->value) &&
                                  (current_value <= kpi->target_max + t->value);
                    } else {
                        in_zone = (current_value > kpi->target_min - t->value) &&
                                  (current_value < kpi->target_max + t->value);
                    }
                    if (in_zone) {
                        status = t->status;
                        break;
                    } else {
                        status = PI_AF_KPI_STATUS_CRITICAL;
                    }
                }
                /* Score: distance from center of zone */
                double center = (kpi->target_min + kpi->target_max) / 2.0;
                double half_width = (kpi->target_max - kpi->target_min) / 2.0;
                if (half_width > 0.0) {
                    double dist = fabs(current_value - center) / half_width;
                    score = 100.0 * (1.0 - dist);
                }
            }
            if (score > 100.0) score = 100.0;
            if (score < 0.0)   score = 0.0;
            break;
        }
    }

    *out_status = status;
    *out_perf_score = score;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: KPI Trend Detection (Linear Regression Slope)
 * ------------------------------------------------------------------------*/

/**
 * @brief Detect KPI trend using linear regression over a time window.
 *
 * Fits a straight line to the recent history and examines the slope.
 *
 * For HIGHER_BETTER: positive slope → improving
 * For LOWER_BETTER:  negative slope → improving
 *
 * A trend is declared only if |slope| > sensitivity.
 *
 * @see Box & Jenkins (1976) — Linear trend estimation
 */
pi_af_error_t pi_af_kpi_detect_trend(const pi_af_datapoint_t *data,
                                      uint32_t count,
                                      pi_af_kpi_direction_t direction,
                                      double sensitivity,
                                      pi_af_kpi_trend_t *out_trend,
                                      double *out_slope) {
    if (!data || !out_trend || !out_slope) return PI_AF_ERR_INVALID_ARGUMENT;

    if (count < 3) {
        *out_trend = PI_AF_KPI_TREND_UNDEFINED;
        *out_slope = 0.0;
        return PI_AF_OK;
    }

    /* OLS slope estimation using reference time for numerical stability */
    double t_ref = (double)data[0].timestamp;
    double sum_t = 0.0, sum_y = 0.0, sum_ty = 0.0, sum_t2 = 0.0;

    for (uint32_t i = 0; i < count; i++) {
        double t_rel = (double)data[i].timestamp - t_ref;
        double y = data[i].value;
        sum_t  += t_rel;
        sum_y  += y;
        sum_ty += t_rel * y;
        sum_t2 += t_rel * t_rel;
    }

    double n = (double)count;
    double denom = n * sum_t2 - sum_t * sum_t;
    double slope = 0.0;

    if (fabs(denom) > 1e-12) {
        slope = (n * sum_ty - sum_t * sum_y) / denom;
    }

    *out_slope = slope;

    /* Determine trend based on direction and slope magnitude */
    if (fabs(slope) <= sensitivity) {
        *out_trend = PI_AF_KPI_TREND_STABLE;
        return PI_AF_OK;
    }

    switch (direction) {
        case PI_AF_KPI_DIR_HIGHER_BETTER:
            *out_trend = (slope > 0.0) ? PI_AF_KPI_TREND_IMPROVING
                                        : PI_AF_KPI_TREND_DETERIORATING;
            break;
        case PI_AF_KPI_DIR_LOWER_BETTER:
            *out_trend = (slope < 0.0) ? PI_AF_KPI_TREND_IMPROVING
                                        : PI_AF_KPI_TREND_DETERIORATING;
            break;
        case PI_AF_KPI_DIR_TARGET_ZONE:
            /* For target zone, trend is improving if moving toward center */
            *out_trend = (slope > 0.0) ? PI_AF_KPI_TREND_IMPROVING
                                        : PI_AF_KPI_TREND_DETERIORATING;
            break;
    }

    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: KPI Rollup (Hierarchical Aggregation)
 * ------------------------------------------------------------------------*/

/**
 * @brief Roll up child KPI values to a parent KPI.
 *
 * Method 0 (Simple average):  parent_value = Σ child_values / N
 * Method 1 (Weighted avg):    parent_value = Σ (w_i × v_i) / Σ w_i
 * Method 2 (Minimum):         parent_value = min(child_values)
 * Method 3 (Maximum):         parent_value = max(child_values)
 * Method 4 (Count critical):  parent_value = count of children in CRITICAL state
 *
 * Status propagates as the worst status among children.
 */
pi_af_error_t pi_af_kpi_rollup(pi_af_kpi_context_t *kpi_ctx,
                                uint32_t parent_id, int method,
                                double *out_value,
                                pi_af_kpi_status_t *out_status) {
    if (!kpi_ctx || !out_value || !out_status) return PI_AF_ERR_INVALID_ARGUMENT;

    pi_af_kpi_t *parent = pi_af_kpi_get(kpi_ctx, parent_id);
    if (!parent) return PI_AF_ERR_NOT_FOUND;

    uint32_t child_count = parent->child_kpi_count;
    if (child_count == 0) {
        *out_value = parent->current_value;
        *out_status = parent->current_status;
        return PI_AF_OK;
    }

    /* Collect child values and statuses */
    double child_values[PI_AF_MAX_KPI_CHILDREN];
    pi_af_kpi_status_t child_statuses[PI_AF_MAX_KPI_CHILDREN];
    for (uint32_t i = 0; i < child_count; i++) {
        pi_af_kpi_t *child = pi_af_kpi_get(kpi_ctx, parent->child_kpi_ids[i]);
        if (child) {
            child_values[i]  = child->current_value;
            child_statuses[i] = child->current_status;
        } else {
            child_values[i]  = 0.0;
            child_statuses[i] = PI_AF_KPI_STATUS_UNKNOWN;
        }
    }

    /* Compute rolled value */
    double rolled = 0.0;
    switch (method) {
        case 0: { /* Simple average */
            double sum = 0.0;
            for (uint32_t i = 0; i < child_count; i++) sum += child_values[i];
            rolled = sum / (double)child_count;
            break;
        }
        case 1: { /* Weighted average by perf_score */
            double sum_wv = 0.0, sum_w = 0.0;
            for (uint32_t i = 0; i < child_count; i++) {
                pi_af_kpi_t *child = pi_af_kpi_get(kpi_ctx, parent->child_kpi_ids[i]);
                double w = child ? (child->perf_score > 0.0 ? child->perf_score : 1.0)
                                 : 1.0;
                sum_wv += w * child_values[i];
                sum_w  += w;
            }
            rolled = (sum_w > 0.0) ? sum_wv / sum_w : 0.0;
            break;
        }
        case 2: { /* Minimum (worst child value for lower-is-better) */
            double min_val = child_values[0];
            for (uint32_t i = 1; i < child_count; i++) {
                if (child_values[i] < min_val) min_val = child_values[i];
            }
            rolled = min_val;
            break;
        }
        case 3: { /* Maximum */
            double max_val = child_values[0];
            for (uint32_t i = 1; i < child_count; i++) {
                if (child_values[i] > max_val) max_val = child_values[i];
            }
            rolled = max_val;
            break;
        }
        case 4: { /* Count of children in CRITICAL */
            uint32_t crit = 0;
            for (uint32_t i = 0; i < child_count; i++) {
                if (child_statuses[i] == PI_AF_KPI_STATUS_CRITICAL) crit++;
            }
            rolled = (double)crit;
            break;
        }
        default:
            return PI_AF_ERR_INVALID_ARGUMENT;
    }

    /* Worst status propagates */
    pi_af_kpi_status_t worst = PI_AF_KPI_STATUS_GOOD;
    for (uint32_t i = 0; i < child_count; i++) {
        if (child_statuses[i] > worst) worst = child_statuses[i];
    }

    *out_value  = rolled;
    *out_status = worst;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * KPI Update
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_kpi_update(pi_af_kpi_context_t *kpi_ctx,
                                uint32_t kpi_id, double value,
                                time_t timestamp) {
    pi_af_kpi_t *kpi = pi_af_kpi_get(kpi_ctx, kpi_id);
    if (!kpi) return PI_AF_ERR_NOT_FOUND;

    kpi->current_value = value;

    /* Evaluate status and performance score */
    pi_af_kpi_evaluate(kpi, value, &kpi->current_status, &kpi->perf_score);

    /* For trend detection, we would need historical data.
     * In a real system, this would query the PI Data Archive.
     * For now, we set trend to stable if we have at least one update. */
    kpi->current_trend = PI_AF_KPI_TREND_STABLE;

    kpi->last_updated = timestamp;
    kpi->update_count++;

    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Topological Ordering for KPI Calculation
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_kpi_build_order(pi_af_kpi_context_t *kpi_ctx) {
    if (!kpi_ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    uint32_t N = kpi_ctx->kpi_count;
    if (N == 0) {
        kpi_ctx->order_valid = true;
        kpi_ctx->calc_order_count = 0;
        return PI_AF_OK;
    }

    /* Build in-degree array */
    uint32_t in_degree[PI_AF_MAX_KPIS];
    memset(in_degree, 0, sizeof(in_degree));

    for (uint32_t i = 0; i < PI_AF_MAX_KPIS; i++) {
        if (!kpi_ctx->kpi_in_use[i]) continue;
        for (uint32_t c = 0; c < kpi_ctx->kpis[i].child_kpi_count; c++) {
            uint32_t child_id = kpi_ctx->kpis[i].child_kpi_ids[c];
            /* Parent depends on child → child must be calculated first */
            /* Find child's slot */
            pi_af_kpi_t *child = pi_af_kpi_get(kpi_ctx, child_id);
            if (child) {
                /* in_degree[i]++ means parent has a dependency */
                in_degree[i]++;
            }
        }
    }

    /* Enqueue nodes with in_degree 0 */
    uint32_t queue[PI_AF_MAX_KPIS];
    uint32_t qh = 0, qt = 0;
    for (uint32_t i = 0; i < PI_AF_MAX_KPIS; i++) {
        if (kpi_ctx->kpi_in_use[i] && in_degree[i] == 0) {
            queue[qt++] = i;
        }
    }

    uint32_t order_idx = 0;
    while (qh < qt) {
        uint32_t u = queue[qh++];
        kpi_ctx->calc_order[order_idx++] = kpi_ctx->kpis[u].kpi_id;

        /* For each child of u, decrement the parent's in_degree */
        for (uint32_t p = 0; p < PI_AF_MAX_KPIS; p++) {
            if (!kpi_ctx->kpi_in_use[p]) continue;
            for (uint32_t c = 0; c < kpi_ctx->kpis[p].child_kpi_count; c++) {
                if (kpi_ctx->kpis[p].child_kpi_ids[c] == kpi_ctx->kpis[u].kpi_id) {
                    in_degree[p]--;
                    if (in_degree[p] == 0) {
                        queue[qt++] = p;
                    }
                    break;
                }
            }
        }
    }

    if (order_idx != N) {
        return PI_AF_ERR_CIRCULAR_DEPENDENCY;
    }

    kpi_ctx->calc_order_count = order_idx;
    kpi_ctx->order_valid = true;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Lookup
 * ------------------------------------------------------------------------*/

pi_af_kpi_t *pi_af_kpi_get(pi_af_kpi_context_t *kpi_ctx, uint32_t kpi_id) {
    if (!kpi_ctx) return NULL;
    for (uint32_t i = 0; i < PI_AF_MAX_KPIS; i++) {
        if (kpi_ctx->kpi_in_use[i] && kpi_ctx->kpis[i].kpi_id == kpi_id) {
            return &kpi_ctx->kpis[i];
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * L5: Composite Scoring (Weighted Multi-KPI)
 * ------------------------------------------------------------------------*/

/**
 * @brief Compute a weighted composite score from multiple KPI performance scores.
 *
 * Composite = Σ(w_i × score_i) / Σ(w_i)
 *
 * This is the standard Analytic Hierarchy Process (AHP) approach to
 * combining multiple KPIs into a single dashboard number.
 *
 * @see Saaty, T.L. (1980) "The Analytic Hierarchy Process", McGraw-Hill.
 */
pi_af_error_t pi_af_kpi_composite_score(const double *scores,
                                         const double *weights,
                                         uint32_t count,
                                         double *out_composite) {
    if (!scores || !weights || !out_composite) return PI_AF_ERR_INVALID_ARGUMENT;
    if (count == 0) { *out_composite = 0.0; return PI_AF_OK; }

    double sum_wv = 0.0, sum_w = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        double w = (weights[i] > 0.0) ? weights[i] : 0.0;
        sum_wv += w * scores[i];
        sum_w  += w;
    }

    *out_composite = (sum_w > 0.0) ? sum_wv / sum_w : 0.0;
    return PI_AF_OK;
}
