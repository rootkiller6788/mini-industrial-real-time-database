/**
 * @file    historian_interpolation.c
 * @brief   Time-series interpolation method implementations.
 *
 * Knowledge coverage:
 *   L1: 8 interpolation method implementations
 *   L2: Step (last known value), linear, quadratic, cubic spline
 *   L5: Akima spline, Catmull-Rom, Lagrange polynomial interpolation
 *   L3: Tridiagonal solver for cubic spline
 */

#include "historian_interpolation.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =========================================================================
 * Helper: binary search for interval containing query_time
 *
 * Returns the index i such that points[i].time <= query_time < points[i+1].time
 * Returns -1 if query_time is before all points.
 * Returns count-2 if query_time is after all points.
 * ========================================================================= */

static int find_interval(const historian_data_point_t *points, size_t count,
                          int64_t query_time_ms)
{
    if (count < 2) return -1;

    int64_t t0 = points[0].timestamp.epoch_ms;
    int64_t tN = points[count-1].timestamp.epoch_ms;

    if (query_time_ms < t0) return -2;
    if (query_time_ms >= tN) return (int)(count - 2);

    /* Binary search */
    size_t lo = 0, hi = count - 1;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (points[mid].timestamp.epoch_ms <= query_time_ms) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return (int)(lo - 1);
}

/* =========================================================================
 * Step Interpolation (Last Known Good Value)
 * ========================================================================= */

int historian_interp_step(const historian_data_point_t *points,
                           size_t count, int64_t query_time_ms,
                           double *value_out)
{
    if (!points || !value_out) return -1;
    if (count == 0) { *value_out = NAN; return -2; }

    /* Find the largest timestamp <= query_time_ms */
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (points[mid].timestamp.epoch_ms <= query_time_ms) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo == 0) {
        *value_out = NAN; /* No point before query_time */
        return -2;
    }

    *value_out = points[lo - 1].value;
    return 0;
}

/* =========================================================================
 * Linear Interpolation
 *
 * Formula (2-point): v(t) = v_i + (v_{i+1} - v_i) * (t - t_i) / (t_{i+1} - t_i)
 * ========================================================================= */

int historian_interp_linear(const historian_data_point_t *points,
                             size_t count, int64_t query_time_ms,
                             double *value_out)
{
    if (!points || !value_out) return -1;
    if (count < 2) return historian_interp_step(points, count, query_time_ms, value_out);

    int idx = find_interval(points, count, query_time_ms);
    if (idx < 0) {
        /* Out of range */
        if (idx == -2) { /* before first point */
            /* Extrapolate using first two points */
            double t0 = (double)points[0].timestamp.epoch_ms;
            double t1 = (double)points[1].timestamp.epoch_ms;
            double dt = t1 - t0;
            if (fabs(dt) < 1e-12) { *value_out = points[0].value; return 0; }
            double frac = (double)(query_time_ms - points[0].timestamp.epoch_ms) / dt;
            *value_out = points[0].value + frac * (points[1].value - points[0].value);
            return 0;
        }
        *value_out = points[count-1].value;
        return 0;
    }

    double t_i = (double)points[idx].timestamp.epoch_ms;
    double t_ip1 = (double)points[idx + 1].timestamp.epoch_ms;
    double dt = t_ip1 - t_i;
    if (fabs(dt) < 1e-12) {
        *value_out = points[idx].value;
        return 0;
    }

    double frac = (double)(query_time_ms - points[idx].timestamp.epoch_ms) / dt;
    *value_out = points[idx].value + frac * (points[idx+1].value - points[idx].value);
    return 0;
}

/* =========================================================================
 * Quadratic Interpolation through 3 nearest points
 *
 * Given 3 points (x0,y0), (x1,y1), (x2,y2), the divided difference
 * form of Newton's interpolation polynomial is:
 *
 *   P(x) = y0 + f[x0,x1](x-x0) + f[x0,x1,x2](x-x0)(x-x1)
 *
 * where f[x0,x1] = (y1-y0)/(x1-x0) and
 *       f[x0,x1,x2] = (f[x1,x2] - f[x0,x1])/(x2-x0)
 * ========================================================================= */

int historian_interp_quadratic(const historian_data_point_t *points,
                                size_t count, int64_t query_time_ms,
                                double *value_out)
{
    if (!points || !value_out) return -1;
    if (count < 3) return historian_interp_linear(points, count, query_time_ms, value_out);

    int idx = find_interval(points, count, query_time_ms);
    if (idx < 0) {
        return historian_interp_linear(points, count, query_time_ms, value_out);
    }

    /* Choose 3 consecutive points centered on the query interval */
    int i0, i1, i2;
    if (idx == 0) {
        i0 = 0; i1 = 1; i2 = 2;
    } else if (idx >= (int)(count - 2)) {
        i0 = (int)(count - 3); i1 = (int)(count - 2); i2 = (int)(count - 1);
    } else {
        i0 = idx - 1; i1 = idx; i2 = idx + 1;
    }

    double x = (double)query_time_ms;
    double x0 = (double)points[i0].timestamp.epoch_ms;
    double x1 = (double)points[i1].timestamp.epoch_ms;
    double x2 = (double)points[i2].timestamp.epoch_ms;
    double y0 = points[i0].value;
    double y1 = points[i1].value;
    double y2 = points[i2].value;

    /* First divided differences */
    double f01 = (y1 - y0) / (x1 - x0);
    double f12 = (y2 - y1) / (x2 - x1);

    /* Second divided difference */
    double f012 = (f12 - f01) / (x2 - x0);

    /* Evaluate Newton form */
    *value_out = y0 + f01 * (x - x0) + f012 * (x - x0) * (x - x1);
    return 0;
}

/* =========================================================================
 * Natural Cubic Spline Interpolation
 *
 * Given n points (x_i, y_i), find n-1 cubic polynomials S_i(x) on
 * [x_i, x_{i+1}] such that:
 *   1. S_i(x_i) = y_i, S_i(x_{i+1}) = y_{i+1}  (interpolation)
 *   2. S'_i(x_{i+1}) = S'_{i+1}(x_{i+1})        (C^1 continuity)
 *   3. S''_i(x_{i+1}) = S''_{i+1}(x_{i+1})      (C^2 continuity)
 *   4. S''_0(x_0) = 0, S''_{n-2}(x_{n-1}) = 0   (natural boundary)
 *
 * Solving the tridiagonal system for the second derivatives M_i:
 *   h_{i-1} * M_{i-1} + 2*(h_{i-1}+h_i) * M_i + h_i * M_{i+1}
 *     = 6 * [(y_{i+1}-y_i)/h_i - (y_i-y_{i-1})/h_{i-1}]
 *
 * Then the polynomial on [x_i, x_{i+1}] is:
 *   S(x) = M_i*(x_{i+1}-x)^3/(6*h_i) + M_{i+1}*(x-x_i)^3/(6*h_i)
 *          + (y_i - M_i*h_i^2/6)*(x_{i+1}-x)/h_i
 *          + (y_{i+1} - M_{i+1}*h_i^2/6)*(x-x_i)/h_i
 *
 * Reference: de Boor, C. (1978). "A Practical Guide to Splines." Springer.
 * ========================================================================= */

int historian_interp_cubic_spline(const historian_data_point_t *points,
                                   size_t count, int64_t query_time_ms,
                                   double *value_out)
{
    if (!points || !value_out) return -1;
    if (count < 3) return historian_interp_linear(points, count, query_time_ms, value_out);

    int idx = find_interval(points, count, query_time_ms);
    if (idx < 0) {
        return historian_interp_linear(points, count, query_time_ms, value_out);
    }

    size_t n = count;

    /* Allocate arrays */
    double *h = (double *)malloc((n - 1) * sizeof(double));
    double *alpha = (double *)malloc(n * sizeof(double));
    double *M = (double *)malloc(n * sizeof(double));
    double *l = (double *)malloc(n * sizeof(double));
    double *mu = (double *)malloc(n * sizeof(double));
    double *z = (double *)malloc(n * sizeof(double));

    if (!h || !alpha || !M || !l || !mu || !z) {
        free(h); free(alpha); free(M); free(l); free(mu); free(z);
        return -3;
    }

    /* Step 1: compute h_i = x_{i+1} - x_i */
    for (size_t i = 0; i < n - 1; i++) {
        double dx = (double)(points[i+1].timestamp.epoch_ms -
                              points[i].timestamp.epoch_ms);
        if (fabs(dx) < 1e-12) dx = 1e-12; /* Avoid zero */
        h[i] = dx;
    }

    /* Step 2: compute right-hand side alpha */
    for (size_t i = 1; i < n - 1; i++) {
        alpha[i] = (3.0 / h[i]) * (points[i+1].value - points[i].value)
                 - (3.0 / h[i-1]) * (points[i].value - points[i-1].value);
    }

    /* Step 3: solve tridiagonal system */
    /* Thomas algorithm for M (second derivatives) */
    l[0] = 1.0;
    mu[0] = 0.0;
    z[0] = 0.0;

    for (size_t i = 1; i < n - 1; i++) {
        l[i] = 2.0 * (points[i+1].timestamp.epoch_ms -
                       points[i-1].timestamp.epoch_ms)
               - h[i-1] * mu[i-1];
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i-1] * z[i-1]) / l[i];
    }

    l[n-1] = 1.0;
    z[n-1] = 0.0;
    M[n-1] = 0.0;

    for (size_t j = n - 1; j > 0; j--) {
        size_t i = j - 1;
        M[i] = z[i] - mu[i] * M[i+1];
    }
    M[0] = 0.0;

    /* Step 4: evaluate at query_time */
    int i = idx;
    double x = (double)query_time_ms;
    double xi = (double)points[i].timestamp.epoch_ms;
    double xi1 = (double)points[i+1].timestamp.epoch_ms;
    double hi = h[i];
    double A = (xi1 - x) / hi;
    double B = (x - xi) / hi;

    *value_out = M[i] * A * A * A * hi * hi / 6.0
               + M[i+1] * B * B * B * hi * hi / 6.0
               + (points[i].value - M[i] * hi * hi / 6.0) * A
               + (points[i+1].value - M[i+1] * hi * hi / 6.0) * B;

    free(h); free(alpha); free(M); free(l); free(mu); free(z);
    return 0;
}

/* =========================================================================
 * Akima Spline Interpolation
 *
 * Akima (1970) proposed a local interpolation method that produces a
 * more "natural" looking curve than natural cubic splines, particularly
 * for data with abrupt changes in slope.
 *
 * The slope at point i is computed as a weighted average of the slopes
 * of adjacent segments:
 *
 *   m_i = (|m_{i+1} - m_i| * m_{i-1} + |m_{i-1} - m_{i-2}| * m_i)
 *       / (|m_{i+1} - m_i| + |m_{i-1} - m_{i-2}|)
 *
 * where m_i = (y_{i+1} - y_i) / (x_{i+1} - x_i)
 *
 * If denominator is zero: m_i = (m_{i-1} + m_i) / 2
 * Boundary slopes: use 2-point formulas.
 *
 * Reference: Akima, H. (1970). "A New Method of Interpolation and Smooth
 * Curve Fitting Based on Local Procedures." J.ACM 17(4):589-602.
 * ========================================================================= */

int historian_interp_akima(const historian_data_point_t *points,
                            size_t count, int64_t query_time_ms,
                            double *value_out)
{
    if (!points || !value_out) return -1;
    if (count < 3) return historian_interp_linear(points, count, query_time_ms, value_out);

    int idx = find_interval(points, count, query_time_ms);
    if (idx < 0) {
        return historian_interp_linear(points, count, query_time_ms, value_out);
    }

    size_t n = count;

    /* Compute segment slopes m_i = (y_{i+1} - y_i) / (x_{i+1} - x_i) */
    double *m = (double *)malloc((n - 1) * sizeof(double));
    double *slopes = (double *)malloc(n * sizeof(double));
    if (!m || !slopes) { free(m); free(slopes); return -3; }

    for (size_t i = 0; i < n - 1; i++) {
        double dx = (double)(points[i+1].timestamp.epoch_ms -
                              points[i].timestamp.epoch_ms);
        if (fabs(dx) < 1e-12) dx = 1e-12;
        m[i] = (points[i+1].value - points[i].value) / dx;
    }

    /* Compute knot slopes using Akima's weighting */
    /* Endpoints: quadratic extrapolation */
    slopes[0] = m[0] + (m[0] - m[1]) * 0.5;
    slopes[1] = (m[0] + m[1]) * 0.5;
    slopes[n-2] = (m[n-3] + m[n-2]) * 0.5;
    slopes[n-1] = m[n-2] + (m[n-2] - m[n-3]) * 0.5;

    /* Interior points using Akima's formula */
    for (size_t i = 2; i < n - 2; i++) {
        double w1 = fabs(m[i] - m[i-1]);
        double w2 = fabs(m[i-2] - m[i-3]);
        if (w1 + w2 < 1e-12) {
            slopes[i] = (m[i-1] + m[i]) * 0.5;
        } else {
            slopes[i] = (w1 * m[i-2] + w2 * m[i-1]) / (w1 + w2);
        }
    }

    /* Evaluate Hermite cubic on the interval [idx, idx+1] */
    int i = idx;
    double x = (double)query_time_ms;
    double x0 = (double)points[i].timestamp.epoch_ms;
    double x1 = (double)points[i+1].timestamp.epoch_ms;
    double dx = x1 - x0;
    if (fabs(dx) < 1e-12) dx = 1e-12;

    double t = (x - x0) / dx;
    double t2 = t * t;
    double t3 = t2 * t;

    double y0 = points[i].value;
    double y1 = points[i+1].value;
    double m0 = slopes[i] * dx;
    double m1 = slopes[i+1] * dx;

    /* Hermite basis functions */
    double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    double h10 = t3 - 2.0 * t2 + t;
    double h01 = -2.0 * t3 + 3.0 * t2;
    double h11 = t3 - t2;

    *value_out = h00 * y0 + h10 * m0 + h01 * y1 + h11 * m1;

    free(m); free(slopes);
    return 0;
}

/* =========================================================================
 * Catmull-Rom Spline (Cubic Hermite with centripetal tangents)
 * ========================================================================= */

int historian_interp_cubic_hermite(const historian_data_point_t *points,
                                    size_t count, int64_t query_time_ms,
                                    double *value_out)
{
    if (!points || !value_out) return -1;
    if (count < 2) return -1;

    int idx = find_interval(points, count, query_time_ms);
    if (idx < 0) {
        return historian_interp_linear(points, count, query_time_ms, value_out);
    }

    int i = idx;
    double x = (double)query_time_ms;
    double x0 = (double)points[i].timestamp.epoch_ms;
    double x1 = (double)points[i+1].timestamp.epoch_ms;

    /* Tangents: Catmull-Rom = central difference */
    double m0, m1;

    if (i == 0) {
        m0 = (points[1].value - points[0].value)
             / ((double)(points[1].timestamp.epoch_ms - points[0].timestamp.epoch_ms));
        m0 *= (x1 - x0);
    } else {
        m0 = (points[i+1].value - points[i-1].value) * 0.5;
        m0 *= (x1 - x0) / (double)(points[i+1].timestamp.epoch_ms
                                    - points[i-1].timestamp.epoch_ms);
    }

    if (i >= (int)(count - 2)) {
        m1 = (points[count-1].value - points[count-2].value)
             / ((double)(points[count-1].timestamp.epoch_ms
                         - points[count-2].timestamp.epoch_ms));
        m1 *= (x1 - x0);
    } else {
        m1 = (points[i+2].value - points[i].value) * 0.5;
        m1 *= (x1 - x0) / (double)(points[i+2].timestamp.epoch_ms
                                    - points[i].timestamp.epoch_ms);
    }

    double dx = x1 - x0;
    if (fabs(dx) < 1e-12) { *value_out = points[i].value; return 0; }

    double t = (x - x0) / dx;
    double t2 = t * t;
    double t3 = t2 * t;

    double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    double h10 = t3 - 2.0 * t2 + t;
    double h01 = -2.0 * t3 + 3.0 * t2;
    double h11 = t3 - t2;

    *value_out = h00 * points[i].value + h10 * m0
               + h01 * points[i+1].value + h11 * m1;
    return 0;
}

/* =========================================================================
 * 3rd-order Lagrange Polynomial Interpolation
 * ========================================================================= */

int historian_interp_lagrange_3(const historian_data_point_t *points,
                                 size_t count, int64_t query_time_ms,
                                 double *value_out)
{
    if (!points || !value_out) return -1;
    if (count < 4) return historian_interp_quadratic(points, count,
                                                       query_time_ms, value_out);

    int idx = find_interval(points, count, query_time_ms);
    if (idx < 0) {
        return historian_interp_linear(points, count, query_time_ms, value_out);
    }

    /* Choose 4 points centered on the interval */
    int i0, i1, i2, i3;
    if (idx == 0) {
        i0 = 0; i1 = 1; i2 = 2; i3 = 3;
    } else if (idx == 1) {
        i0 = 0; i1 = 1; i2 = 2; i3 = 3;
    } else if (idx >= (int)(count - 3)) {
        i0 = (int)(count - 4); i1 = (int)(count - 3);
        i2 = (int)(count - 2); i3 = (int)(count - 1);
    } else {
        i0 = idx - 1; i1 = idx; i2 = idx + 1; i3 = idx + 2;
    }

    double x = (double)query_time_ms;
    double x0 = (double)points[i0].timestamp.epoch_ms;
    double x1 = (double)points[i1].timestamp.epoch_ms;
    double x2 = (double)points[i2].timestamp.epoch_ms;
    double x3 = (double)points[i3].timestamp.epoch_ms;
    double y0 = points[i0].value;
    double y1 = points[i1].value;
    double y2 = points[i2].value;
    double y3 = points[i3].value;

    /* Lagrange basis L_i(x) */
    double L0 = ((x - x1) * (x - x2) * (x - x3))
              / ((x0 - x1) * (x0 - x2) * (x0 - x3));
    double L1 = ((x - x0) * (x - x2) * (x - x3))
              / ((x1 - x0) * (x1 - x2) * (x1 - x3));
    double L2 = ((x - x0) * (x - x1) * (x - x3))
              / ((x2 - x0) * (x2 - x1) * (x2 - x3));
    double L3 = ((x - x0) * (x - x1) * (x - x2))
              / ((x3 - x0) * (x3 - x1) * (x3 - x2));

    *value_out = L0 * y0 + L1 * y1 + L2 * y2 + L3 * y3;
    return 0;
}

/* =========================================================================
 * Range Interpolation
 * ========================================================================= */

int historian_interpolate_range(const historian_data_point_t *points,
                                 size_t count,
                                 int64_t start_time_ms, int64_t end_time_ms,
                                 int64_t interval_ms,
                                 historian_interp_method_t method,
                                 historian_data_point_t *output,
                                 size_t output_cap, size_t *output_count)
{
    if (!points || !output || !output_count) return -1;
    *output_count = 0;

    if (count == 0 || interval_ms <= 0) return -2;
    if (start_time_ms >= end_time_ms) return -3;

    for (int64_t t = start_time_ms; t <= end_time_ms && *output_count < output_cap;
         t += interval_ms) {

        double val;
        historian_interpolate_at(points, count, t, method, &val);

        historian_timestamp_t ts;
        ts.epoch_ms = t;
        ts.tz_offset_min = 0;
        ts.is_dst = 0;
        ts.is_utc = 1;

        output[*output_count] = historian_make_point(0, ts, val,
                                                       HISTORIAN_QUAL_GOOD |
                                                       HISTORIAN_QUAL_SUB_INTERPOLATED);
        (*output_count)++;
    }

    return 0;
}

/* =========================================================================
 * Generic Interpolation Dispatcher
 * ========================================================================= */

int historian_interpolate_at(const historian_data_point_t *points,
                              size_t count, int64_t query_time_ms,
                              historian_interp_method_t method,
                              double *value_out)
{
    if (!points || !value_out) return -1;
    if (count == 0) { *value_out = NAN; return -2; }

    switch (method) {
    case HISTORIAN_INTERP_NONE:
        /* Return NAN if no exact match (none interpolation) */
        *value_out = NAN;
        return 0;

    case HISTORIAN_INTERP_STEP:
        return historian_interp_step(points, count, query_time_ms, value_out);

    case HISTORIAN_INTERP_LINEAR:
        return historian_interp_linear(points, count, query_time_ms, value_out);

    case HISTORIAN_INTERP_QUADRATIC:
        return historian_interp_quadratic(points, count, query_time_ms, value_out);

    case HISTORIAN_INTERP_CUBIC_SPLINE:
        return historian_interp_cubic_spline(points, count, query_time_ms, value_out);

    case HISTORIAN_INTERP_CUBIC_HERMITE:
        return historian_interp_cubic_hermite(points, count, query_time_ms, value_out);

    case HISTORIAN_INTERP_AKIMA:
        return historian_interp_akima(points, count, query_time_ms, value_out);

    case HISTORIAN_INTERP_LAGRANGE_N3:
        return historian_interp_lagrange_3(points, count, query_time_ms, value_out);

    default:
        *value_out = NAN;
        return -99;
    }
}

/* =========================================================================
 * Quality-Aware Interpolation
 *
 * Skips bad-quality points when selecting interpolation neighbors.
 * Falls back to step interpolation using the last known good value
 * if insufficient good points are available for the requested method.
 * ========================================================================= */

int historian_interp_quality_aware(const historian_data_point_t *points,
                                    size_t count, int64_t query_time_ms,
                                    historian_interp_method_t method,
                                    double *value_out)
{
    if (!points || !value_out) return -1;

    /* Filter to good-quality points only */
    historian_data_point_t *good_points = (historian_data_point_t *)malloc(
        count * sizeof(historian_data_point_t));
    if (!good_points) return -3;

    size_t good_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (historian_quality_is_good(points[i].quality)) {
            good_points[good_count++] = points[i];
        }
    }

    int result;
    if (good_count == 0) {
        *value_out = NAN;
        result = -2;
    } else {
        /* Try the requested method; if it fails, fall back to step */
        result = historian_interpolate_at(good_points, good_count,
                                           query_time_ms, method, value_out);
        if (result < 0) {
            result = historian_interp_step(good_points, good_count,
                                            query_time_ms, value_out);
        }
    }

    free(good_points);
    return result;
}
