/** pi_data_analysis.c - PI Data Analysis & Retrieval Functions
 * Time-series analysis: moving average, rate-of-change, interpolation.
 * Knowledge: L2-L5  MIT 6.302  Stanford ENGR205
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "../include/pi_da_types.h"

/* ─── Moving Average ─────────────────────────────────────────── */
/** Simple moving average over a window of values.
 *  For PI tags, used to smooth noisy measurements before display.
 *  Complexity: O(n*window) naive, O(n) with running sum. */
void pi_moving_average(const double *input, int n, int window, double *output) {
    if (!input || !output || n <= 0 || window <= 0) return;
    if (window > n) window = n;
    int i, j;
    for (i = 0; i < n; i++) {
        double sum = 0.0;
        int count = 0;
        int start = i - window/2;
        if (start < 0) start = 0;
        int end = i + window/2;
        if (end >= n) end = n - 1;
        for (j = start; j <= end; j++) { sum += input[j]; count++; }
        output[i] = count > 0 ? sum / (double)count : input[i];
    }
}

/** Exponential moving average: EMA_t = alpha*value + (1-alpha)*EMA_{t-1}
 *  alpha = 2/(N+1) for N-period equivalent.
 *  Widely used in PI for signal filtering. */
void pi_exponential_moving_average(const double *input, int n, double alpha, double *output) {
    if (!input || !output || n <= 0) return;
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;
    output[0] = input[0];
    int i;
    for (i = 1; i < n; i++)
        output[i] = alpha * input[i] + (1.0 - alpha) * output[i-1];
}

/* ─── Rate of Change ─────────────────────────────────────────── */
/** Compute rate of change: (v1 - v0) / (t1 - t0) in units/second.
 *  Used for trend detection and alarm condition checks. */
double pi_rate_of_change(double v0, double v1, double t0, double t1) {
    double dt = t1 - t0;
    if (fabs(dt) < 1e-9) return 0.0;
    return (v1 - v0) / dt;
}

/** Rate of change per minute (common industrial unit). */
double pi_rate_of_change_per_minute(double v0, double v1, double dt_seconds) {
    if (fabs(dt_seconds) < 1e-9) return 0.0;
    return (v1 - v0) * 60.0 / dt_seconds;
}

/* ─── Statistical Functions ───────────────────────────────────── */
/** Arithmetic mean. Uses Kahan compensated summation for numerical stability. */
double pi_statistics_mean(const double *data, int n) {
    if (!data || n <= 0) return 0.0;
    double sum = 0.0, c = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        double y = data[i] - c;
        double t = sum + y;
        c = (t - sum) - y;
        sum = t;
    }
    return sum / (double)n;
}

/** Sample standard deviation (Bessel corrected, n-1 denominator). */
double pi_statistics_stddev(const double *data, int n) {
    if (!data || n < 2) return 0.0;
    double mean = pi_statistics_mean(data, n);
    double sum_sq = 0.0, c = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        double d = data[i] - mean;
        double y = d * d - c;
        double t = sum_sq + y;
        c = (t - sum_sq) - y;
        sum_sq = t;
    }
    return sqrt(sum_sq / (double)(n - 1));
}

/** Minimum value in array. */
double pi_statistics_min(const double *data, int n) {
    if (!data || n <= 0) return 0.0;
    double m = data[0];
    int i; for (i = 1; i < n; i++) if (data[i] < m) m = data[i];
    return m;
}

/** Maximum value in array. */
double pi_statistics_max(const double *data, int n) {
    if (!data || n <= 0) return 0.0;
    double m = data[0];
    int i; for (i = 1; i < n; i++) if (data[i] > m) m = data[i];
    return m;
}

/* ─── Linear Regression ──────────────────────────────────────── */
/** Simple OLS linear regression: y = a + b*x.
 *  Returns r-squared (coefficient of determination).
 *  Used for trend analysis and sensor drift detection in PI. */
double pi_linear_regression(const double *x, const double *y, int n,
                              double *slope, double *intercept) {
    if (!x || !y || !slope || !intercept || n < 2) {
        if (slope) *slope = 0.0;
        if (intercept) *intercept = 0.0;
        return 0.0;
    }
    double sx = 0.0, sy = 0.0, sxx = 0.0, syy = 0.0, sxy = 0.0;
    int i;
    for (i = 0; i < n; i++) {
        sx += x[i]; sy += y[i];
        sxx += x[i] * x[i]; syy += y[i] * y[i];
        sxy += x[i] * y[i];
    }
    double denom = (double)n * sxx - sx * sx;
    if (fabs(denom) < 1e-12) { *slope = 0.0; *intercept = sy / (double)n; return 0.0; }
    *slope = ((double)n * sxy - sx * sy) / denom;
    *intercept = (sy - (*slope) * sx) / (double)n;
    double ss_res = 0.0, ss_tot = 0.0;
    double y_mean = sy / (double)n;
    for (i = 0; i < n; i++) {
        double y_pred = (*intercept) + (*slope) * x[i];
        ss_res += (y[i] - y_pred) * (y[i] - y_pred);
        ss_tot += (y[i] - y_mean) * (y[i] - y_mean);
    }
    return ss_tot > 0.0 ? 1.0 - ss_res / ss_tot : 0.0;
}

/* ─── Integration / Totalization ────────────────────────────── */
/** Trapezoidal integration: sum of trapezoid areas between consecutive points.
 *  Used in PI for flow totalization (flow rate -> volume).
 *  total = sum_i (t_{i+1} - t_i) * (v_i + v_{i+1}) / 2 */
double pi_trapezoidal_integrate(const double *times, const double *values, int n) {
    if (!times || !values || n < 2) return 0.0;
    double total = 0.0;
    int i;
    for (i = 0; i < n - 1; i++) {
        double dt = times[i+1] - times[i];
        if (dt < 0.0) dt = -dt;
        total += dt * (values[i] + values[i+1]) / 2.0;
    }
    return total;
}

/** Runge-Kutta 4th order for ODE integration.
 *  dy/dt = f(t, y). Steps from t0 to t1 with nsteps intermediate evaluations.
 *  Useful for model-based prediction in PI Analytics. */
double pi_rk4_integrate(double t0, double y0, double t1, int nsteps,
                          double (*f)(double t, double y)) {
    if (!f || nsteps <= 0) return y0;
    double h = (t1 - t0) / (double)nsteps;
    double t = t0, y = y0;
    int i;
    for (i = 0; i < nsteps; i++) {
        double k1 = f(t, y);
        double k2 = f(t + h/2.0, y + h*k1/2.0);
        double k3 = f(t + h/2.0, y + h*k2/2.0);
        double k4 = f(t + h, y + h*k3);
        y += h * (k1 + 2.0*k2 + 2.0*k3 + k4) / 6.0;
        t += h;
    }
    return y;
}

/* ─── Outlier Detection ──────────────────────────────────────── */
/** IQR-based outlier detection.
 *  Returns 1 if value is an outlier, 0 if normal.
 *  outlier if value < Q1 - 1.5*IQR or value > Q3 + 1.5*IQR */
int pi_is_outlier(const double *data, int n, double value) {
    if (!data || n < 4) return 0;
    double *sorted = (double*)malloc((size_t)n * sizeof(double));
    if (!sorted) return 0;
    memcpy(sorted, data, (size_t)n * sizeof(double));
    /* Simple insertion sort for small n */
    int i, j;
    for (i = 1; i < n; i++) {
        double key = sorted[i];
        for (j = i - 1; j >= 0 && sorted[j] > key; j--) sorted[j+1] = sorted[j];
        sorted[j+1] = key;
    }
    double q1 = sorted[n/4];
    double q3 = sorted[3*n/4];
    double iqr = q3 - q1;
    double lower = q1 - 1.5 * iqr;
    double upper = q3 + 1.5 * iqr;
    free(sorted);
    return (value < lower || value > upper) ? 1 : 0;
}

/** Z-score normalization. Returns (value - mean) / stddev. */
double pi_zscore(const double *data, int n, double value) {
    if (!data || n < 2) return 0.0;
    double mean = pi_statistics_mean(data, n);
    double sd = pi_statistics_stddev(data, n);
    if (fabs(sd) < 1e-12) return 0.0;
    return (value - mean) / sd;
}

/* ─── Time-Weighted Average ──────────────────────────────────── */
/** Time-weighted average over irregularly-spaced timestamps.
 *  TWAvg = sum(dt_i * v_i) / total_dt.
 *  Critical for PI summary calculations where events are not evenly spaced. */
double pi_time_weighted_average(const double *times, const double *values, int n) {
    if (!times || !values || n < 1) return 0.0;
    if (n == 1) return values[0];
    double weighted_sum = 0.0, total_dt = 0.0;
    int i;
    for (i = 1; i < n; i++) {
        double dt = times[i] - times[i-1];
        if (dt < 0.0) dt = -dt;
        double avg_val = (values[i] + values[i-1]) / 2.0;
        weighted_sum += dt * avg_val;
        total_dt += dt;
    }
    return total_dt > 0.0 ? weighted_sum / total_dt : values[0];
}

/* ─── Peak Detection ──────────────────────────────────────────── */
int pi_find_peaks(const double *data, int n, double threshold,
                   int *peak_indices, int max_peaks) {
    if (!data || !peak_indices || n < 3 || max_peaks <= 0) return 0;
    int i, count = 0;
    for (i = 1; i < n - 1 && count < max_peaks; i++)
        if (data[i] > data[i-1] && data[i] > data[i+1] && data[i] > threshold)
            peak_indices[count++] = i;
    return count;
}

/* ─── Linear Interpolation ────────────────────────────────────── */
double pi_linear_interpolate(double x0, double y0, double x1, double y1, double x) {
    if (fabs(x1 - x0) < 1e-12) return y0;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

/* ─── Cumulative Sum ──────────────────────────────────────────── */
void pi_cumulative_sum(const double *input, double *output, int n) {
    if (!input || !output || n <= 0) return;
    output[0] = input[0];
    int i;
    for (i = 1; i < n; i++) output[i] = output[i-1] + input[i];
}

/* ─── Signal Processing ───────────────────────────────────────────── */
/** First-order low-pass filter: y[i] = alpha*x[i] + (1-alpha)*y[i-1]. */
void pi_lowpass_filter(const double *input, double *output, int n, double alpha) {
    if (!input || !output || n <= 0) return;
    output[0] = input[0];
    int i;
    for (i = 1; i < n; i++) output[i] = alpha*input[i] + (1.0-alpha)*output[i-1];
}

/** Signal energy (sum of squares). */
double pi_signal_energy(const double *data, int n) {
    if (!data || n <= 0) return 0.0;
    double e = 0.0;
    int i;
    for (i = 0; i < n; i++) e += data[i] * data[i];
    return e;
}

/** Cross-correlation at lag k. */
double pi_cross_correlation(const double *x, const double *y, int n, int lag) {
    if (!x || !y || n <= lag) return 0.0;
    double sum = 0.0;
    int i;
    for (i = 0; i < n - lag; i++) sum += x[i] * y[i + lag];
    return sum / (double)(n - lag);
}

/* ─── Median ──────────────────────────────────────────────────── */
double pi_statistics_median(double *data, int n) {
    if (!data || n <= 0) return 0.0;
    if (n == 1) return data[0];
    double *tmp = (double*)malloc((size_t)n * sizeof(double));
    if (!tmp) return 0.0;
    memcpy(tmp, data, (size_t)n * sizeof(double));
    int i, j;
    for (i = 1; i < n; i++) { double key = tmp[i]; for (j=i-1; j>=0&&tmp[j]>key; j--) tmp[j+1]=tmp[j]; tmp[j+1]=key; }
    double med = (n % 2) ? tmp[n/2] : (tmp[n/2-1] + tmp[n/2]) / 2.0;
    free(tmp);
    return med;
}

/* ─── Percentile ──────────────────────────────────────────────── */
double pi_statistics_percentile(const double *data, int n, double pct) {
    if (!data || n <= 0 || pct < 0.0 || pct > 100.0) return 0.0;
    double *tmp = (double*)malloc((size_t)n * sizeof(double));
    if (!tmp) return 0.0;
    memcpy(tmp, data, (size_t)n * sizeof(double));
    int i, j;
    for (i = 1; i < n; i++) { double key = tmp[i]; for (j=i-1; j>=0&&tmp[j]>key; j--) tmp[j+1]=tmp[j]; tmp[j+1]=key; }
    double idx = pct / 100.0 * (double)(n - 1);
    int lo = (int)idx, hi = lo + 1;
    if (hi >= n) hi = n - 1;
    double frac = idx - (double)lo;
    double result = tmp[lo] + frac * (tmp[hi] - tmp[lo]);
    free(tmp);
    return result;
}

/* ─── Range ──────────────────────────────────────────────────── */
double pi_statistics_range(const double *data, int n) {
    if (!data || n <= 0) return 0.0;
    double mn = data[0], mx = data[0]; int i;
    for (i = 1; i < n; i++) { if(data[i]<mn) mn=data[i]; if(data[i]>mx) mx=data[i]; }
    return mx - mn;
}

/* ─── Sum ────────────────────────────────────────────────────── */
double pi_statistics_sum(const double *data, int n) {
    if (!data || n <= 0) return 0.0;
    double s = 0.0; int i;
    for (i = 0; i < n; i++) s += data[i];
    return s;
}
