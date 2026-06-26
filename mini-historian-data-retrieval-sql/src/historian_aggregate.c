/**
 * @file    historian_aggregate.c
 * @brief   Time-series aggregate computation implementation.
 *
 * Knowledge coverage:
 *   L1: 20 aggregate function types
 *   L2: Time-weighted averaging (trapezoidal integration)
 *   L4: ISO 22400-2 KPI bucket conventions
 *   L5: Welford's online algorithm for variance, percentile computation
 *   L8: Distribution statistics (skewness, kurtosis)
 */

#include "historian_aggregate.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* =========================================================================
 * L1: Specification Initialization
 * ========================================================================= */

void historian_aggregate_spec_init(historian_aggregate_spec_t *spec)
{
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));
    spec->agg_type = HISTORIAN_AGG_AVERAGE;
    spec->tag_id = -1;
    spec->percentile = 50.0;
    spec->quality_aware = 1;
    historian_bucket_spec_init(&spec->bucket);
}

void historian_bucket_spec_init(historian_bucket_spec_t *spec)
{
    if (!spec) return;
    memset(spec, 0, sizeof(*spec));
    spec->period = HISTORIAN_BUCKET_HOUR;
    spec->alignment = HISTORIAN_ALIGN_QUERY_START;
    spec->exclude_partial = 0;
}

/* =========================================================================
 * L5: Core Aggregate Computation
 * ========================================================================= */

/* Running state for Welford's online algorithm.
 * Welford, B.P. (1962). "Note on a method for calculating corrected sums
 * of squares and products". Technometrics 4(3):419-420.
 *
 * Update formulas:
 *   n = n + 1
 *   delta = x - mean
 *   mean = mean + delta / n
 *   M2 = M2 + delta * (x - mean)
 *
 * Then: variance = M2 / n (population) or M2 / (n-1) (sample)
 */
typedef struct {
    size_t n;
    double mean;
    double M2;       /* Sum of squared differences from current mean */
    double min_val;
    double max_val;
    double sum;
} welford_state_t;

int historian_compute_aggregate(historian_aggregate_type_t agg_type,
                                 const historian_data_point_t *points,
                                 size_t count, double *result)
{
    if (!result) return -1;
    if (count == 0) { *result = NAN; return -2; }

    /* Single-pass algorithms where possible */
    switch (agg_type) {
    case HISTORIAN_AGG_COUNT:
        *result = (double)count;
        return 0;

    case HISTORIAN_AGG_SUM: {
        double sum = 0.0;
        for (size_t i = 0; i < count; i++) {
            if (isfinite(points[i].value)) sum += points[i].value;
        }
        *result = sum;
        return 0;
    }

    case HISTORIAN_AGG_AVERAGE: {
        double sum = 0.0;
        size_t valid = 0;
        for (size_t i = 0; i < count; i++) {
            if (isfinite(points[i].value)) {
                sum += points[i].value;
                valid++;
            }
        }
        if (valid == 0) { *result = NAN; return -3; }
        *result = sum / (double)valid;
        return 0;
    }

    case HISTORIAN_AGG_MINIMUM: {
        double min_val = INFINITY;
        int found = 0;
        for (size_t i = 0; i < count; i++) {
            if (isfinite(points[i].value) && points[i].value < min_val) {
                min_val = points[i].value;
                found = 1;
            }
        }
        *result = found ? min_val : NAN;
        return found ? 0 : -4;
    }

    case HISTORIAN_AGG_MAXIMUM: {
        double max_val = -INFINITY;
        int found = 0;
        for (size_t i = 0; i < count; i++) {
            if (isfinite(points[i].value) && points[i].value > max_val) {
                max_val = points[i].value;
                found = 1;
            }
        }
        *result = found ? max_val : NAN;
        return found ? 0 : -5;
    }

    case HISTORIAN_AGG_RANGE: {
        double min_val = INFINITY, max_val = -INFINITY;
        int found = 0;
        for (size_t i = 0; i < count; i++) {
            if (isfinite(points[i].value)) {
                if (points[i].value < min_val) min_val = points[i].value;
                if (points[i].value > max_val) max_val = points[i].value;
                found = 1;
            }
        }
        if (!found) { *result = NAN; return -6; }
        *result = max_val - min_val;
        return 0;
    }

    case HISTORIAN_AGG_STDDEV:
    case HISTORIAN_AGG_VARIANCE: {
        /* Welford's single-pass algorithm */
        welford_state_t ws = {0, 0.0, 0.0, INFINITY, -INFINITY, 0.0};
        for (size_t i = 0; i < count; i++) {
            if (!isfinite(points[i].value)) continue;
            ws.n++;
            double delta = points[i].value - ws.mean;
            ws.mean += delta / (double)ws.n;
            double delta2 = points[i].value - ws.mean;
            ws.M2 += delta * delta2;
        }
        if (ws.n < 1) { *result = NAN; return -7; }
        if (ws.n == 1 && agg_type == HISTORIAN_AGG_STDDEV_SAMPLE) {
            *result = NAN; return -8;
        }
        double variance = (agg_type == HISTORIAN_AGG_STDDEV_SAMPLE ||
                           agg_type == HISTORIAN_AGG_VARIANCE_SAMPLE)
                          ? ws.M2 / (double)(ws.n - 1)
                          : ws.M2 / (double)ws.n;
        if (agg_type == HISTORIAN_AGG_STDDEV ||
            agg_type == HISTORIAN_AGG_STDDEV_SAMPLE) {
            *result = sqrt(variance);
        } else {
            *result = variance;
        }
        return 0;
    }

    case HISTORIAN_AGG_FIRST: {
        /* Return the chronologically first value */
        *result = (count > 0 && isfinite(points[0].value)) ? points[0].value : NAN;
        return (count > 0) ? 0 : -9;
    }

    case HISTORIAN_AGG_LAST: {
        *result = (count > 0 && isfinite(points[count-1].value))
                  ? points[count-1].value : NAN;
        return (count > 0) ? 0 : -10;
    }

    case HISTORIAN_AGG_DELTA: {
        if (count < 2) { *result = NAN; return -11; }
        double first = points[0].value;
        double last  = points[count-1].value;
        if (!isfinite(first) || !isfinite(last)) { *result = NAN; return -11; }
        *result = last - first;
        return 0;
    }

    case HISTORIAN_AGG_MEDIAN:
    case HISTORIAN_AGG_PERCENTILE: {
        /* Need to sort a copy, then pick the kth element */
        size_t valid_count = 0;
        for (size_t i = 0; i < count; i++) {
            if (isfinite(points[i].value)) valid_count++;
        }
        if (valid_count == 0) { *result = NAN; return -12; }

        double *sorted = (double *)malloc(valid_count * sizeof(double));
        if (!sorted) return -13;

        size_t idx = 0;
        for (size_t i = 0; i < count; i++) {
            if (isfinite(points[i].value)) sorted[idx++] = points[i].value;
        }

        /* Simple insertion sort for moderate sizes */
        for (size_t i = 1; i < valid_count; i++) {
            double key = sorted[i];
            size_t j = i;
            while (j > 0 && sorted[j-1] > key) {
                sorted[j] = sorted[j-1];
                j--;
            }
            sorted[j] = key;
        }

        double pct = (agg_type == HISTORIAN_AGG_MEDIAN) ? 50.0 : 50.0; /* default */
        /* For percentile, use the natural ranking formula:
         *   index = (pct/100) * (N - 1)
         * Linear interpolation between floor and ceil indices
         */
        double rank = (pct / 100.0) * (double)(valid_count - 1);
        size_t lo = (size_t)floor(rank);
        size_t hi = (size_t)ceil(rank);
        if (lo >= valid_count) lo = valid_count - 1;
        if (hi >= valid_count) hi = valid_count - 1;

        if (lo == hi) {
            *result = sorted[lo];
        } else {
            double frac = rank - (double)lo;
            *result = sorted[lo] + frac * (sorted[hi] - sorted[lo]);
        }

        free(sorted);
        return 0;
    }

    case HISTORIAN_AGG_RATE: {
        if (count < 2) { *result = NAN; return -14; }
        double first = points[0].value;
        double last  = points[count-1].value;
        int64_t dt_ms = points[count-1].timestamp.epoch_ms -
                         points[0].timestamp.epoch_ms;
        if (dt_ms <= 0 || !isfinite(first) || !isfinite(last)) {
            *result = NAN; return -14;
        }
        *result = (last - first) / ((double)dt_ms / 1000.0);
        return 0;
    }

    case HISTORIAN_AGG_INTEGRAL: {
        /* Trapezoidal integration: SUM( (v_i + v_{i+1})/2 * dt_i ) */
        double integral = 0.0;
        for (size_t i = 0; i < count - 1; i++) {
            if (!isfinite(points[i].value) || !isfinite(points[i+1].value))
                continue;
            double avg_val = (points[i].value + points[i+1].value) * 0.5;
            double dt_s = (double)(points[i+1].timestamp.epoch_ms -
                                   points[i].timestamp.epoch_ms) / 1000.0;
            integral += avg_val * dt_s;
        }
        *result = integral;
        return 0;
    }

    case HISTORIAN_AGG_DURATION_GOOD: {
        double duration_s = 0.0;
        for (size_t i = 0; i < count - 1; i++) {
            if (historian_quality_is_good(points[i].quality)) {
                duration_s += (double)(points[i+1].timestamp.epoch_ms -
                                       points[i].timestamp.epoch_ms) / 1000.0;
            }
        }
        *result = duration_s;
        return 0;
    }

    case HISTORIAN_AGG_DURATION_BAD: {
        double duration_s = 0.0;
        for (size_t i = 0; i < count - 1; i++) {
            if (!historian_quality_is_good(points[i].quality)) {
                duration_s += (double)(points[i+1].timestamp.epoch_ms -
                                       points[i].timestamp.epoch_ms) / 1000.0;
            }
        }
        *result = duration_s;
        return 0;
    }

    case HISTORIAN_AGG_TIME_AVERAGE: {
        return historian_compute_time_weighted_avg(points, count, result);
    }

    default:
        *result = NAN;
        return -99;
    }
}

/* =========================================================================
 * L5: Time-Weighted Average (PI System TimeAvg)
 *
 * The time-weighted average accounts for varying time intervals between
 * data points. Each interval contributes proportionally to its duration.
 *
 * TWAvg = SUM_i avg(v_i, v_{i+1}) * (t_{i+1} - t_i) / total_duration
 *
 * This is the PI Performance Equation "TimeAvg" function.
 * Reference: OSIsoft PI PE Reference (2018), Section 4.3
 * ========================================================================= */

int historian_compute_time_weighted_avg(const historian_data_point_t *points,
                                         size_t count, double *result)
{
    if (!points || !result) return -1;
    if (count < 1) { *result = NAN; return -2; }
    if (count == 1) {
        *result = points[0].value;
        return 0;
    }

    double weighted_sum = 0.0;
    double total_duration_s = 0.0;

    for (size_t i = 0; i < count - 1; i++) {
        if (!isfinite(points[i].value) || !isfinite(points[i+1].value))
            continue;

        double avg_val = (points[i].value + points[i+1].value) * 0.5;
        double dt_s = (double)(points[i+1].timestamp.epoch_ms -
                                points[i].timestamp.epoch_ms) / 1000.0;

        if (dt_s > 0) {
            weighted_sum += avg_val * dt_s;
            total_duration_s += dt_s;
        }
    }

    if (total_duration_s <= 0.0) {
        *result = points[0].value; /* Fall back to first value */
        return 0;
    }

    *result = weighted_sum / total_duration_s;
    return 0;
}

/* =========================================================================
 * L4: ISO 22400-2 Time-Bucketed Aggregation
 * ========================================================================= */

/* Map bucket period enum to milliseconds */
static int64_t bucket_period_to_ms(historian_bucket_period_t period,
                                     int64_t custom_ms)
{
    switch (period) {
    case HISTORIAN_BUCKET_SECOND:    return 1000LL;
    case HISTORIAN_BUCKET_MINUTE:    return 60000LL;
    case HISTORIAN_BUCKET_HOUR:      return 3600000LL;
    case HISTORIAN_BUCKET_SHIFT:     return 43200000LL;  /* 12 hours */
    case HISTORIAN_BUCKET_DAY:       return 86400000LL;
    case HISTORIAN_BUCKET_WEEK:      return 604800000LL;
    case HISTORIAN_BUCKET_MONTH:     return 2592000000LL; /* 30 days approx */
    case HISTORIAN_BUCKET_QUARTER:   return 7776000000LL; /* 90 days approx */
    case HISTORIAN_BUCKET_YEAR:      return 31536000000LL;/* 365 days approx */
    case HISTORIAN_BUCKET_CUSTOM_MS: return custom_ms;
    default: return 3600000LL;
    }
}

int historian_compute_bucketed_aggregate(const historian_aggregate_spec_t *spec,
                                          const historian_data_point_t *points,
                                          size_t count,
                                          historian_bucket_result_set_t *results)
{
    if (!spec || !points || !results) return -1;
    if (count == 0) return -2;

    int64_t bucket_ms = bucket_period_to_ms(spec->bucket.period,
                                              spec->bucket.custom_ms);
    if (bucket_ms <= 0) return -3;

    /* Determine overall time range */
    int64_t start_ms = points[0].timestamp.epoch_ms;
    int64_t end_ms   = points[count-1].timestamp.epoch_ms;

    /* Align first bucket start */
    int64_t bucket_start;
    if (spec->bucket.alignment == HISTORIAN_ALIGN_QUERY_START) {
        bucket_start = start_ms;
    } else {
        /* Align to natural boundary */
        bucket_start = (start_ms / bucket_ms) * bucket_ms;
        if (spec->bucket.alignment == HISTORIAN_ALIGN_CUSTOM) {
            bucket_start += spec->bucket.align_offset_ms;
        }
    }

    /* Iterate over buckets */
    size_t point_idx = 0;
    while (bucket_start <= end_ms) {
        int64_t bucket_end = bucket_start + bucket_ms;

        /* Skip buckets before the first data point */
        if (bucket_end <= start_ms) {
            bucket_start = bucket_end;
            continue;
        }

        /* Collect points in this bucket */
        historian_data_point_t *bucket_points = NULL;
        size_t bucket_cap = 256;
        size_t bucket_cnt = 0;
        bucket_points = (historian_data_point_t *)malloc(
            bucket_cap * sizeof(historian_data_point_t));
        if (!bucket_points) return -4;

        /* Gather points that fall in this bucket */
        while (point_idx < count &&
               points[point_idx].timestamp.epoch_ms < bucket_end) {
            if (points[point_idx].timestamp.epoch_ms >= bucket_start) {
                if (bucket_cnt >= bucket_cap) {
                    bucket_cap *= 2;
                    bucket_points = (historian_data_point_t *)realloc(
                        bucket_points, bucket_cap * sizeof(historian_data_point_t));
                    if (!bucket_points) return -4;
                }
                bucket_points[bucket_cnt++] = points[point_idx];
            }
            point_idx++;
        }

        /* Compute aggregate for this bucket */
        historian_bucket_result_t br;
        memset(&br, 0, sizeof(br));
        br.bucket_start.epoch_ms = bucket_start;
        br.bucket_end.epoch_ms = bucket_end;
        br.sample_count = bucket_cnt;
        br.is_partial = (spec->bucket.exclude_partial &&
                         bucket_end > points[count-1].timestamp.epoch_ms) ? 1 : 0;

        /* Count good-quality samples */
        size_t good_count = 0;
        for (size_t i = 0; i < bucket_cnt; i++) {
            if (historian_quality_is_good(bucket_points[i].quality))
                good_count++;
        }
        br.percent_good = (bucket_cnt > 0)
                          ? (double)good_count / (double)bucket_cnt * 100.0
                          : 0.0;

        /* Compute the specified aggregate */
        double agg_val;
        int ret = historian_compute_aggregate(spec->agg_type,
                                               bucket_points, bucket_cnt,
                                               &agg_val);
        br.agg_value = (ret == 0) ? agg_val : NAN;

        free(bucket_points);

        /* Append result */
        if (!spec->bucket.exclude_partial || !br.is_partial) {
            historian_bucket_result_set_append(results, br);
        }

        bucket_start = bucket_end;

        /* Rewind point_idx for next bucket (overlapping windows not used here) */
        if (point_idx >= count) break;
    }

    return 0;
}

/* =========================================================================
 * L5: Running (Online) Aggregates - Welford's Algorithm
 * ========================================================================= */

/* State structure for running aggregates (256 bytes, as declared). */
typedef struct {
    size_t n;
    double mean;
    double M2;
    double min_val;
    double max_val;
    double sum;
    int64_t start_time_ms;
    int64_t last_time_ms;
    double last_value;
    double weighted_sum;
    double total_duration_s;
} running_agg_state_t;

/* Verify state size fits declared 256 bytes */
typedef char static_assert_state_size[
    (sizeof(running_agg_state_t) <= 256) ? 1 : -1];

int historian_running_aggregate_update(historian_aggregate_type_t agg_type,
                                        const historian_data_point_t *new_point,
                                        void *state)
{
    if (!new_point || !state) return -1;
    (void)agg_type; /* type selection is deferred to finalize */
    running_agg_state_t *rs = (running_agg_state_t *)state;

    if (rs->n == 0) {
        rs->start_time_ms = new_point->timestamp.epoch_ms;
        rs->min_val = new_point->value;
        rs->max_val = new_point->value;
    }

    rs->n++;
    double delta = new_point->value - rs->mean;
    rs->mean += delta / (double)rs->n;
    double delta2 = new_point->value - rs->mean;
    rs->M2 += delta * delta2;
    rs->sum += new_point->value;

    if (new_point->value < rs->min_val) rs->min_val = new_point->value;
    if (new_point->value > rs->max_val) rs->max_val = new_point->value;

    /* For time-weighted: accumulate integral */
    if (rs->n > 1) {
        double avg_val = (rs->last_value + new_point->value) * 0.5;
        double dt_s = (double)(new_point->timestamp.epoch_ms -
                                rs->last_time_ms) / 1000.0;
        if (dt_s > 0) {
            rs->weighted_sum += avg_val * dt_s;
            rs->total_duration_s += dt_s;
        }
    }

    rs->last_time_ms = new_point->timestamp.epoch_ms;
    rs->last_value = new_point->value;

    return 0;
}

int historian_running_aggregate_finalize(historian_aggregate_type_t agg_type,
                                          const void *state, double *result)
{
    if (!state || !result) return -1;
    const running_agg_state_t *rs = (const running_agg_state_t *)state;

    switch (agg_type) {
    case HISTORIAN_AGG_COUNT:   *result = (double)rs->n; return 0;
    case HISTORIAN_AGG_SUM:     *result = rs->sum; return 0;
    case HISTORIAN_AGG_AVERAGE: *result = (rs->n > 0) ? rs->mean : NAN; return 0;
    case HISTORIAN_AGG_MINIMUM: *result = (rs->n > 0) ? rs->min_val : NAN; return 0;
    case HISTORIAN_AGG_MAXIMUM: *result = (rs->n > 0) ? rs->max_val : NAN; return 0;
    case HISTORIAN_AGG_RANGE:   *result = (rs->n > 0) ? rs->max_val - rs->min_val : NAN; return 0;
    case HISTORIAN_AGG_STDDEV:
        *result = (rs->n > 0) ? sqrt(rs->M2 / (double)rs->n) : NAN;
        return 0;
    case HISTORIAN_AGG_VARIANCE:
        *result = (rs->n > 0) ? rs->M2 / (double)rs->n : NAN;
        return 0;
    case HISTORIAN_AGG_STDDEV_SAMPLE:
        *result = (rs->n > 1) ? sqrt(rs->M2 / (double)(rs->n - 1)) : NAN;
        return 0;
    case HISTORIAN_AGG_VARIANCE_SAMPLE:
        *result = (rs->n > 1) ? rs->M2 / (double)(rs->n - 1) : NAN;
        return 0;
    case HISTORIAN_AGG_TIME_AVERAGE:
        *result = (rs->total_duration_s > 0) ? rs->weighted_sum / rs->total_duration_s : NAN;
        return 0;
    case HISTORIAN_AGG_RATE:
        if (rs->n < 2 || rs->total_duration_s <= 0) { *result = NAN; return -2; }
        *result = (rs->last_value - rs->min_val) / rs->total_duration_s;
        return 0;
    case HISTORIAN_AGG_INTEGRAL:
        *result = rs->weighted_sum;
        return 0;
    default:
        *result = NAN;
        return -99;
    }
}

/* =========================================================================
 * L8: Distribution Statistics
 * ========================================================================= */

int historian_compute_distribution_stats(const historian_data_point_t *points,
                                          size_t count,
                                          historian_distribution_stats_t *stats)
{
    if (!points || !stats) return -1;
    memset(stats, 0, sizeof(*stats));

    if (count == 0) return -2;

    /* Pass 1: mean, min, max */
    size_t valid_count = 0;
    double sum = 0.0;
    double min_val = INFINITY, max_val = -INFINITY;

    for (size_t i = 0; i < count; i++) {
        if (!isfinite(points[i].value)) continue;
        valid_count++;
        double v = points[i].value;
        sum += v;
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
    }

    if (valid_count == 0) return -3;

    double mean = sum / (double)valid_count;

    /* Pass 2: higher moments, standard deviation */
    double M2 = 0.0, M3 = 0.0, M4 = 0.0;
    for (size_t i = 0; i < count; i++) {
        if (!isfinite(points[i].value)) continue;
        double diff = points[i].value - mean;
        double diff2 = diff * diff;
        M2 += diff2;
        M3 += diff2 * diff;
        M4 += diff2 * diff2;
    }

    double variance = M2 / (double)valid_count;
    double stddev = sqrt(variance);

    /* Skewness: E[(X-mu)^3] / sigma^3
     * Uses adjusted Fisher-Pearson standardized moment coefficient */
    double skewness = 0.0;
    if (stddev > 1e-12 && valid_count >= 2) {
        double m3 = M3 / (double)valid_count;
        skewness = m3 / (stddev * stddev * stddev);
        /* Apply sample size correction */
        double n = (double)valid_count;
        skewness *= sqrt(n * (n - 1.0)) / (n - 2.0);
    }

    /* Kurtosis: E[(X-mu)^4] / sigma^4 - 3 (excess kurtosis) */
    double kurtosis = 0.0;
    if (variance > 1e-12 && valid_count >= 4) {
        double m4 = M4 / (double)valid_count;
        kurtosis = m4 / (variance * variance) - 3.0;
        /* Bias correction for sample excess kurtosis */
        double n = (double)valid_count;
        kurtosis = ((n - 1.0) / ((n - 2.0) * (n - 3.0))) *
                   ((n + 1.0) * kurtosis + 6.0);
    }

    /* Compute percentiles (requires sorting a copy) */
    double *sorted = (double *)malloc(valid_count * sizeof(double));
    if (!sorted) return -4;

    size_t idx = 0;
    for (size_t i = 0; i < count; i++) {
        if (isfinite(points[i].value)) sorted[idx++] = points[i].value;
    }

    /* Insertion sort */
    for (size_t i = 1; i < valid_count; i++) {
        double key = sorted[i];
        size_t j = i;
        while (j > 0 && sorted[j-1] > key) {
            sorted[j] = sorted[j-1];
            j--;
        }
        sorted[j] = key;
    }

    /* Median (50th percentile) */
    double median;
    if (valid_count % 2 == 0) {
        median = (sorted[valid_count/2 - 1] + sorted[valid_count/2]) * 0.5;
    } else {
        median = sorted[valid_count/2];
    }

    /* 10th and 90th percentiles using linear interpolation */
    /* Compute percentile using linear interpolation between floor/ceil indices */
    {
        double rank10 = 10.0 / 100.0 * (double)(valid_count - 1);
        size_t lo10 = (size_t)floor(rank10);
        size_t hi10 = (size_t)ceil(rank10);
        if (lo10 >= valid_count) lo10 = valid_count - 1;
        if (hi10 >= valid_count) hi10 = valid_count - 1;
        double p10;
        if (lo10 == hi10) {
            p10 = sorted[lo10];
        } else {
            double frac10 = rank10 - (double)lo10;
            p10 = sorted[lo10] + frac10 * (sorted[hi10] - sorted[lo10]);
        }

        double rank90 = 90.0 / 100.0 * (double)(valid_count - 1);
        size_t lo90 = (size_t)floor(rank90);
        size_t hi90 = (size_t)ceil(rank90);
        if (lo90 >= valid_count) lo90 = valid_count - 1;
        if (hi90 >= valid_count) hi90 = valid_count - 1;
        double p90;
        if (lo90 == hi90) {
            p90 = sorted[lo90];
        } else {
            double frac90 = rank90 - (double)lo90;
            p90 = sorted[lo90] + frac90 * (sorted[hi90] - sorted[lo90]);
        }

        /* Assign to stats after the block to avoid shadowing */
        stats->p10 = p10;
        stats->p90 = p90;
    }

    free(sorted);

    /* Duration */
    double duration_hr = 0.0;
    if (count >= 2) {
        duration_hr = (double)(points[count-1].timestamp.epoch_ms -
                                points[0].timestamp.epoch_ms) / 3600000.0;
    }

    stats->mean = mean;
    stats->median = median;
    stats->stddev = stddev;
    stats->skewness = skewness;
    stats->kurtosis = kurtosis;
    stats->min_val = min_val;
    stats->max_val = max_val;
    /* p10 and p90 already assigned inside block above */
    stats->count = valid_count;
    stats->duration_hr = duration_hr;

    return 0;
}

/* =========================================================================
 * Bucket Result Set Memory Management
 * ========================================================================= */

void historian_bucket_result_set_init(historian_bucket_result_set_t *brs)
{
    if (!brs) return;
    brs->buckets = NULL;
    brs->count = 0;
    brs->capacity = 0;
}

int historian_bucket_result_set_append(historian_bucket_result_set_t *brs,
                                        historian_bucket_result_t result)
{
    if (!brs) return -1;
    if (brs->capacity == 0) {
        brs->buckets = (historian_bucket_result_t *)malloc(
            64 * sizeof(historian_bucket_result_t));
        if (!brs->buckets) return -1;
        brs->capacity = 64;
    }
    if (brs->count >= brs->capacity) {
        size_t new_cap = brs->capacity * 2;
        historian_bucket_result_t *new_b = (historian_bucket_result_t *)realloc(
            brs->buckets, new_cap * sizeof(historian_bucket_result_t));
        if (!new_b) return -1;
        brs->buckets = new_b;
        brs->capacity = new_cap;
    }
    brs->buckets[brs->count++] = result;
    return 0;
}

void historian_bucket_result_set_destroy(historian_bucket_result_set_t *brs)
{
    if (!brs) return;
    free(brs->buckets);
    brs->buckets = NULL;
    brs->count = 0;
    brs->capacity = 0;
}
