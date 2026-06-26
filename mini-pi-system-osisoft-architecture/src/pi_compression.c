/**
 * pi_compression.c - PI Data Compression Algorithms
 * Implements multiple time-series compression strategies.
 * Knowledge: L2-L5  MIT 6.302  Stanford ENGR205  RWTH Aachen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>

/* ─── Deadband Compression ────────────────────────────────────── */
/** Absolute deadband filter. Only pass value if |new - last| > deadband.
 *  Simplest form of data reduction for process signals.
 *  Returns 1 if value should be stored, 0 if suppressed. */
int pi_comp_deadband_absolute(double new_val, double last_val, double deadband) {
    if (deadband <= 0.0) return 1;
    return fabs(new_val - last_val) > deadband ? 1 : 0;
}

/** Percentage deadband filter.
 *  deadband_pct is percentage of engineering span (zero to span). */
int pi_comp_deadband_percent(double new_val, double last_val,
                              double deadband_pct, double eu_span) {
    if (eu_span <= 0.0) return 1;
    double abs_db = fabs(deadband_pct) * eu_span / 100.0;
    return fabs(new_val - last_val) > abs_db ? 1 : 0;
}

/* ─── Slope-Based Compression ─────────────────────────────────── */
/** Rate-of-change filter: only archive if slope exceeds threshold.
 *  Useful for detecting process upsets while ignoring noise.
 *  Returns 1 if significant, 0 if within normal variation. */
int pi_comp_slope_filter(double prev_val, double prev_time,
                          double curr_val, double curr_time,
                          double max_slope) {
    double dt = curr_time - prev_time;
    if (fabs(dt) < 1e-9) return fabs(curr_val - prev_val) > 0.0 ? 1 : 0;
    double slope = (curr_val - prev_val) / dt;
    return fabs(slope) > max_slope ? 1 : 0;
}

/* ─── Boxcar / Downsampling ───────────────────────────────────── */
/** Boxcar downsampling: keep 1 sample every N.
 *  Factor 10 means keep 1 in 10 samples (90% reduction).
 *  Used for long-term storage or data export. */
void pi_comp_boxcar_downsample(const double *input, int n, int factor,
                                double *output, int *out_count) {
    if (!input || !output || !out_count || n <= 0 || factor <= 0) {
        if (out_count) *out_count = 0;
        return;
    }
    int i, count = 0;
    for (i = 0; i < n; i += factor) output[count++] = input[i];
    *out_count = count;
}

/** Boxcar averaging: average every N samples into 1.
 *  Better than simple downsampling for noisy signals. */
void pi_comp_boxcar_average(const double *input, int n, int window,
                              double *output, int *out_count) {
    if (!input || !output || !out_count || n <= 0 || window <= 0) {
        if (out_count) *out_count = 0;
        return;
    }
    int i, count = 0;
    for (i = 0; i + window <= n; i += window) {
        double sum = 0.0;
        int j;
        for (j = 0; j < window; j++) sum += input[i + j];
        output[count++] = sum / (double)window;
    }
    *out_count = count;
}

/* ─── Min/Max Resampling ──────────────────────────────────────── */
/** Min-max resampling: for each window, keep both min and max.
 *  Preserves signal envelope better than averaging.
 *  Used for process boundary monitoring. */
void pi_comp_minmax_resample(const double *input, int n, int window,
                              double *output, int *out_count) {
    if (!input || !output || !out_count || n <= 0 || window <= 0) {
        if (out_count) *out_count = 0;
        return;
    }
    int i, count = 0;
    for (i = 0; i + window <= n; i += window) {
        double min_v = input[i], max_v = input[i];
        int j;
        for (j = 1; j < window; j++) {
            double v = input[i + j];
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
        }
        output[count++] = min_v;
        output[count++] = max_v;
    }
    *out_count = count;
}

/* ─── Zero-Order Hold ─────────────────────────────────────────── */
/** Zero-order hold (step) compression: only store value when it changes.
 *  For digital/discrete signals. Returns compressed array length. */
int pi_comp_zero_order_hold(const double *input, int n,
                             double *output_values,
                             int *output_indices, int max_out) {
    if (!input || !output_values || !output_indices || n <= 0) return 0;
    int count = 0;
    output_values[count] = input[0];
    output_indices[count] = 0;
    count++;
    int i;
    for (i = 1; i < n && count < max_out; i++) {
        if (fabs(input[i] - input[i-1]) > 1e-12) {
            output_values[count] = input[i];
            output_indices[count] = i;
            count++;
        }
    }
    return count;
}

/* ─── Piecewise Linear Approximation ──────────────────────────── */
/** Ramer-Douglas-Peucker algorithm for polyline simplification.
 *  Reduces number of points while preserving overall shape.
 *  epsilon: maximum perpendicular distance allowed.
 *  Classic algorithm for map/trend simplification, O(n^2). */
static double rdp_perp_distance(double x1, double y1, double x2, double y2,
                                  double px, double py) {
    double dx = x2 - x1, dy = y2 - y1;
    double len_sq = dx*dx + dy*dy;
    if (len_sq < 1e-12) return sqrt((px-x1)*(px-x1) + (py-y1)*(py-y1));
    double t = ((px-x1)*dx + (py-y1)*dy) / len_sq;
    if (t < 0.0) return sqrt((px-x1)*(px-x1) + (py-y1)*(py-y1));
    if (t > 1.0) return sqrt((px-x2)*(px-x2) + (py-y2)*(py-y2));
    double proj_x = x1 + t*dx, proj_y = y1 + t*dy;
    return sqrt((px-proj_x)*(px-proj_x) + (py-proj_y)*(py-proj_y));
}

static void rdp_recurse(const double *x, const double *y, int start, int end,
                         double epsilon, int *kept) {
    if (end <= start + 1) return;
    int i, max_idx = start + 1;
    double max_dist = 0.0;
    for (i = start + 1; i < end; i++) {
        double d = rdp_perp_distance(x[start], y[start],
                                      x[end], y[end], x[i], y[i]);
        if (d > max_dist) { max_dist = d; max_idx = i; }
    }
    if (max_dist > epsilon) {
        rdp_recurse(x, y, start, max_idx, epsilon, kept);
        rdp_recurse(x, y, max_idx, end, epsilon, kept);
    } else {
        for (i = start + 1; i < end; i++) kept[i] = 0;
    }
}

int pi_comp_rdp_simplify(const double *x, const double *y, int n,
                          double epsilon, int *kept_mask) {
    if (!x || !y || !kept_mask || n < 2) return 0;
    int i;
    for (i = 0; i < n; i++) kept_mask[i] = 1;
    kept_mask[0] = 1; kept_mask[n-1] = 1;
    rdp_recurse(x, y, 0, n-1, epsilon, kept_mask);
    int count = 0;
    for (i = 0; i < n; i++) if (kept_mask[i]) count++;
    return count;
}

/* ─── Compression Ratio ───────────────────────────────────────── */
double pi_comp_compression_ratio(int original_count, int compressed_count) {
    if (original_count <= 0) return 0.0;
    return (double)(original_count - compressed_count) / (double)original_count;
}

double pi_comp_space_savings_pct(int original_count, int compressed_count) {
    return 100.0 * pi_comp_compression_ratio(original_count, compressed_count);
}

/* ─── Signal-to-Noise ─────────────────────────────────────────── */
/** Estimate signal-to-noise ratio for assessing compression quality.
 *  SNR = 10 * log10(signal_variance / noise_variance).
 *  Higher SNR means less noise, better compression possible. */
double pi_comp_estimate_snr(const double *data, int n) {
    if (!data || n < 2) return 0.0;
    double mean = 0.0;
    int i;
    for (i = 0; i < n; i++) mean += data[i];
    mean /= (double)n;
    double sig_var = 0.0, noise_var = 0.0;
    for (i = 0; i < n; i++) {
        double d = data[i] - mean;
        sig_var += d * d;
    }
    sig_var /= (double)n;
    for (i = 1; i < n; i++) {
        double diff = data[i] - data[i-1];
        noise_var += diff * diff;
    }
    noise_var /= (double)(n - 1);
    if (noise_var < 1e-12) return 100.0;
    return 10.0 * log10(sig_var / noise_var);
}

/* ─── Deadband with Timeout ─────────────────────────────────────── */
int pi_comp_deadband_timeout(double new_val, double last_val, double deadband,
                              double last_time, double curr_time, double max_interval) {
    if (max_interval > 0.0 && (curr_time - last_time) >= max_interval) return 1;
    if (deadband <= 0.0) return 1;
    return fabs(new_val - last_val) > deadband ? 1 : 0;
}

/* ─── Piecewise Constant Approximation ─────────────────────────── */
int pi_comp_piecewise_constant(const double *data, int n, double tolerance,
                                 double *output, int max_out) {
    if (!data || !output || n <= 0 || max_out <= 0) return 0;
    int count = 0, i;
    output[count++] = data[0];
    for (i = 1; i < n && count < max_out; i++)
        if (fabs(data[i] - output[count-1]) > tolerance) output[count++] = data[i];
    return count;
}

/* ─── Compression Quality Metrics ───────────────────────────────── */
double pi_comp_nrmse(const double *original, int n, const double *reconstructed, int m) {
    if (!original || !reconstructed || n < 2) return 0.0;
    double mean = 0.0; int i;
    for (i = 0; i < n; i++) mean += original[i];
    mean /= (double)n;
    double rmse = 0.0, range = 0.0;
    double omin = original[0], omax = original[0];
    for (i = 0; i < n; i++) {
        if (original[i] < omin) omin = original[i];
        if (original[i] > omax) omax = original[i];
    }
    range = omax - omin;
    if (range < 1e-12) return 0.0;
    for (i = 0; i < n && i < m; i++) {
        double diff = original[i] - reconstructed[i];
        rmse += diff * diff;
    }
    return sqrt(rmse / (double)n) / range;
}
