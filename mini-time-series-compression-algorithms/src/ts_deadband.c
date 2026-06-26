/**
 * @file ts_deadband.c
 * @brief Deadband Compression Implementation — Threshold-Based Data Reduction
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge: L2 Core Concepts, L5 Algorithms, L6 Canonical Problems
 *
 * Implements all deadband filter modes: absolute, percent, time-based,
 * combined, rate-of-change, and adaptive. Each filter decision is O(1).
 *
 * Curriculum: MIT 6.302, Stanford ENGR205, RWTH Aachen
 */

#include "ts_deadband.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * L5: Default Configuration
 * ------------------------------------------------------------------------- */

static const ts_deadband_config_t DEADBAND_DEFAULTS = {
    .mode = TS_DEADBAND_NONE,
    .threshold_abs = 0.0,
    .threshold_pct = 0.0,
    .span_min = 0.0,
    .span_max = 100.0,
    .threshold_time_us = 0,
    .rate_threshold = 0.0,
    .enable_exception = true,
    .enable_snapshot = false,
    .adaptive_learning_rate = 0.01
};

/* ---------------------------------------------------------------------------
 * L2: Initialization and Reset
 * ------------------------------------------------------------------------- */

int ts_deadband_init(ts_deadband_state_t *state,
                      const ts_deadband_config_t *config)
{
    if (!state) return -1;

    memset(state, 0, sizeof(*state));

    if (config) {
        state->config = *config;
    } else {
        state->config = DEADBAND_DEFAULTS;
    }

    state->initialized = false;
    state->consecutive_discards = 0;
    memset(&state->stats, 0, sizeof(state->stats));

    return 0;
}

int ts_deadband_reset(ts_deadband_state_t *state)
{
    if (!state) return -1;

    state->initialized = false;
    state->consecutive_discards = 0;
    memset(&state->last_archived, 0, sizeof(state->last_archived));
    memset(&state->last_received, 0, sizeof(state->last_received));
    memset(&state->stats, 0, sizeof(state->stats));

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Absolute Deadband Filter
 *
 * Decision rule: |x_new - x_archived| >= epsilon
 *
 * This is the simplest deadband. The epsilon threshold represents the
 * minimum meaningful change in the process variable. Values of epsilon
 * are typically chosen as 1-5% of the engineering span.
 * ------------------------------------------------------------------------- */

static bool deadband_absolute_check(const ts_deadband_state_t *state,
                                     const ts_data_point_t *input)
{
    double delta = fabs(input->value - state->last_archived.value);
    return (delta >= state->config.threshold_abs);
}

/* ---------------------------------------------------------------------------
 * L5: Percent Deadband Filter
 *
 * Decision rule: |x_new - x_archived| >= (pct/100) * span
 *   where span = span_max - span_min
 *
 * Percent deadband is commonly used when the same relative tolerance
 * should apply across different measurement ranges.
 * ------------------------------------------------------------------------- */

static bool deadband_percent_check(const ts_deadband_state_t *state,
                                    const ts_data_point_t *input)
{
    double span = state->config.span_max - state->config.span_min;
    if (span <= 0.0) return true;  /* Invalid span: archive everything */

    double delta = fabs(input->value - state->last_archived.value);
    double threshold = state->config.threshold_pct * 0.01 * span;

    return (delta >= threshold);
}

/* ---------------------------------------------------------------------------
 * L5: Time-Based Deadband Filter
 *
 * Decision rule: dt >= threshold_time_us
 *
 * Ensures points are archived at a minimum time interval. This is used
 * when data is sampled much faster than useful for trending. Works
 * independently of value changes.
 * ------------------------------------------------------------------------- */

static bool deadband_time_check(const ts_deadband_state_t *state,
                                 const ts_data_point_t *input)
{
    int64_t dt = input->epoch_us - state->last_archived.epoch_us;
    if (dt < 0) return true;  /* Out-of-order: archive */

    return (dt >= state->config.threshold_time_us);
}

/* ---------------------------------------------------------------------------
 * L5: Rate-of-Change Deadband Filter
 *
 * Decision rule: |dx/dt| >= rate_threshold
 *
 * Archives only when the signal is changing fast enough to be
 * interesting. Suppresses slow drifts that don't represent
 * process dynamics.
 * ------------------------------------------------------------------------- */

static bool deadband_rate_check(const ts_deadband_state_t *state,
                                 const ts_data_point_t *input)
{
    int64_t dt = input->epoch_us - state->last_archived.epoch_us;
    if (dt <= 0) return true;  /* Can't compute rate: archive */

    double delta = fabs(input->value - state->last_archived.value);
    double rate  = delta / ((double)dt * 1e-6);  /* units per second */

    return (rate >= state->config.rate_threshold);
}

/* ---------------------------------------------------------------------------
 * L5: Adaptive Deadband Filter
 *
 * Adjusts the deadband threshold epsilon based on recent signal variance.
 * In regions of low noise, the deadband tightens (more precision).
 * In regions of high noise, the deadband widens (more compression).
 *
 * Adaptive update rule (exponential moving average of squared deltas):
 *   sigma2_new = (1-alpha) * sigma2_old + alpha * delta^2
 *   epsilon_adaptive = k * sqrt(sigma2_new)
 *
 * where k is typically 2-3 (95-99% confidence interval for Gaussian noise).
 * ------------------------------------------------------------------------- */

static double deadband_adaptive_sigma_sq = 0.0;

static bool deadband_adaptive_check(ts_deadband_state_t *state,
                                     const ts_data_point_t *input)
{
    double delta = input->value - state->last_received.value;
    double alpha = state->config.adaptive_learning_rate;

    /* Clamp alpha to (0, 1] */
    if (alpha <= 0.0) alpha = 0.01;
    if (alpha > 1.0)  alpha = 1.0;

    /* Update variance estimate using EMA */
    deadband_adaptive_sigma_sq = (1.0 - alpha) * deadband_adaptive_sigma_sq
                                  + alpha * delta * delta;

    double sigma = sqrt(deadband_adaptive_sigma_sq);
    if (sigma < 1e-12) sigma = 1e-12;  /* Avoid division by zero */

    double adaptive_epsilon = 3.0 * sigma;  /* 3-sigma = 99.7% CI */

    double deviation = fabs(input->value - state->last_archived.value);
    return (deviation >= adaptive_epsilon);
}

/* ---------------------------------------------------------------------------
 * L2: Core Filter Function
 *
 * Dispatches to the appropriate deadband check based on the configured
 * mode. Always archives the first point (initialization anchor).
 * ------------------------------------------------------------------------- */

int ts_deadband_filter(ts_deadband_state_t *state,
                        const ts_data_point_t *input,
                        bool *archived)
{
    if (!state || !input || !archived) return -1;

    state->stats.points_received++;
    state->last_received = *input;

    /* Always archive the first point */
    if (!state->initialized) {
        state->last_archived = *input;
        state->last_archive_ts = input->epoch_us;
        state->initialized = true;
        state->stats.points_archived++;
        state->consecutive_discards = 0;
        *archived = true;
        return 0;
    }

    /* Quality exception: force-archive on quality change */
    if (state->config.enable_exception) {
        if (input->quality != state->last_archived.quality) {
            state->last_archived = *input;
            state->last_archive_ts = input->epoch_us;
            state->stats.points_archived++;
            state->consecutive_discards = 0;
            *archived = true;
            return 0;
        }
    }

    /* Dispatch based on deadband mode */
    bool should_archive = false;

    switch (state->config.mode) {
    case TS_DEADBAND_NONE:
        should_archive = true;
        break;

    case TS_DEADBAND_ABSOLUTE:
        should_archive = deadband_absolute_check(state, input);
        break;

    case TS_DEADBAND_PERCENT:
        should_archive = deadband_percent_check(state, input);
        break;

    case TS_DEADBAND_TIME:
        should_archive = deadband_time_check(state, input);
        break;

    case TS_DEADBAND_COMBINED:
        should_archive = deadband_absolute_check(state, input)
                      && deadband_time_check(state, input);
        break;

    case TS_DEADBAND_RATE_OF_CHANGE:
        should_archive = deadband_rate_check(state, input);
        break;

    case TS_DEADBAND_ADAPTIVE:
        should_archive = deadband_adaptive_check(state, input);
        break;

    default:
        should_archive = true;
        break;
    }

    if (should_archive) {
        state->last_archived = *input;
        state->last_archive_ts = input->epoch_us;
        state->stats.points_archived++;
        state->consecutive_discards = 0;
    } else {
        state->stats.points_discarded++;
        state->consecutive_discards++;
    }

    *archived = should_archive;
    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Batch Processing
 *
 * Applies the deadband filter to an entire array of points.
 * The output array receives only the points that pass the filter.
 *
 * Complexity: O(n) — single linear scan.
 * Space: O(1) auxiliary — only the filter state is maintained.
 * ------------------------------------------------------------------------- */

int ts_deadband_filter_batch(ts_deadband_state_t *state,
                              const ts_data_point_t *inputs,
                              size_t num_input,
                              ts_data_point_t *outputs,
                              size_t *num_output)
{
    if (!state || !inputs || !outputs || !num_output) return -1;

    *num_output = 0;

    for (size_t i = 0; i < num_input; i++) {
        bool archived = false;
        int ret = ts_deadband_filter(state, &inputs[i], &archived);
        if (ret != 0) return ret;

        if (archived) {
            if (*num_output >= num_input) return -1;  /* Output overflow */
            outputs[*num_output] = state->last_archived;
            (*num_output)++;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Effective Threshold Computation
 * ------------------------------------------------------------------------- */

double ts_deadband_effective_threshold(const ts_deadband_state_t *state,
                                         double value)
{
    (void)value;
    if (!state) return 0.0;

    switch (state->config.mode) {
    case TS_DEADBAND_ABSOLUTE:
        return state->config.threshold_abs;

    case TS_DEADBAND_PERCENT: {
        double span = state->config.span_max - state->config.span_min;
        if (span <= 0.0) return 0.0;
        return state->config.threshold_pct * 0.01 * span;
    }

    case TS_DEADBAND_ADAPTIVE: {
        double sigma = sqrt(deadband_adaptive_sigma_sq);
        if (sigma < 1e-12) sigma = 1e-12;
        return 3.0 * sigma;
    }

    default:
        return 0.0;
    }
}

/* ---------------------------------------------------------------------------
 * L2: What-If Query — Would Archive?
 *
 * Pure function: evaluates the deadband decision without modifying state.
 * Useful for tuning and sensitivity analysis.
 * ------------------------------------------------------------------------- */

bool ts_deadband_would_archive(const ts_deadband_state_t *state,
                                const ts_data_point_t *input)
{
    if (!state || !input) return false;
    if (!state->initialized) return true;

    bool archived = false;
    ts_deadband_state_t tmp = *state;  /* Shallow copy for query */
    ts_deadband_filter(&tmp, input, &archived);

    return archived;
}

/* ---------------------------------------------------------------------------
 * L2: Statistics Retrieval
 *
 * Updates the compression ratio before returning stats.
 * compression_ratio = points_received / max(1, points_archived)
 * ------------------------------------------------------------------------- */

int ts_deadband_get_stats(const ts_deadband_state_t *state,
                           ts_compression_stats_t *stats)
{
    if (!state || !stats) return -1;

    *stats = state->stats;

    if (stats->points_archived > 0) {
        stats->compression_ratio = (double)stats->points_received
                                   / (double)stats->points_archived;
    } else {
        stats->compression_ratio = 1.0;
    }

    /* Estimate bytes saved: each discarded point saves sizeof(ts_data_point_t) */
    double bytes_original = (double)stats->points_received
                            * (double)sizeof(ts_data_point_t);
    double bytes_compressed = (double)stats->points_archived
                              * (double)sizeof(ts_data_point_t);
    if (bytes_original > 0.0) {
        stats->bytes_saved_pct = 100.0 * (1.0 - bytes_compressed / bytes_original);
    } else {
        stats->bytes_saved_pct = 0.0;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Theoretical Compression Ratio Estimation
 *
 * For Gaussian i.i.d. increments with variance sigma^2 and deadband epsilon:
 *
 *   P(|delta| < epsilon) = 2*Phi(epsilon/sigma) - 1
 *   P(archive) = 1 - P(|delta| < epsilon) = 2*(1 - Phi(epsilon/sigma))
 *   Expected compression ratio = 1 / P(archive)
 *
 * This uses the standard normal CDF approximation via the error function.
 *
 * Reference: Abramowitz & Stegun, Handbook of Mathematical Functions, §7.1
 * ------------------------------------------------------------------------- */

/* Standard normal CDF using error function (Horner's method approximation) */
static double normal_cdf_approx(double x)
{
    /* Abramowitz & Stegun 7.1.26 approximation: max error 1.5e-7 */
    const double a1 =  0.254829592;
    const double a2 = -0.284496736;
    const double a3 =  1.421413741;
    const double a4 = -1.453152027;
    const double a5 =  1.061405429;
    const double p  =  0.3275911;

    int sign = (x < 0) ? -1 : 1;
    x = fabs(x) / sqrt(2.0);

    double t = 1.0 / (1.0 + p * x);
    double y = 1.0 - (((((a5*t + a4)*t) + a3)*t + a2)*t + a1)*t * exp(-x*x);

    return 0.5 * (1.0 + sign * y);
}

double ts_deadband_estimate_compression(double threshold,
                                          double sigma_increment)
{
    if (threshold <= 0.0) return 1.0;
    if (sigma_increment <= 0.0) return 1.0;

    double z = threshold / sigma_increment;

    /* P(|delta| < epsilon) = 2*Phi(z) - 1 */
    double p_within = 2.0 * normal_cdf_approx(z) - 1.0;

    /* P(archive) = 1 - P(within) */
    double p_archive = 1.0 - p_within;

    if (p_archive < 1e-12) return 1e12;  /* Avoid division by zero */

    return 1.0 / p_archive;
}

/* ---------------------------------------------------------------------------
 * L6: Reconstruction Error Computation
 *
 * Given original and archived (subsampled) point arrays, compute
 * the RMSE under zero-order hold reconstruction:
 *
 *   For each original point at t_i, the reconstructed value is the
 *   value of the most recent archived point with timestamp <= t_i.
 *
 * Complexity: O(n + m) using two-pointer scan.
 * ------------------------------------------------------------------------- */

int ts_deadband_reconstruction_error(const ts_data_point_t *original,
                                       size_t num_original,
                                       const ts_data_point_t *archived,
                                       size_t num_archived,
                                       double *rmse,
                                       double *max_error)
{
    if (!original || !archived || !rmse || !max_error) return -1;
    if (num_original == 0 || num_archived == 0) return -1;

    double sum_sq = 0.0;
    double max_err = 0.0;
    size_t arch_idx = 0;

    for (size_t i = 0; i < num_original; i++) {
        /* Advance archive pointer to last point at or before original[i] */
        while (arch_idx + 1 < num_archived
               && archived[arch_idx + 1].epoch_us <= original[i].epoch_us) {
            arch_idx++;
        }

        double reconstructed = archived[arch_idx].value;
        double error = original[i].value - reconstructed;
        double abs_err = fabs(error);

        sum_sq += error * error;
        if (abs_err > max_err) max_err = abs_err;
    }

    *rmse = sqrt(sum_sq / (double)num_original);
    *max_error = max_err;

    return 0;
}

/* ---------------------------------------------------------------------------
 * L2: Configuration Management
 * ------------------------------------------------------------------------- */

int ts_deadband_set_config(ts_deadband_state_t *state,
                            const ts_deadband_config_t *config,
                            bool reset_stats)
{
    if (!state || !config) return -1;

    state->config = *config;

    if (reset_stats) {
        memset(&state->stats, 0, sizeof(state->stats));
        state->consecutive_discards = 0;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L3: Quality-from-Staleness Heuristic
 *
 * Time since last archive determines data staleness:
 *   - Fresh (recent): GOOD quality
 *   - Stale (between fresh and max): UNCERTAIN quality
 *   - Expired (beyond max): BAD quality
 *
 * Combined with the deadband epsilon to determine uncertainty bounds.
 * ------------------------------------------------------------------------- */

uint8_t ts_deadband_quality_from_staleness(int64_t time_since_archive_us,
                                             double epsilon,
                                             int64_t max_staleness_us)
{
    (void)epsilon;
    if (time_since_archive_us < 0) return TS_QUALITY_BAD;

    /* Within 10% of max staleness: GOOD */
    int64_t good_limit_us = max_staleness_us / 10;
    if (time_since_archive_us <= good_limit_us) {
        return TS_QUALITY_GOOD;
    }

    /* Within max staleness: UNCERTAIN */
    if (time_since_archive_us <= max_staleness_us) {
        return TS_QUALITY_UNCERTAIN;
    }

    /* Beyond max staleness: BAD */
    return TS_QUALITY_BAD;
}
