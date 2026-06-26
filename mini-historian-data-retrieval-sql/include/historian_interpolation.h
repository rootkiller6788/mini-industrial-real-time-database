#ifndef HISTORIAN_INTERPOLATION_H
#define HISTORIAN_INTERPOLATION_H

/**
 * @file    historian_interpolation.h
 * @brief   Time-series interpolation methods for historian data retrieval.
 *
 * When querying a historian at a timestamp where no value was stored
 * (compressed or query timestamp falls between stored points), the
 * historian must interpolate. Different interpolation methods have
 * different trade-offs between accuracy, smoothness, and computation.
 *
 * Knowledge Coverage:
 *   L1: Interpolation type definitions
 *   L2: Last-known-value vs. interpolated retrieval
 *   L5: Linear, step, quadratic, cubic spline, Akima spline
 *   L7: PI Data Archive interpolation semantics
 *
 * References:
 *   - de Boor, C. "A Practical Guide to Splines" (1978)
 *   - Akima, H. "A New Method of Interpolation..." (1970) J.ACM
 *   - OSIsoft PI Server Reference, Chapter 7: Retrieval
 */

#include "historian_model.h"
#include <stdint.h>
#include <stddef.h>

/**
 * Interpolation method for filling gaps between stored data points.
 *
 * OPC HDA defines: NoInterpolation=0, Linear=1, Staircase=2
 * OSIsoft PI extends this with additional methods.
 */
typedef enum {
    HISTORIAN_INTERP_NONE          = 0,  /**< Return only stored points */
    HISTORIAN_INTERP_STEP          = 1,  /**< Last known value (OSIsoft: "Previous") */
    HISTORIAN_INTERP_LINEAR        = 2,  /**< Linear interpolation between neighbors */
    HISTORIAN_INTERP_QUADRATIC     = 3,  /**< Quadratic through 3 nearest points */
    HISTORIAN_INTERP_CUBIC_SPLINE  = 4,  /**< Natural cubic spline */
    HISTORIAN_INTERP_CUBIC_HERMITE = 5,  /**< Catmull-Rom / cardinal spline */
    HISTORIAN_INTERP_AKIMA         = 6,  /**< Akima spline (better for noisy data) */
    HISTORIAN_INTERP_LAGRANGE_N3   = 7   /**< 3rd-order Lagrange polynomial */
} historian_interp_method_t;

/**
 * Interpolate a single value at the query timestamp.
 *
 * Given a sorted array of stored data points, compute the interpolated
 * value at query_time_ms.
 *
 * @param points         Sorted array of data points (ascending timestamp).
 * @param count          Number of points (must be >= 2 for most methods).
 * @param query_time_ms  Timestamp at which to interpolate.
 * @param method         Interpolation method.
 * @param value_out      Output: interpolated value.
 * @return 0 on success, negative on error (e.g., insufficient points).
 *
 * Complexity: O(log n) for step/linear (binary search),
 *             O(n) for spline methods (solve tridiagonal system).
 */
int historian_interpolate_at(const historian_data_point_t *points,
                              size_t count, int64_t query_time_ms,
                              historian_interp_method_t method,
                              double *value_out);

/**
 * Interpolate at multiple regularly-spaced timestamps.
 *
 * Produces an evenly-spaced array of interpolated values from
 * start_time_ms to end_time_ms with the given interval.
 *
 * @param points         Sorted array of stored data points.
 * @param count          Number of stored points.
 * @param start_time_ms  First output timestamp.
 * @param end_time_ms    Last output timestamp (inclusive).
 * @param interval_ms    Interval between output points.
 * @param method         Interpolation method.
 * @param output         Pre-allocated output array.
 * @param output_cap     Capacity of output array.
 * @param output_count   Output: number of points written.
 * @return 0 on success.
 *
 * This is the fundamental operation behind OPC HDA ReadProcessed
 * with resampleInterval specified.
 */
int historian_interpolate_range(const historian_data_point_t *points,
                                 size_t count,
                                 int64_t start_time_ms, int64_t end_time_ms,
                                 int64_t interval_ms,
                                 historian_interp_method_t method,
                                 historian_data_point_t *output,
                                 size_t output_cap, size_t *output_count);

/**
 * Step interpolation: return the value of the data point with the largest
 * timestamp <= query_time_ms (i.e., "last known good value").
 *
 * @param points        Sorted array.
 * @param count         Number of points.
 * @param query_time_ms Query timestamp.
 * @param value_out     Output value.
 * @return 0 on success, -1 if no data point exists before query_time.
 *
 * Knowledge: This is the most common interpolation in industrial HMIs,
 * where the display must always show "the last known value" of a sensor.
 */
int historian_interp_step(const historian_data_point_t *points,
                           size_t count, int64_t query_time_ms,
                           double *value_out);

/**
 * Linear interpolation between two neighboring points.
 *
 * Formula: v(t) = v_i + (v_{i+1} - v_i) * (t - t_i) / (t_{i+1} - t_i)
 *
 * @param points        Sorted array.
 * @param count         Number of points.
 * @param query_time_ms Query timestamp.
 * @param value_out     Output value.
 * @return 0 on success, -1 if outside data range.
 */
int historian_interp_linear(const historian_data_point_t *points,
                             size_t count, int64_t query_time_ms,
                             double *value_out);

/**
 * Quadratic interpolation through three nearest points.
 *
 * Uses the three points nearest to query_time_ms, fits a parabola
 * y = ax^2 + bx + c using divided differences, and evaluates at
 * query_time_ms.
 *
 * @param points        Sorted array (need at least 3 points).
 * @param count         Number of points.
 * @param query_time_ms Query timestamp.
 * @param value_out     Output value.
 * @return 0 on success.
 */
int historian_interp_quadratic(const historian_data_point_t *points,
                                size_t count, int64_t query_time_ms,
                                double *value_out);

/**
 * Natural cubic spline interpolation.
 *
 * Solves the tridiagonal system for the second derivatives at each knot,
 * then evaluates the piecewise cubic polynomial at query_time_ms.
 *
 * Boundary conditions: second derivative = 0 at both ends (natural spline).
 *
 * @param points        Sorted array.
 * @param count         Number of points (>= 2).
 * @param query_time_ms Query timestamp.
 * @param value_out     Output value.
 * @return 0 on success.
 *
 * Complexity: O(n) per evaluation after O(n) setup.
 * Reference: de Boor (1978), Chapter IV.
 */
int historian_interp_cubic_spline(const historian_data_point_t *points,
                                   size_t count, int64_t query_time_ms,
                                   double *value_out);

/**
 * Akima spline interpolation.
 *
 * Akima's method (1970) produces a C^1 continuous piecewise cubic
 * interpolant that reduces wiggles (overshoot) compared to natural
 * cubic splines, making it better suited for industrial data with
 * step changes and noise.
 *
 * The derivative at each knot is a weighted average of the slopes
 * of adjacent segments: weight_i = |m_{i+1} - m_i|
 *
 * @param points        Sorted array (>= 3 points).
 * @param count         Number of points.
 * @param query_time_ms Query timestamp.
 * @param value_out     Output value.
 * @return 0 on success.
 *
 * Reference: Akima (1970), J.ACM 17(4):589-602.
 */
int historian_interp_akima(const historian_data_point_t *points,
                            size_t count, int64_t query_time_ms,
                            double *value_out);

/**
 * Cubic Hermite (Catmull-Rom) interpolation.
 *
 * Passes through all control points with specified tangent vectors.
 * Catmull-Rom: tangents = (p_{i+1} - p_{i-1}) / 2
 *
 * @param points        Sorted array (>= 2 points).
 * @param count         Number of points.
 * @param query_time_ms Query timestamp.
 * @param value_out     Output value.
 * @return 0 on success.
 */
int historian_interp_cubic_hermite(const historian_data_point_t *points,
                                    size_t count, int64_t query_time_ms,
                                    double *value_out);

/**
 * 3rd-order Lagrange polynomial interpolation.
 *
 * Uses the four nearest points to construct a 3rd-degree polynomial
 * through the Lagrange basis.
 *
 * Formula: L(x) = SUM_{i=0}^{3} y_i * PROD_{j!=i} (x - x_j)/(x_i - x_j)
 */
int historian_interp_lagrange_3(const historian_data_point_t *points,
                                 size_t count, int64_t query_time_ms,
                                 double *value_out);

/**
 * Quality-aware interpolation.
 *
 * Like historian_interpolate_at, but skips data points with bad quality
 * flags. If not enough good points exist for the chosen method, falls
 * back to step interpolation using the last known good value.
 */
int historian_interp_quality_aware(const historian_data_point_t *points,
                                    size_t count, int64_t query_time_ms,
                                    historian_interp_method_t method,
                                    double *value_out);

#endif /* HISTORIAN_INTERPOLATION_H */
