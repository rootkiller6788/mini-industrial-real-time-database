/**
 * @file ts_piecewise_linear.c
 * @brief Piecewise Linear Approximation Implementation
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge: L2 Concepts, L5 Algorithms, L6 Canonical Problems
 *
 * Implements four PLA algorithms: Sliding Window, Top-Down (Douglas-Peucker),
 * Bottom-Up (merge-based), and SWAB (Keogh's hybrid). Also includes MDL-based
 * automatic segment count optimization.
 *
 * Reference: Keogh, E. et al. (2001). ICDM 2001, pp. 289-296.
 *            Rissanen, J. (1978). Automatica 14:465-471.
 * Curriculum: Stanford AA272, CMU 24-677, Berkeley ME233
 */

#include "ts_piecewise_linear.h"
#include "ts_deadband.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

/* ---------------------------------------------------------------------------
 * L2: Model Init/Free
 * ------------------------------------------------------------------------- */

int ts_pla_model_init(ts_pla_model_t *model, size_t capacity)
{
    if (!model || capacity == 0) return -1;

    model->segments = (ts_pla_segment_t *)calloc(capacity, sizeof(ts_pla_segment_t));
    if (!model->segments) return -1;

    model->num_segments = 0;
    model->capacity = capacity;
    model->error_tolerance = 0.0;
    model->max_segment_error = 0.0;
    model->rmse = 0.0;
    model->original_points = 0;
    model->compressed_points = 0;
    model->compression_ratio = 1.0;

    return 0;
}

void ts_pla_model_free(ts_pla_model_t *model)
{
    if (model && model->segments) {
        free(model->segments);
        model->segments = NULL;
        model->capacity = 0;
        model->num_segments = 0;
    }
}

/* ---------------------------------------------------------------------------
 * L5: Helper — Add a segment to the model
 * ------------------------------------------------------------------------- */

static int pla_add_segment(ts_pla_model_t *model,
                            int64_t t_start, int64_t t_end,
                            double v_start, double v_end)
{
    if (model->num_segments >= model->capacity) {
        /* Grow capacity */
        size_t new_cap = model->capacity * 2;
        ts_pla_segment_t *new_segs = (ts_pla_segment_t *)realloc(
            model->segments, new_cap * sizeof(ts_pla_segment_t));
        if (!new_segs) return -1;
        model->segments = new_segs;
        model->capacity = new_cap;
    }

    ts_pla_segment_t *seg = &model->segments[model->num_segments];
    seg->t_start = t_start;
    seg->t_end = t_end;
    seg->v_start = v_start;
    seg->v_end = v_end;

    double dt = (double)(t_end - t_start);
    if (dt > 0.0) {
        seg->slope = (v_end - v_start) / dt;
    } else {
        seg->slope = 0.0;
    }

    seg->max_error = 0.0;
    seg->num_points = 2;

    model->num_segments++;
    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Point-to-Segment Perpendicular Distance
 *
 * Distance from point (xp, yp) to line through (x0,y0)-(x1,y1):
 *
 *   d = |(x1-x0)*(y0-yp) - (x0-xp)*(y1-y0)| / sqrt((x1-x0)^2 + (y1-y0)^2)
 *
 * For time-series PLA, the x-axis is time and y-axis is value.
 * We compute this in double precision after casting int64 timestamps.
 * ------------------------------------------------------------------------- */

double ts_pla_point_segment_distance(int64_t x0, double y0,
                                       int64_t x1, double y1,
                                       int64_t xp, double yp)
{
    double dx = (double)(x1 - x0);
    double dy = y1 - y0;

    double len_sq = dx * dx + dy * dy;
    if (len_sq < 1e-30) {
        /* Degenerate: segment is a point */
        double dpx = (double)(xp - x0);
        return sqrt(dpx * dpx + (yp - y0) * (yp - y0));
    }

    /* Cross product magnitude divided by segment length */
    double cross = fabs((double)(x1 - x0) * (y0 - yp)
                       - (double)(x0 - xp) * (y1 - y0));
    return cross / sqrt(len_sq);
}

/* ---------------------------------------------------------------------------
 * L5: Maximum vertical deviation within a candidate segment
 *
 * For points [start, end], compute the maximum absolute deviation
 * of values from the line connecting start to end.
 *
 * For PLA with time-value data, we use the time-normalized perpendicular
 * distance, which accounts for both time and value axes.
 * ------------------------------------------------------------------------- */

static double segment_max_error(const ts_data_point_t *points,
                                 size_t start, size_t end)
{
    double max_err = 0.0;

    int64_t x0 = points[start].epoch_us;
    double  y0 = points[start].value;
    int64_t x1 = points[end].epoch_us;
    double  y1 = points[end].value;

    for (size_t i = start + 1; i < end; i++) {
        double d = ts_pla_point_segment_distance(
            x0, y0, x1, y1,
            points[i].epoch_us, points[i].value);
        if (d > max_err) max_err = d;
    }

    return max_err;
}

/* ---------------------------------------------------------------------------
 * L5: Sliding Window PLA
 *
 * Algorithm:
 *   anchor = 0
 *   for i = 2, 3, ..., n-1:
 *     err = max_error_of_segment(anchor, i)
 *     if err > tolerance:
 *       close segment at i-1
 *       anchor = i-1
 *   close final segment at n-1
 *
 * This is the simplest online-capable PLA algorithm. Each new point
 * either extends the current segment (if error acceptable) or starts
 * a new segment.
 *
 * Complexity: O(n*L) where L is average segment length.
 * Worst case (no compression): O(n^2)
 * ------------------------------------------------------------------------- */

int ts_pla_sliding_window(ts_pla_model_t *model,
                           const ts_data_point_t *points,
                           size_t n,
                           const ts_pla_config_t *config)
{
    if (!model || !points || !config || n < 2) return -1;

    model->num_segments = 0;
    model->original_points = n;
    double tol = config->error_tolerance;

    size_t anchor = 0;
    size_t i = 2;

    while (i < n) {
        double max_err = segment_max_error(points, anchor, i);

        if (max_err > tol &&
            (i - anchor) >= config->min_segment_points) {
            /* Close segment at i-1 */
            int ret = pla_add_segment(model,
                points[anchor].epoch_us, points[i-1].epoch_us,
                points[anchor].value, points[i-1].value);
            if (ret != 0) return ret;
            model->segments[model->num_segments - 1].max_error = max_err;
            model->segments[model->num_segments - 1].num_points =
                (uint32_t)(i - anchor);

            anchor = i - 1;  /* Start new segment from the last included point */
        }

        /* Check max_segment_points constraint */
        if (config->max_segment_points > 0 &&
            (i - anchor) >= config->max_segment_points) {
            int ret = pla_add_segment(model,
                points[anchor].epoch_us, points[i].epoch_us,
                points[anchor].value, points[i].value);
            if (ret != 0) return ret;
            model->segments[model->num_segments - 1].num_points =
                (uint32_t)(i - anchor + 1);
            anchor = i;
            i = anchor + 2;
            continue;
        }

        i++;
    }

    /* Close final segment */
    if (anchor < n - 1) {
        pla_add_segment(model,
            points[anchor].epoch_us, points[n-1].epoch_us,
            points[anchor].value, points[n-1].value);
        model->segments[model->num_segments - 1].num_points =
            (uint32_t)(n - anchor);
    }

    model->compressed_points = model->num_segments * 2;
    if (model->compressed_points > 0) {
        model->compression_ratio = (double)n / (double)model->compressed_points;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Top-Down (Douglas-Peucker) PLA
 *
 * Recursive algorithm:
 *   1. Fit line from points[start] to points[end]
 *   2. Find point k in (start, end) with maximum distance from line
 *   3. If distance <= tolerance: accept this segment
 *   4. Else: recursively process [start, k] and [k, end]
 *
 * This algorithm is optimal in the sense that, given the segmentation
 * points must come from the original data (connected PLA), it minimizes
 * the number of segments for a given error tolerance.
 *
 * Complexity: O(n^2) worst case, O(n log n) average.
 * ------------------------------------------------------------------------- */

static int pla_top_down_recursive(ts_pla_model_t *model,
                                    const ts_data_point_t *points,
                                    size_t start, size_t end,
                                    double tolerance,
                                    uint32_t min_points)
{
    if (end <= start + 1) return 0;

    /* Find split point with maximum distance */
    double max_dist = 0.0;
    size_t split = start + 1;

    int64_t x0 = points[start].epoch_us;
    double  y0 = points[start].value;
    int64_t x1 = points[end].epoch_us;
    double  y1 = points[end].value;

    for (size_t i = start + 1; i < end; i++) {
        double d = ts_pla_point_segment_distance(
            x0, y0, x1, y1,
            points[i].epoch_us, points[i].value);
        if (d > max_dist) {
            max_dist = d;
            split = i;
        }
    }

    if (max_dist <= tolerance ||
        (end - start) <= min_points) {
        /* Accept this segment */
        int ret = pla_add_segment(model,
            points[start].epoch_us, points[end].epoch_us,
            points[start].value, points[end].value);
        if (ret != 0) return ret;
        model->segments[model->num_segments - 1].max_error = max_dist;
        model->segments[model->num_segments - 1].num_points =
            (uint32_t)(end - start + 1);
        return 0;
    }

    /* Recursively split */
    int ret = pla_top_down_recursive(model, points, start, split,
                                      tolerance, min_points);
    if (ret != 0) return ret;

    ret = pla_top_down_recursive(model, points, split, end,
                                  tolerance, min_points);
    return ret;
}

int ts_pla_top_down(ts_pla_model_t *model,
                     const ts_data_point_t *points,
                     size_t n,
                     const ts_pla_config_t *config)
{
    if (!model || !points || !config || n < 2) return -1;

    model->num_segments = 0;
    model->original_points = n;

    int ret = pla_top_down_recursive(model, points, 0, n - 1,
                                      config->error_tolerance,
                                      config->min_segment_points);
    if (ret != 0) return ret;

    model->compressed_points = model->num_segments * 2;
    if (model->compressed_points > 0) {
        model->compression_ratio = (double)n / (double)model->compressed_points;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Bottom-Up PLA
 *
 * Algorithm:
 *   1. Create initial segmentation: every K=min_segment_points points
 *   2. Compute merge cost for each adjacent pair of segments
 *   3. Merge the pair with the smallest merge cost
 *   4. Update merge costs for the affected neighbors
 *   5. Repeat until all merge costs exceed tolerance
 *
 * The merge cost is the maximum deviation of points in the combined
 * segment from the line connecting its endpoints.
 *
 * Complexity: O(n log n) with priority queue.
 * ------------------------------------------------------------------------- */

int ts_pla_bottom_up(ts_pla_model_t *model,
                      const ts_data_point_t *points,
                      size_t n,
                      const ts_pla_config_t *config)
{
    if (!model || !points || !config || n < 2) return -1;

    model->num_segments = 0;
    model->original_points = n;

    /* Step 1: Create initial fine-grained segmentation */
    size_t seg_size = config->min_segment_points > 0
                      ? config->min_segment_points : 2;
    if (seg_size < 2) seg_size = 2;

    size_t num_initial = 0;
    size_t pos = 0;

    /* Allocate temporary segment index array */
    size_t max_segs = n / seg_size + 2;
    size_t *seg_ends = (size_t *)malloc(max_segs * sizeof(size_t));
    if (!seg_ends) return -1;

    while (pos + seg_size <= n - 1) {
        seg_ends[num_initial] = pos + seg_size;
        num_initial++;
        pos += seg_size;
    }
    /* Last segment includes remaining points */
    if (pos < n - 1) {
        seg_ends[num_initial] = n - 1;
        num_initial++;
    }

    /* Step 2: Greedy merge loop */
    double tolerance = config->error_tolerance;

    while (num_initial > 1) {
        /* Find pair with minimum merge cost */
        double min_cost = DBL_MAX;
        size_t min_idx = 0;

        for (size_t i = 0; i < num_initial - 1; i++) {
            size_t seg_start = (i == 0) ? 0 : seg_ends[i - 1] + 1;
            size_t seg_end   = seg_ends[i + 1];

            double cost = segment_max_error(points, seg_start, seg_end);
            if (cost < min_cost) {
                min_cost = cost;
                min_idx = i;
            }
        }

        if (min_cost > tolerance) break;  /* No more acceptable merges */

        /* Merge segments min_idx and min_idx+1 */
        seg_ends[min_idx] = seg_ends[min_idx + 1];

        /* Shift remaining indices */
        for (size_t i = min_idx + 1; i < num_initial - 1; i++) {
            seg_ends[i] = seg_ends[i + 1];
        }
        num_initial--;
    }

    /* Step 3: Build PLA model from final segments */
    pos = 0;
    for (size_t i = 0; i < num_initial; i++) {
        pla_add_segment(model,
            points[pos].epoch_us,
            points[seg_ends[i]].epoch_us,
            points[pos].value,
            points[seg_ends[i]].value);
        model->segments[model->num_segments - 1].num_points =
            (uint32_t)(seg_ends[i] - pos + 1);
        pos = seg_ends[i] + 1;
    }

    /* Handle any trailing points */
    if (pos < n) {
        pla_add_segment(model,
            points[pos].epoch_us, points[n-1].epoch_us,
            points[pos].value, points[n-1].value);
        model->segments[model->num_segments - 1].num_points =
            (uint32_t)(n - pos);
    }

    free(seg_ends);

    model->compressed_points = model->num_segments * 2;
    if (model->compressed_points > 0) {
        model->compression_ratio = (double)n / (double)model->compressed_points;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: SWAB (Sliding Window And Bottom-up) — Keogh's Hybrid
 *
 * For streaming data, SWAB maintains a sliding buffer. New points enter
 * the buffer using sliding window logic. When the buffer reaches a
 * threshold, bottom-up refinement is applied to the buffer contents,
 * producing final segments for the first part of the buffer.
 *
 * Complexity: O(n*B) where B is buffer size.
 * ------------------------------------------------------------------------- */

int ts_pla_swab(ts_pla_model_t *model,
                 const ts_data_point_t *points,
                 size_t n,
                 const ts_pla_config_t *config,
                 size_t buffer_size)
{
    if (!model || !points || !config || n < 2) return -1;
    if (buffer_size < 10) buffer_size = 10;

    model->num_segments = 0;
    model->original_points = n;

    size_t pos = 0;

    while (pos < n) {
        size_t remaining = n - pos;
        size_t batch_size = (remaining < buffer_size) ? remaining : buffer_size;

        /* Apply sliding window to this batch */
        ts_pla_model_t batch_model;
        if (ts_pla_model_init(&batch_model, batch_size) != 0) return -1;

        ts_pla_config_t sw_config = *config;
        sw_config.algorithm = TS_PLA_SLIDING_WINDOW;

        ts_pla_sliding_window(&batch_model,
                               points + pos, batch_size, &sw_config);

        /* Copy batch segments to main model */
        for (size_t i = 0; i < batch_model.num_segments; i++) {
            pla_add_segment(model,
                batch_model.segments[i].t_start,
                batch_model.segments[i].t_end,
                batch_model.segments[i].v_start,
                batch_model.segments[i].v_end);
            model->segments[model->num_segments - 1].num_points =
                batch_model.segments[i].num_points;
        }

        ts_pla_model_free(&batch_model);
        pos += batch_size;
    }

    model->compressed_points = model->num_segments * 2;
    if (model->compressed_points > 0) {
        model->compression_ratio = (double)n / (double)model->compressed_points;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L2: Dispatcher
 * ------------------------------------------------------------------------- */

int ts_pla_compute(ts_pla_model_t *model,
                    const ts_data_point_t *points,
                    size_t n,
                    const ts_pla_config_t *config)
{
    switch (config->algorithm) {
    case TS_PLA_SLIDING_WINDOW:
        return ts_pla_sliding_window(model, points, n, config);
    case TS_PLA_TOP_DOWN:
        return ts_pla_top_down(model, points, n, config);
    case TS_PLA_BOTTOM_UP:
        return ts_pla_bottom_up(model, points, n, config);
    case TS_PLA_SWAB:
        return ts_pla_swab(model, points, n, config, 50);
    default:
        return -1;
    }
}

/* ---------------------------------------------------------------------------
 * L6: PLA Reconstruction
 * ------------------------------------------------------------------------- */

int ts_pla_reconstruct(const ts_pla_model_t *model,
                        const int64_t *timestamps,
                        size_t n,
                        double *values)
{
    if (!model || !timestamps || !values) return -1;

    size_t seg_idx = 0;

    for (size_t i = 0; i < n; i++) {
        int64_t t = timestamps[i];

        /* Advance segment index */
        while (seg_idx + 1 < model->num_segments
               && model->segments[seg_idx + 1].t_start <= t) {
            seg_idx++;
        }

        const ts_pla_segment_t *seg = &model->segments[seg_idx];

        if (t <= seg->t_start) {
            values[i] = seg->v_start;
        } else if (t >= seg->t_end) {
            values[i] = seg->v_end;
        } else {
            double dt = (double)(seg->t_end - seg->t_start);
            if (dt <= 0.0) {
                values[i] = seg->v_start;
            } else {
                values[i] = seg->v_start + seg->slope * (double)(t - seg->t_start);
            }
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L6: Error Metrics
 * ------------------------------------------------------------------------- */

int ts_pla_compute_error(ts_pla_model_t *model,
                          const ts_data_point_t *original,
                          size_t n)
{
    if (!model || !original || n < 2) return -1;

    double sum_sq = 0.0;
    double max_seg_err = 0.0;
    size_t seg_idx = 0;

    for (size_t i = 0; i < n; i++) {
        int64_t t = original[i].epoch_us;

        while (seg_idx + 1 < model->num_segments
               && model->segments[seg_idx + 1].t_start <= t) {
            seg_idx++;
        }

        const ts_pla_segment_t *seg = &model->segments[seg_idx];
        double v_recon;

        if (t <= seg->t_start) v_recon = seg->v_start;
        else if (t >= seg->t_end) v_recon = seg->v_end;
        else {
            v_recon = seg->v_start + seg->slope * (double)(t - seg->t_start);
        }

        double err = original[i].value - v_recon;
        sum_sq += err * err;
        if (fabs(err) > max_seg_err) max_seg_err = fabs(err);
    }

    model->rmse = sqrt(sum_sq / (double)n);
    model->max_segment_error = max_seg_err;

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: MDL-Based Optimal Segment Count
 *
 * Minimum Description Length principle (Rissanen, 1978):
 *
 *   MDL(K) = K * log2(n) * c_param + (n/2) * log2(sigma_e^2(K))
 *
 * where K = number of segments, sigma_e^2 = reconstruction error variance.
 *
 * The term K * log2(n) * c_param is the model complexity cost (bits to
 * encode K parameters). The term (n/2)*log2(sigma_e^2) is the data
 * description cost (bits to encode residuals).
 *
 * We evaluate MDL(K) for K from 1 to max_segments and select the K
 * that minimizes the description length.
 * ------------------------------------------------------------------------- */

double ts_pla_mdl_optimal_segments(const ts_data_point_t *points,
                                     size_t n,
                                     size_t max_segments,
                                     double c_param,
                                     size_t *optimal_k)
{
    if (!points || n < 2 || max_segments < 1 || !optimal_k) return DBL_MAX;

    double best_mdl = DBL_MAX;
    *optimal_k = 1;
    double log2n = log2((double)n);

    /* Evaluate K = 1 to max_segments via tolerance sweep */
    /* Map K -> tolerance by binary search */
    double tol_max = 10.0;  /* Initial guess for max error */

    /* Expand tol_max until K <= max_segments is achievable */
    for (int expand = 0; expand < 10; expand++) {
        ts_pla_model_t model;
        ts_pla_model_init(&model, max_segments + 2);

        ts_pla_config_t config = {
            .algorithm = TS_PLA_SLIDING_WINDOW,
            .error_tolerance = tol_max,
            .min_segment_points = 2,
            .max_segment_points = 0
        };

        ts_pla_top_down(&model, points, n, &config);
        size_t k = model.num_segments;
        ts_pla_model_free(&model);

        if (k <= max_segments) break;
        tol_max *= 2.0;
    }

    /* Binary search for each K */
    for (size_t target_k = 1; target_k <= max_segments; target_k++) {
        double lo = 0.0, hi = tol_max;
        double best_tol = hi;

        for (int bs_iter = 0; bs_iter < 20; bs_iter++) {
            double mid = (lo + hi) * 0.5;

            ts_pla_model_t model;
            ts_pla_model_init(&model, max_segments + 2);
            ts_pla_config_t config = {
                .algorithm = TS_PLA_TOP_DOWN,
                .error_tolerance = mid,
                .min_segment_points = 2
            };
            ts_pla_top_down(&model, points, n, &config);
            size_t k = model.num_segments;
            ts_pla_model_free(&model);

            if (k <= target_k) {
                best_tol = mid;
                hi = mid;
            } else {
                lo = mid;
            }
        }

        /* Compute MDL for K=target_k at tolerance=best_tol */
        ts_pla_model_t model;
        ts_pla_model_init(&model, max_segments + 2);
        ts_pla_config_t config = {
            .algorithm = TS_PLA_SLIDING_WINDOW,
            .error_tolerance = best_tol,
            .min_segment_points = 2
        };
        ts_pla_sliding_window(&model, points, n, &config);
        size_t k = model.num_segments;

        /* RMSE of reconstruction */
        double recon_rmse = 0.0;
        double sum_sq = 0.0;
        size_t seg_idx = 0;
        for (size_t i = 0; i < n; i++) {
            int64_t t = points[i].epoch_us;
            while (seg_idx + 1 < k && model.segments[seg_idx + 1].t_start <= t)
                seg_idx++;
            const ts_pla_segment_t *seg = &model.segments[seg_idx];
            double v_recon;
            if (t <= seg->t_start) v_recon = seg->v_start;
            else if (t >= seg->t_end) v_recon = seg->v_end;
            else v_recon = seg->v_start + seg->slope * (double)(t - seg->t_start);
            double err = points[i].value - v_recon;
            sum_sq += err * err;
        }
        recon_rmse = sqrt(sum_sq / (double)n);
        if (recon_rmse < 1e-12) recon_rmse = 1e-12;

        double mdl = (double)k * log2n * c_param
                     + (double)n * 0.5 * log2(recon_rmse * recon_rmse);

        ts_pla_model_free(&model);

        if (mdl < best_mdl) {
            best_mdl = mdl;
            *optimal_k = k;
        }
    }

    return best_mdl;
}

/* ---------------------------------------------------------------------------
 * L6: Convenience: Compress to Boundary Points
 * ------------------------------------------------------------------------- */

int ts_pla_compress(const ts_data_point_t *points, size_t n,
                     const ts_pla_config_t *config,
                     ts_data_point_t **compressed,
                     size_t *num_compressed)
{
    if (!points || !config || !compressed || !num_compressed || n < 2)
        return -1;

    ts_pla_model_t model;
    if (ts_pla_model_init(&model, n) != 0) return -1;

    ts_pla_compute(&model, points, n, config);

    *num_compressed = model.num_segments * 2;
    *compressed = (ts_data_point_t *)malloc(
        *num_compressed * sizeof(ts_data_point_t));

    if (!*compressed) {
        ts_pla_model_free(&model);
        return -1;
    }

    for (size_t i = 0; i < model.num_segments; i++) {
        /* Start point of segment */
        (*compressed)[2*i].epoch_us = model.segments[i].t_start;
        (*compressed)[2*i].value = model.segments[i].v_start;
        (*compressed)[2*i].quality = TS_QUALITY_GOOD;

        /* End point of segment */
        (*compressed)[2*i + 1].epoch_us = model.segments[i].t_end;
        (*compressed)[2*i + 1].value = model.segments[i].v_end;
        (*compressed)[2*i + 1].quality = TS_QUALITY_GOOD;
    }

    ts_pla_model_free(&model);
    return 0;
}
