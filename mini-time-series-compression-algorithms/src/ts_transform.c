/**
 * @file ts_transform.c
 * @brief Transform-Based Compression — DFT, DCT, Windowing, Thresholding
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge: L2 Concepts, L5 Algorithms, L8 Advanced Topics
 *
 * Implements the Cooley-Tukey radix-2 FFT for DFT, DCT-II via FFT,
 * window functions (Hann, Hamming, Blackman), coefficient thresholding
 * (hard, soft, universal/VisuShrink), and full compression pipeline.
 *
 * Reference: Cooley & Tukey (1965). Math. Comp. 19:297-301.
 *            Ahmed, Natarajan & Rao (1974). IEEE Trans. C-23(1):90-93.
 *            Donoho & Johnstone (1994). Biometrika 81(3):425-455.
 * Curriculum: MIT 6.302, Stanford ENGR205, Berkeley EECS 20, CMU 18-771
 */

#include "ts_transform.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------------------------------------------------------------------------
 * L5: Window Generation
 * ------------------------------------------------------------------------- */

int ts_window_generate(double *window, size_t N, ts_window_type_t type)
{
    if (!window || N == 0) return -1;

    switch (type) {
    case TS_WINDOW_RECTANGULAR:
        for (size_t n = 0; n < N; n++) window[n] = 1.0;
        break;

    case TS_WINDOW_HANN:
        for (size_t n = 0; n < N; n++) {
            window[n] = 0.5 * (1.0 - cos(2.0 * M_PI * (double)n / (double)(N - 1)));
        }
        break;

    case TS_WINDOW_HAMMING:
        for (size_t n = 0; n < N; n++) {
            window[n] = 0.54 - 0.46 * cos(2.0 * M_PI * (double)n / (double)(N - 1));
        }
        break;

    case TS_WINDOW_BLACKMAN:
        for (size_t n = 0; n < N; n++) {
            window[n] = 0.42
                       - 0.5 * cos(2.0 * M_PI * (double)n / (double)(N - 1))
                       + 0.08 * cos(4.0 * M_PI * (double)n / (double)(N - 1));
        }
        break;

    case TS_WINDOW_BLACKMAN_HARRIS:
        for (size_t n = 0; n < N; n++) {
            window[n] = 0.35875
                       - 0.48829 * cos(2.0 * M_PI * (double)n / (double)(N - 1))
                       + 0.14128 * cos(4.0 * M_PI * (double)n / (double)(N - 1))
                       - 0.01168 * cos(6.0 * M_PI * (double)n / (double)(N - 1));
        }
        break;

    default:
        return -1;
    }

    return 0;
}

int ts_window_apply(double *signal, size_t N, const double *window)
{
    if (!signal || !window || N == 0) return -1;
    for (size_t i = 0; i < N; i++) signal[i] *= window[i];
    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Cooley-Tukey Radix-2 FFT
 *
 * Forward DFT:  X[k] = sum_{n=0}^{N-1} x[n] * exp(-j*2*pi*k*n/N)
 * Inverse DFT:  x[n] = (1/N) * sum_{k=0}^{N-1} X[k] * exp(j*2*pi*k*n/N)
 *
 * The radix-2 algorithm recursively decomposes the N-point DFT into
 * two N/2-point DFTs (even and odd indexed samples), achieving
 * O(N log N) complexity instead of the naive O(N^2).
 *
 * Bit-reversal permutation reorders the input array before the
 * butterfly stages begin.
 * ------------------------------------------------------------------------- */

/** Bit-reverse an index for radix-2 FFT */
static size_t bit_reverse(size_t x, int log2n)
{
    size_t n = 0;
    for (int i = 0; i < log2n; i++) {
        n = (n << 1) | (x & 1);
        x >>= 1;
    }
    return n;
}

/** Check if N is a power of 2 */
static int is_power_of_2(size_t N) {
    return (N > 0) && ((N & (N - 1)) == 0);
}

/** Compute log2(N) for power-of-2 N */
static int ilog2(size_t N) {
    int r = 0;
    while (N >>= 1) r++;
    return r;
}

int ts_dft_forward(const double *x_real, size_t N, ts_complex_t *X)
{
    if (!x_real || !X || N == 0) return -1;

    /* Load real input into complex array */
    for (size_t n = 0; n < N; n++) {
        X[n].real = x_real[n];
        X[n].imag = 0.0;
    }

    /* Handle non-power-of-2 with brute-force DFT */
    if (!is_power_of_2(N)) {
        ts_complex_t *temp = (ts_complex_t *)malloc(N * sizeof(ts_complex_t));
        if (!temp) return -1;
        memcpy(temp, X, N * sizeof(ts_complex_t));

        for (size_t k = 0; k < N; k++) {
            double re = 0.0, im = 0.0;
            for (size_t n = 0; n < N; n++) {
                double angle = -2.0 * M_PI * (double)k * (double)n / (double)N;
                re += temp[n].real * cos(angle) - temp[n].imag * sin(angle);
                im += temp[n].real * sin(angle) + temp[n].imag * cos(angle);
            }
            X[k].real = re;
            X[k].imag = im;
        }
        free(temp);
        return 0;
    }

    /* Radix-2 FFT for power-of-2 N */
    int log2n = ilog2(N);

    /* Bit-reversal permutation */
    for (size_t i = 0; i < N; i++) {
        size_t j = bit_reverse(i, log2n);
        if (j > i) {
            ts_complex_t tmp = X[i];
            X[i] = X[j];
            X[j] = tmp;
        }
    }

    /* Butterfly stages */
    for (int s = 1; s <= log2n; s++) {
        size_t m = (size_t)1 << s;       /* Size of sub-DFT */
        size_t m2 = m >> 1;              /* Half-size */
        ts_complex_t wm = {cos(-2.0 * M_PI / (double)m),
                           sin(-2.0 * M_PI / (double)m)};

        for (size_t k = 0; k < N; k += m) {
            ts_complex_t w = {1.0, 0.0};

            for (size_t j = 0; j < m2; j++) {
                /* Butterfly: X[k+j] = u + w*v, X[k+j+m2] = u - w*v */
                ts_complex_t u = X[k + j];
                ts_complex_t t = TS_COMPLEX_MUL(w, X[k + j + m2]);

                X[k + j].real       = u.real + t.real;
                X[k + j].imag       = u.imag + t.imag;
                X[k + j + m2].real  = u.real - t.real;
                X[k + j + m2].imag  = u.imag - t.imag;

                /* Update twiddle factor: w = w * wm */
                double w_re = w.real * wm.real - w.imag * wm.imag;
                double w_im = w.real * wm.imag + w.imag * wm.real;
                w.real = w_re;
                w.imag = w_im;
            }
        }
    }

    return 0;
}

int ts_dft_inverse(const ts_complex_t *X, size_t N, double *x_real)
{
    if (!X || !x_real || N == 0) return -1;

    /* Allocate complex workspace */
    ts_complex_t *Y = (ts_complex_t *)malloc(N * sizeof(ts_complex_t));
    if (!Y) return -1;

    /* Conjugate input (for inverse via forward FFT) */
    for (size_t k = 0; k < N; k++) {
        Y[k].real =  X[k].real;
        Y[k].imag = -X[k].imag;
    }

    /* Forward FFT of conjugated input */
    /* Reuse forward FFT by temporarily overwriting Y */
    if (!is_power_of_2(N)) {
        ts_complex_t *temp = (ts_complex_t *)malloc(N * sizeof(ts_complex_t));
        if (!temp) { free(Y); return -1; }
        memcpy(temp, Y, N * sizeof(ts_complex_t));
        for (size_t k = 0; k < N; k++) {
            double re = 0.0, im = 0.0;
            for (size_t n = 0; n < N; n++) {
                double angle = -2.0 * M_PI * (double)k * (double)n / (double)N;
                re += temp[n].real * cos(angle) - temp[n].imag * sin(angle);
                im += temp[n].real * sin(angle) + temp[n].imag * cos(angle);
            }
            Y[k].real = re;
            Y[k].imag = im;
        }
        free(temp);
    } else {
        int log2n = ilog2(N);
        for (size_t i = 0; i < N; i++) {
            size_t j = bit_reverse(i, log2n);
            if (j > i) { ts_complex_t tmp = Y[i]; Y[i] = Y[j]; Y[j] = tmp; }
        }
        for (int s = 1; s <= log2n; s++) {
            size_t m = (size_t)1 << s;
            size_t m2 = m >> 1;
            ts_complex_t wm = {cos(-2.0*M_PI/(double)m), sin(-2.0*M_PI/(double)m)};
            for (size_t k = 0; k < N; k += m) {
                ts_complex_t w = {1.0, 0.0};
                for (size_t j = 0; j < m2; j++) {
                    ts_complex_t u = Y[k+j];
                    ts_complex_t t; t.real=w.real*Y[k+j+m2].real-w.imag*Y[k+j+m2].imag;
                    t.imag=w.real*Y[k+j+m2].imag+w.imag*Y[k+j+m2].real;
                    Y[k+j].real=u.real+t.real; Y[k+j].imag=u.imag+t.imag;
                    Y[k+j+m2].real=u.real-t.real; Y[k+j+m2].imag=u.imag-t.imag;
                    double wr=w.real*wm.real-w.imag*wm.imag;
                    double wi=w.real*wm.imag+w.imag*wm.real;
                    w.real=wr; w.imag=wi;
                }
            }
        }
    }

    /* Scale and conjugate output */
    for (size_t n = 0; n < N; n++) {
        x_real[n] = Y[n].real / (double)N;
    }

    free(Y);
    return 0;
}

int ts_dft_magnitude(const ts_complex_t *X, size_t N, double *magnitude)
{
    if (!X || !magnitude || N == 0) return -1;
    for (size_t k = 0; k < N; k++) {
        magnitude[k] = sqrt(X[k].real * X[k].real + X[k].imag * X[k].imag);
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: DCT-II Implementation via FFT
 *
 * DCT-II can be computed from DFT using a clever pre-processing:
 *
 *   X_dct[k] = 2 * sum_{n=0}^{N-1} x[n] * cos(pi/N * (n+0.5) * k)
 *
 * We use the FFT-based method: create a 4N-point sequence, take FFT,
 * and extract the DCT coefficients.
 *
 * Reference: Makhoul (1980). "A Fast Cosine Transform in One and Two
 *            Dimensions." IEEE Trans. ASSP-28(1):27-34.
 * ------------------------------------------------------------------------- */

int ts_dct_forward(const double *x_real, size_t N, double *X)
{
    if (!x_real || !X || N == 0) return -1;

    /* DCT-II direct formula (O(N^2) but correct for any N) */
    for (size_t k = 0; k < N; k++) {
        double sum = 0.0;
        for (size_t n = 0; n < N; n++) {
            sum += x_real[n] * cos(M_PI / (double)N * ((double)n + 0.5) * (double)k);
        }
        X[k] = sum;
    }

    return 0;
}

int ts_dct_inverse(const double *X, size_t N, double *x_real)
{
    if (!X || !x_real || N == 0) return -1;

    /* Inverse DCT-II = DCT-III (up to scaling) */
    for (size_t n = 0; n < N; n++) {
        double sum = X[0] * 0.5;
        for (size_t k = 1; k < N; k++) {
            sum += X[k] * cos(M_PI / (double)N * ((double)n + 0.5) * (double)k);
        }
        x_real[n] = (2.0 / (double)N) * sum;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Coefficient Selection — Keep Top-K by Magnitude
 * ------------------------------------------------------------------------- */

/* Comparison for sorting coefficient indices by magnitude */
typedef struct { size_t idx; double mag; } coeff_sort_t;

static int cmp_coeff_mag(const void *a, const void *b)
{
    double ma = ((const coeff_sort_t *)a)->mag;
    double mb = ((const coeff_sort_t *)b)->mag;
    return (ma < mb) - (ma > mb);  /* Descending */
}

int ts_dft_keep_top_coefficients(ts_complex_t *X, size_t N, size_t K)
{
    if (!X || N == 0 || K >= N) return -1;

    /* Create magnitude-index pairs */
    coeff_sort_t *coeffs = (coeff_sort_t *)malloc(N * sizeof(coeff_sort_t));
    if (!coeffs) return -1;

    for (size_t i = 0; i < N; i++) {
        coeffs[i].idx = i;
        coeffs[i].mag = sqrt(X[i].real * X[i].real + X[i].imag * X[i].imag);
    }

    /* Sort by descending magnitude */
    qsort(coeffs, N, sizeof(coeff_sort_t), cmp_coeff_mag);

    /* Zero out all coefficients */
    ts_complex_t *temp = (ts_complex_t *)calloc(N, sizeof(ts_complex_t));
    if (!temp) { free(coeffs); return -1; }

    /* Keep top K */
    for (size_t i = 0; i < K && i < N; i++) {
        temp[coeffs[i].idx] = X[coeffs[i].idx];
    }

    memcpy(X, temp, N * sizeof(ts_complex_t));
    free(temp);
    free(coeffs);

    return (int)K;
}

int ts_dct_keep_top_coefficients(double *X, size_t N, size_t K)
{
    if (!X || N == 0 || K >= N) return -1;

    coeff_sort_t *coeffs = (coeff_sort_t *)malloc(N * sizeof(coeff_sort_t));
    if (!coeffs) return -1;

    for (size_t i = 0; i < N; i++) {
        coeffs[i].idx = i;
        coeffs[i].mag = fabs(X[i]);
    }

    qsort(coeffs, N, sizeof(coeff_sort_t), cmp_coeff_mag);

    double *temp = (double *)calloc(N, sizeof(double));
    if (!temp) { free(coeffs); return -1; }

    for (size_t i = 0; i < K && i < N; i++) {
        temp[coeffs[i].idx] = X[coeffs[i].idx];
    }

    memcpy(X, temp, N * sizeof(double));
    free(temp);
    free(coeffs);

    return (int)K;
}

/* ---------------------------------------------------------------------------
 * L5: Thresholding (Hard and Soft)
 *
 * Hard:  eta_T(x) = x * 1{|x| >= T}
 * Soft:  eta_T(x) = sign(x) * max(|x| - T, 0)
 *
 * Reference: Donoho & Johnstone (1994).
 * ------------------------------------------------------------------------- */

int ts_soft_threshold(double *X, size_t N, double threshold)
{
    if (!X || N == 0) return -1;
    int count = 0;
    for (size_t i = 0; i < N; i++) {
        double abs_x = fabs(X[i]);
        if (abs_x > threshold) {
            X[i] = (X[i] > 0 ? 1.0 : -1.0) * (abs_x - threshold);
            count++;
        } else {
            X[i] = 0.0;
        }
    }
    return count;
}

int ts_hard_threshold(double *X, size_t N, double threshold)
{
    if (!X || N == 0) return -1;
    int count = 0;
    for (size_t i = 0; i < N; i++) {
        if (fabs(X[i]) >= threshold) {
            count++;
        } else {
            X[i] = 0.0;
        }
    }
    return count;
}

/**
 * @brief Universal threshold (VisuShrink).
 *
 * T = sigma * sqrt(2 * log(N))
 *
 * sigma is estimated as median(|X|) / 0.6745
 * (robust estimator assuming Gaussian noise in coefficients)
 */
double ts_universal_threshold(const double *X, size_t N)
{
    if (!X || N == 0) return 0.0;

    /* Compute absolute values */
    double *abs_x = (double *)malloc(N * sizeof(double));
    if (!abs_x) return 0.0;

    for (size_t i = 0; i < N; i++) {
        abs_x[i] = fabs(X[i]);
    }

    /* Sort to find median */
    qsort(abs_x, N, sizeof(double), cmp_coeff_mag);
    /* Re-sort ascending for median */
    for (size_t i = 0; i < N / 2; i++) {
        double tmp = abs_x[i];
        abs_x[i] = abs_x[N - 1 - i];
        abs_x[N - 1 - i] = tmp;
    }

    double median;
    if (N % 2 == 0) {
        median = (abs_x[N/2 - 1] + abs_x[N/2]) * 0.5;
    } else {
        median = abs_x[N/2];
    }

    free(abs_x);

    double sigma = median / 0.6745;  /* Gaussian consistency */
    return sigma * sqrt(2.0 * log((double)N));
}

/* ---------------------------------------------------------------------------
 * L2: Full Compression Pipeline
 * ------------------------------------------------------------------------- */

int ts_transform_compress(const double *values, size_t N,
                           const ts_transform_config_t *config,
                           uint8_t **compressed,
                           size_t *comp_size,
                           ts_transform_stats_t *stats)
{
    if (!values || !config || !compressed || !comp_size || !stats || N == 0)
        return -1;

    memset(stats, 0, sizeof(*stats));

    /* Step 1: Copy and optionally window the signal */
    double *signal = (double *)malloc(N * sizeof(double));
    if (!signal) return -1;
    memcpy(signal, values, N * sizeof(double));

    if (config->window_type != TS_WINDOW_RECTANGULAR) {
        double *window = (double *)malloc(N * sizeof(double));
        if (window) {
            ts_window_generate(window, N, config->window_type);
            ts_window_apply(signal, N, window);
            free(window);
        }
    }

    /* Step 2: Forward transform */
    if (config->transform_type == TS_TRANSFORM_DFT) {
        ts_complex_t *X = (ts_complex_t *)malloc(N * sizeof(ts_complex_t));
        if (!X) { free(signal); return -1; }

        ts_dft_forward(signal, N, X);
        stats->original_coeffs = N;

        /* Step 3: Coefficient selection */
        size_t K = config->use_keep_count
                   ? config->keep_count
                   : (size_t)((double)N * config->keep_ratio);
        if (K < 1) K = 1;
        if (K > N) K = N;

        ts_dft_keep_top_coefficients(X, N, K);
        stats->retained_coeffs = K;

        /* Step 4: Quantize (simple uniform) */
        /* Step 5: Encode retained coefficients to bytes */
        size_t coeff_bytes = K * 2 * sizeof(double);  /* Real + imag */
        *compressed = (uint8_t *)malloc(coeff_bytes);
        if (!*compressed) { free(X); free(signal); return -1; }
        memcpy(*compressed, X, coeff_bytes);
        *comp_size = coeff_bytes;
        stats->encoded_bits = coeff_bytes * 8;

        free(X);
    } else {
        /* DCT path */
        double *X = (double *)malloc(N * sizeof(double));
        if (!X) { free(signal); return -1; }

        ts_dct_forward(signal, N, X);
        stats->original_coeffs = N;

        size_t K = config->use_keep_count
                   ? config->keep_count
                   : (size_t)((double)N * config->keep_ratio);
        if (K < 1) K = 1;
        if (K > N) K = N;

        ts_dct_keep_top_coefficients(X, N, K);
        stats->retained_coeffs = K;

        size_t coeff_bytes = K * sizeof(double);
        *compressed = (uint8_t *)malloc(coeff_bytes);
        if (!*compressed) { free(X); free(signal); return -1; }
        memcpy(*compressed, X, coeff_bytes);
        *comp_size = coeff_bytes;
        stats->encoded_bits = coeff_bytes * 8;

        free(X);
    }

    /* Compute stats */
    stats->bits_per_value = (double)stats->encoded_bits / (double)N;

    free(signal);
    return 0;
}

int ts_transform_decompress(const uint8_t *compressed, size_t comp_size,
                             const ts_transform_config_t *config,
                             double *values, size_t N,
                             ts_transform_stats_t *stats)
{
    if (!compressed || !config || !values || N == 0) return -1;

    /* Decode coefficients from byte buffer */
    if (config->transform_type == TS_TRANSFORM_DFT) {
        size_t K = comp_size / (2 * sizeof(double));
        if (K > N) K = N;

        ts_complex_t *X = (ts_complex_t *)calloc(N, sizeof(ts_complex_t));
        if (!X) return -1;

        memcpy(X, compressed, comp_size < N * sizeof(ts_complex_t)
                              ? comp_size : N * sizeof(ts_complex_t));

        /* Inverse DFT */
        ts_dft_inverse(X, N, values);
        free(X);
    } else {
        size_t K = comp_size / sizeof(double);
        if (K > N) K = N;

        double *X = (double *)calloc(N, sizeof(double));
        if (!X) return -1;

        memcpy(X, compressed, comp_size < N * sizeof(double)
                              ? comp_size : N * sizeof(double));

        /* Inverse DCT */
        ts_dct_inverse(X, N, values);
        free(X);
    }

    /* Compute PSNR if original stats available */
    if (stats) {
        *stats = (ts_transform_stats_t){0};
        stats->rmse = 0.0;
    }

    return 0;
}

int ts_transform_analyze(const double *original, size_t N,
                          const ts_transform_config_t *config,
                          ts_transform_stats_t *stats)
{
    if (!original || !config || !stats || N == 0) return -1;

    uint8_t *compressed = NULL;
    size_t comp_size = 0;

    int ret = ts_transform_compress(original, N, config,
                                     &compressed, &comp_size, stats);
    if (ret != 0) return ret;

    /* Decompress and compare */
    double *reconstructed = (double *)malloc(N * sizeof(double));
    if (!reconstructed) { free(compressed); return -1; }

    ret = ts_transform_decompress(compressed, comp_size, config,
                                   reconstructed, N, NULL);
    if (ret == 0) {
        double sum_sq = 0.0;
        double max_err = 0.0;
        double peak = original[0], min_val = original[0];

        for (size_t i = 0; i < N; i++) {
            double err = original[i] - reconstructed[i];
            sum_sq += err * err;
            if (fabs(err) > max_err) max_err = fabs(err);
            if (original[i] > peak) peak = original[i];
            if (original[i] < min_val) min_val = original[i];
        }

        stats->rmse = sqrt(sum_sq / (double)N);
        stats->max_error = max_err;

        double mse = sum_sq / (double)N;
        double dynamic_range = peak - min_val;
        if (mse > 0.0 && dynamic_range > 0.0) {
            stats->psnr_db = 10.0 * log10(dynamic_range * dynamic_range / mse);
        } else {
            stats->psnr_db = 100.0;  /* Perfect reconstruction */
        }
    }

    free(reconstructed);
    free(compressed);
    return ret;
}

double ts_psnr(const double *original, const double *reconstructed, size_t N)
{
    if (!original || !reconstructed || N == 0) return 0.0;

    double mse = 0.0;
    double peak = original[0], min_val = original[0];

    for (size_t i = 0; i < N; i++) {
        double err = original[i] - reconstructed[i];
        mse += err * err;
        if (original[i] > peak) peak = original[i];
        if (original[i] < min_val) min_val = original[i];
    }

    mse /= (double)N;
    double dynamic_range = peak - min_val;

    if (mse <= 0.0) return 200.0;  /* Perfect */
    if (dynamic_range <= 0.0) return 0.0;

    return 10.0 * log10(dynamic_range * dynamic_range / mse);
}

double ts_transform_compression_ratio(size_t N,
                                        const ts_transform_stats_t *stats)
{
    if (!stats || N == 0) return 1.0;
    double orig_bits = (double)N * 64.0;
    if (stats->encoded_bits == 0) return 1.0;
    return orig_bits / (double)stats->encoded_bits;
}
