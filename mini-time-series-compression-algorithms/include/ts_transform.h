/**
 * @file ts_transform.h
 * @brief Transform-Based Time-Series Compression — DFT, DCT, Wavelet Thresholding
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge Coverage: L1 Definitions, L2 Concepts, L5 Algorithms, L8 Advanced
 *
 * Transform-based compression operates in the frequency/scale domain.
 * Many industrial signals are sparse in the frequency domain — most
 * signal energy concentrates in few spectral coefficients.
 *
 * Compression Pipeline:
 *   1. Windowing (Hann/Hamming to reduce spectral leakage)
 *   2. Forward Transform (DFT/DCT on windowed block)
 *   3. Coefficient Selection (keep top-K magnitude coefficients)
 *   4. Quantization (reduce precision of retained coefficients)
 *   5. Entropy Coding (Huffman/arithmetic on quantized coefficients)
 *
 * Reference: Oppenheim & Schafer, Discrete-Time Signal Processing (3rd ed.)
 *            Ahmed, Natarajan & Rao (1974). IEEE Trans. Computers C-23(1):90-93.
 *            Donoho & Johnstone (1994). Biometrika 81(3):425-455.
 * Curriculum: MIT 6.302, Stanford ENGR205, Berkeley EECS 20, CMU 18-771
 */

#ifndef TS_TRANSFORM_H
#define TS_TRANSFORM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * L1: Complex Number Type
 * ------------------------------------------------------------------------- */

typedef struct {
    double real;
    double imag;
} ts_complex_t;

#define TS_COMPLEX_ADD(a, b)  ((ts_complex_t){(a).real+(b).real, (a).imag+(b).imag})
#define TS_COMPLEX_SUB(a, b)  ((ts_complex_t){(a).real-(b).real, (a).imag-(b).imag})
#define TS_COMPLEX_MUL(a, b)  ((ts_complex_t){(a).real*(b).real - (a).imag*(b).imag, \
                                               (a).real*(b).imag + (a).imag*(b).real})

/* ---------------------------------------------------------------------------
 * L1: Transform Types
 * ------------------------------------------------------------------------- */

typedef enum {
    TS_TRANSFORM_DFT   = 0,
    TS_TRANSFORM_DCT   = 1,
    TS_TRANSFORM_DCT4  = 2,
    TS_TRANSFORM_HADAMARD = 3
} ts_transform_type_t;

typedef enum {
    TS_WINDOW_RECTANGULAR = 0,
    TS_WINDOW_HANN        = 1,
    TS_WINDOW_HAMMING     = 2,
    TS_WINDOW_BLACKMAN    = 3,
    TS_WINDOW_BLACKMAN_HARRIS = 4
} ts_window_type_t;

/* ---------------------------------------------------------------------------
 * L1: Transform Configuration and Statistics
 * ------------------------------------------------------------------------- */

typedef struct {
    ts_transform_type_t transform_type;
    ts_window_type_t    window_type;
    size_t   block_size;
    double   keep_ratio;
    size_t   keep_count;
    bool     use_keep_count;
    double   magnitude_threshold;
    int      quantize_bits;
    bool     enable_overlap;
    double   overlap_ratio;
} ts_transform_config_t;

typedef struct {
    size_t   original_coeffs;
    size_t   retained_coeffs;
    double   energy_retained_pct;
    double   rmse;
    double   max_error;
    double   psnr_db;
    uint64_t encoded_bits;
    double   bits_per_value;
} ts_transform_stats_t;

/* ---------------------------------------------------------------------------
 * L2: Core Transform API
 * ------------------------------------------------------------------------- */

/**
 * @brief Generate window coefficients.
 *
 * Hann:   w[n] = 0.5 * (1 - cos(2*pi*n/(N-1)))
 * Hamming: w[n] = 0.54 - 0.46 * cos(2*pi*n/(N-1))
 * Blackman: w[n] = 0.42 - 0.5*cos(2*pi*n/N) + 0.08*cos(4*pi*n/N)
 */
int ts_window_generate(double *window, size_t N, ts_window_type_t type);
int ts_window_apply(double *signal, size_t N, const double *window);

/* ---------------------------------------------------------------------------
 * L5: DFT (Discrete Fourier Transform)
 * ------------------------------------------------------------------------- */

/**
 * @brief Compute DFT using Cooley-Tukey radix-2 FFT.
 *
 * Forward: X[k] = sum_{n=0}^{N-1} x[n]*exp(-j*2*pi*k*n/N)
 *
 * Reference: Cooley & Tukey (1965). Math. Comp. 19:297-301.
 */
int ts_dft_forward(const double *x_real, size_t N, ts_complex_t *X);
int ts_dft_inverse(const ts_complex_t *X, size_t N, double *x_real);
int ts_dft_magnitude(const ts_complex_t *X, size_t N, double *magnitude);

/* ---------------------------------------------------------------------------
 * L5: DCT (Discrete Cosine Transform)
 * ------------------------------------------------------------------------- */

/**
 * @brief Compute DCT-II.
 *
 * X[k] = sum_{n=0}^{N-1} x[n] * cos(pi/N * (n+0.5) * k)
 *
 * Reference: Ahmed, Natarajan & Rao (1974).
 */
int ts_dct_forward(const double *x_real, size_t N, double *X);
int ts_dct_inverse(const double *X, size_t N, double *x_real);

/* ---------------------------------------------------------------------------
 * L5: Coefficient Selection and Thresholding
 * ------------------------------------------------------------------------- */

int ts_dft_keep_top_coefficients(ts_complex_t *X, size_t N, size_t K);
int ts_dct_keep_top_coefficients(double *X, size_t N, size_t K);

/**
 * @brief Soft thresholding: eta_T(x) = sign(x) * max(|x|-T, 0)
 *
 * Reference: Donoho & Johnstone (1994).
 */
int ts_soft_threshold(double *X, size_t N, double threshold);
int ts_hard_threshold(double *X, size_t N, double threshold);

/**
 * @brief Universal threshold: T = sigma * sqrt(2*log(N))
 */
double ts_universal_threshold(const double *X, size_t N);

/* ---------------------------------------------------------------------------
 * L2: Full Compression/Decompression Pipeline
 * ------------------------------------------------------------------------- */

int ts_transform_compress(const double *values, size_t N,
                           const ts_transform_config_t *config,
                           uint8_t **compressed,
                           size_t *comp_size,
                           ts_transform_stats_t *stats);

int ts_transform_decompress(const uint8_t *compressed, size_t comp_size,
                             const ts_transform_config_t *config,
                             double *values, size_t N,
                             ts_transform_stats_t *stats);

int ts_transform_analyze(const double *original, size_t N,
                          const ts_transform_config_t *config,
                          ts_transform_stats_t *stats);

double ts_psnr(const double *original, const double *reconstructed, size_t N);

double ts_transform_compression_ratio(size_t N,
                                        const ts_transform_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* TS_TRANSFORM_H */
