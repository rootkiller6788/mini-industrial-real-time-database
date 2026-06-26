/**
 * @file test_ts_algorithms.c
 * @brief Assert-based test suite for time-series compression algorithms
 *
 * Tests all core modules: deadband, swinging door, delta encoding,
 * PLA, transform compression, entropy coding, and quality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ts_deadband.h"
#include "ts_swinging_door.h"
#include "ts_delta_encoding.h"
#include "ts_piecewise_linear.h"
#include "ts_transform.h"
#include "ts_entropy.h"
#include "ts_quality.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST %s ... ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

/* ---------------------------------------------------------------------------
 * Deadband Tests
 * ------------------------------------------------------------------------- */
static void test_deadband_absolute(void)
{
    TEST("Deadband absolute mode");
    ts_deadband_state_t state;
    ts_deadband_config_t config = {
        .mode = TS_DEADBAND_ABSOLUTE,
        .threshold_abs = 0.5
    };
    ts_deadband_init(&state, &config);

    ts_data_point_t p1 = {.epoch_us = 1000000, .value = 10.0, .quality = TS_QUALITY_GOOD};
    ts_data_point_t p2 = {.epoch_us = 2000000, .value = 10.2, .quality = TS_QUALITY_GOOD};
    ts_data_point_t p3 = {.epoch_us = 3000000, .value = 10.7, .quality = TS_QUALITY_GOOD};
    ts_data_point_t p4 = {.epoch_us = 4000000, .value = 10.85, .quality = TS_QUALITY_GOOD};

    bool archived;
    ts_deadband_filter(&state, &p1, &archived);
    assert(archived == true);  /* First point always */

    ts_deadband_filter(&state, &p2, &archived);
    assert(archived == false); /* delta = 0.2 < 0.5 */

    ts_deadband_filter(&state, &p3, &archived);
    assert(archived == true);  /* delta = 0.7 >= 0.5 */

    ts_deadband_filter(&state, &p4, &archived);
    assert(archived == false); /* delta = 0.15 < 0.5 */

    ts_compression_stats_t stats;
    ts_deadband_get_stats(&state, &stats);
    assert(stats.points_received == 4);
    assert(stats.points_archived == 2);

    PASS();
}

static void test_deadband_none(void)
{
    TEST("Deadband none mode");
    ts_deadband_state_t state;
    ts_deadband_init(&state, NULL);

    ts_data_point_t p = {.epoch_us = 1000000, .value = 5.0, .quality = TS_QUALITY_GOOD};
    bool archived;
    for (int i = 0; i < 100; i++) {
        ts_deadband_filter(&state, &p, &archived);
        assert(archived == true);
    }
    PASS();
}

static void test_deadband_compression_estimate(void)
{
    TEST("Deadband compression ratio estimate");
    double ratio = ts_deadband_estimate_compression(1.0, 1.0);
    assert(ratio > 1.0);
    assert(ratio < 10.0);

    /* Larger threshold -> larger ratio */
    double ratio2 = ts_deadband_estimate_compression(2.0, 1.0);
    assert(ratio2 > ratio);
    PASS();
}

/* ---------------------------------------------------------------------------
 * Swinging Door Tests
 * ------------------------------------------------------------------------- */
static void test_swinging_door_basic(void)
{
    TEST("Swinging door basic");
    ts_swinging_door_state_t state;
    ts_swinging_door_config_t config = {
        .compdev = 0.5,
        .compmax_us = 10000000LL  /* 10 seconds */
    };
    ts_swinging_door_init(&state, &config);

    /* Linearly increasing signal: should be compressed to endpoints */
    ts_data_point_t pts[10];
    for (int i = 0; i < 10; i++) {
        pts[i].epoch_us = (int64_t)(i + 1) * 1000000LL;
        pts[i].value = (double)i;
        pts[i].quality = TS_QUALITY_GOOD;
    }

    size_t num_out = 0;

    for (int i = 0; i < 10; i++) {
        bool arch;
        ts_data_point_t out;
        ts_swinging_door_filter(&state, &pts[i], &arch, &out);
        if (arch) {
            num_out++;
        }
    }

    /* Flush remaining */
    ts_data_point_t flush[2];
    size_t flush_n;
    ts_swinging_door_flush(&state, flush, &flush_n);
    num_out += flush_n;

    assert(num_out > 0);
    assert(num_out < 10);  /* Should achieve some compression */

    PASS();
}

static void test_swinging_door_noisy_signal(void)
{
    TEST("Swinging door noisy signal");
    ts_swinging_door_state_t state;
    ts_swinging_door_config_t config = {
        .compdev = 1.0,
        .compmax_us = 5000000LL
    };
    ts_swinging_door_init(&state, &config);

    ts_data_point_t pts[20];
    ts_data_point_t outputs[20];
    size_t num_out = 0;

    for (int i = 0; i < 20; i++) {
        pts[i].epoch_us = (int64_t)i * 1000000LL;
        pts[i].value = 10.0 + 0.3 * sin((double)i * 0.5) + ((i % 3) ? 0.0 : 2.0);
        pts[i].quality = TS_QUALITY_GOOD;

        bool arch;
        ts_data_point_t out;
        ts_swinging_door_filter(&state, &pts[i], &arch, &out);
        if (arch) outputs[num_out++] = out;
    }

    ts_data_point_t flush[2];
    size_t flush_n;
    ts_swinging_door_flush(&state, flush, &flush_n);
    for (size_t i = 0; i < flush_n; i++) outputs[num_out++] = flush[i];

    assert(num_out > 0);
    assert(num_out <= 20);

    /* Check RMSE */
    double rmse, max_err;
    int ret = ts_swinging_door_reconstruction_error(
        pts, 20, outputs, num_out, &rmse, &max_err);
    assert(ret == 0);
    printf("(RMSE=%.3f, max_err=%.3f) ", rmse, max_err);

    PASS();
}

/* ---------------------------------------------------------------------------
 * Delta Encoding Tests
 * ------------------------------------------------------------------------- */
static void test_delta_encoding_roundtrip(void)
{
    TEST("Delta encoding roundtrip");
    ts_delta_encode_buffer_t buf;
    ts_delta_encode_init(&buf, 4096);

    ts_data_point_t original[5];
    for (int i = 0; i < 5; i++) {
        original[i].epoch_us = 1000000LL * (int64_t)(i + 1);
        original[i].value = (double)(100 + i);
        original[i].quality = TS_QUALITY_GOOD;
    }

    int enc = ts_delta_encode_batch(&buf, original, 5);
    assert(enc == 5);

    ts_delta_decode_buffer_t dec_buf;
    ts_delta_decode_init(&dec_buf, buf.data, buf.size);

    ts_data_point_t decoded[5];
    size_t n_dec;
    int ret = ts_delta_decode_batch(&dec_buf, decoded, 5, &n_dec);
    assert(ret == 0);
    assert(n_dec == 5);

    for (size_t i = 0; i < 5; i++) {
        assert(decoded[i].epoch_us == original[i].epoch_us);
        assert(fabs(decoded[i].value - original[i].value) < 1e-10);
    }

    ts_delta_encode_free(&buf);
    PASS();
}

static void test_zigzag_encoding(void)
{
    TEST("ZigZag encoding");
    assert(ts_zigzag_encode(0) == 0);
    assert(ts_zigzag_encode(-1) == 1);
    assert(ts_zigzag_encode(1) == 2);
    assert(ts_zigzag_encode(-2) == 3);
    assert(ts_zigzag_encode(2) == 4);

    assert(ts_zigzag_decode(0) == 0);
    assert(ts_zigzag_decode(1) == -1);
    assert(ts_zigzag_decode(2) == 1);
    assert(ts_zigzag_decode(3) == -2);
    assert(ts_zigzag_decode(4) == 2);

    PASS();
}

static void test_varint_roundtrip(void)
{
    TEST("Varint roundtrip");
    ts_delta_encode_buffer_t ebuf;
    ts_delta_encode_init(&ebuf, 1024);

    uint64_t test_vals[] = {0, 1, 127, 128, 16383, 16384, 2097151, 2097152};
    int n = 8;

    for (int i = 0; i < n; i++) {
        int ret = ts_varint_encode(&ebuf, test_vals[i]);
        assert(ret > 0);
    }

    ts_delta_decode_buffer_t dbuf;
    ts_delta_decode_init(&dbuf, ebuf.data, ebuf.size);

    for (int i = 0; i < n; i++) {
        uint64_t val;
        int ret = ts_varint_decode(&dbuf, &val);
        assert(ret > 0);
        assert(val == test_vals[i]);
    }

    ts_delta_encode_free(&ebuf);
    PASS();
}

/* ---------------------------------------------------------------------------
 * PLA Tests
 * ------------------------------------------------------------------------- */
static void test_pla_sliding_window(void)
{
    TEST("PLA sliding window");
    /* Linear ramp: should produce 1 segment */
    ts_data_point_t pts[10];
    for (int i = 0; i < 10; i++) {
        pts[i].epoch_us = (int64_t)i * 1000000LL;
        pts[i].value = (double)i * 2.0;
        pts[i].quality = TS_QUALITY_GOOD;
    }

    ts_pla_model_t model;
    ts_pla_model_init(&model, 10);

    ts_pla_config_t config = {
        .algorithm = TS_PLA_SLIDING_WINDOW,
        .error_tolerance = 0.1,
        .min_segment_points = 2
    };

    int ret = ts_pla_sliding_window(&model, pts, 10, &config);
    assert(ret == 0);
    /* Linear ramp with tight tolerance should have few segments */
    assert(model.num_segments >= 1);

    ts_pla_model_free(&model);
    PASS();
}

static void test_pla_point_segment_distance(void)
{
    TEST("PLA point-segment distance");
    /* Point (5, 5) to line from (0,0) to (10,10) -> distance = 0 */
    double d1 = ts_pla_point_segment_distance(0, 0.0, 10, 10.0, 5, 5.0);
    assert(fabs(d1) < 1e-10);

    /* Point (0, 1) to line from (0,0) to (10,0) -> distance = 1 */
    double d2 = ts_pla_point_segment_distance(0, 0.0, 10, 0.0, 0, 1.0);
    assert(fabs(d2 - 1.0) < 1e-10);

    PASS();
}

/* ---------------------------------------------------------------------------
 * Transform Tests
 * ------------------------------------------------------------------------- */
static void test_dft_roundtrip(void)
{
    TEST("DFT roundtrip");
    double signal[8];
    for (int i = 0; i < 8; i++) {
        signal[i] = sin(2.0 * M_PI * (double)i / 8.0);
    }

    ts_complex_t X[8];
    ts_dft_forward(signal, 8, X);

    double reconstructed[8];
    ts_dft_inverse(X, 8, reconstructed);

    for (int i = 0; i < 8; i++) {
        assert(fabs(reconstructed[i] - signal[i]) < 1e-12);
    }

    PASS();
}

static void test_dct_roundtrip(void)
{
    TEST("DCT roundtrip");
    double signal[8];
    for (int i = 0; i < 8; i++) {
        signal[i] = (double)(i * i);
    }

    double X[8];
    ts_dct_forward(signal, 8, X);

    double reconstructed[8];
    ts_dct_inverse(X, 8, reconstructed);

    for (int i = 0; i < 8; i++) {
        assert(fabs(reconstructed[i] - signal[i]) < 1e-6);
    }

    PASS();
}

static void test_window_generation(void)
{
    TEST("Window generation");
    double window[256];
    int ret;

    ret = ts_window_generate(window, 256, TS_WINDOW_HANN);
    assert(ret == 0);
    assert(fabs(window[0]) < 1e-10);       /* Hann: first = 0 */
    assert(window[128] > 0.5);              /* Hann: center = 1.0 */
    assert(fabs(window[255]) < 1e-10);     /* Hann: last = 0 */

    ret = ts_window_generate(window, 256, TS_WINDOW_RECTANGULAR);
    assert(ret == 0);
    assert(window[0] == 1.0);
    assert(window[255] == 1.0);

    PASS();
}

static void test_universal_threshold(void)
{
    TEST("Universal threshold");
    double X[100];
    for (int i = 0; i < 100; i++) X[i] = 0.0;
    X[0] = 10.0;
    X[1] = -5.0;
    X[2] = 3.0;
    /* Add noise */
    for (int i = 3; i < 100; i++) X[i] = 0.1 * ((double)rand() / RAND_MAX - 0.5);

    double T = ts_universal_threshold(X, 100);
    assert(T > 0.0);

    PASS();
}

/* ---------------------------------------------------------------------------
 * Entropy Tests
 * ------------------------------------------------------------------------- */
static void test_empirical_entropy(void)
{
    TEST("Empirical entropy");
    uint64_t freqs[4] = {1, 1, 1, 1};  /* Uniform distribution */
    double H = ts_empirical_entropy(freqs, 3, 4);
    /* Uniform over 4 symbols: H = -4*(0.25*log2(0.25)) = 2.0 */
    assert(fabs(H - 2.0) < 0.01);

    uint64_t freqs2[4] = {4, 0, 0, 0};  /* Deterministic */
    double H2 = ts_empirical_entropy(freqs2, 3, 4);
    assert(fabs(H2 - 0.0) < 0.01);

    PASS();
}

static void test_huffman_build(void)
{
    TEST("Huffman tree build");
    uint64_t freqs[256];
    memset(freqs, 0, sizeof(freqs));
    freqs['a'] = 45;
    freqs['b'] = 13;
    freqs['c'] = 12;
    freqs['d'] = 16;
    freqs['e'] = 9;
    freqs['f'] = 5;

    ts_huffman_model_t model;
    int ret = ts_huffman_build(&model, freqs, 255);
    assert(ret == 0);
    assert(model.root != NULL);
    assert(model.num_symbols == 6);
    assert(model.total_frequency == 100);
    assert(model.entropy_bits > 0.0);

    ts_huffman_free(&model);
    PASS();
}

/* ---------------------------------------------------------------------------
 * Quality Tests
 * ------------------------------------------------------------------------- */
static void test_quality_macros(void)
{
    TEST("Quality macros");
    assert(TS_QUALITY_IS_GOOD(TS_QUALITY_GOOD));
    assert(!TS_QUALITY_IS_GOOD(TS_QUALITY_BAD));
    assert(TS_QUALITY_IS_BAD(TS_QUALITY_BAD));
    assert(TS_QUALITY_IS_UNCERTAIN(TS_QUALITY_UNCERTAIN));
    PASS();
}

static void test_interpolation_linear(void)
{
    TEST("Linear interpolation");
    ts_data_point_t archived[3] = {
        {.epoch_us = 1000000, .value = 0.0, .quality = TS_QUALITY_GOOD},
        {.epoch_us = 2000000, .value = 10.0, .quality = TS_QUALITY_GOOD},
        {.epoch_us = 3000000, .value = 20.0, .quality = TS_QUALITY_GOOD}
    };

    double v = ts_interpolate(archived, 3, 1500000, TS_INTERP_LINEAR);
    assert(fabs(v - 5.0) < 1e-10);

    v = ts_interpolate(archived, 3, 2500000, TS_INTERP_LINEAR);
    assert(fabs(v - 15.0) < 1e-10);

    PASS();
}

static void test_interpolation_step(void)
{
    TEST("Step interpolation");
    ts_data_point_t archived[2] = {
        {.epoch_us = 1000000, .value = 100.0, .quality = TS_QUALITY_GOOD},
        {.epoch_us = 5000000, .value = 200.0, .quality = TS_QUALITY_GOOD}
    };

    double v = ts_interpolate(archived, 2, 3000000, TS_INTERP_STEP);
    assert(fabs(v - 100.0) < 1e-10);

    PASS();
}

static void test_lerp(void)
{
    TEST("Lerp function");
    assert(fabs(ts_lerp(0, 0.0, 10, 10.0, 5) - 5.0) < 1e-10);
    assert(fabs(ts_lerp(0, 0.0, 10, 10.0, 0) - 0.0) < 1e-10);
    assert(fabs(ts_lerp(0, 0.0, 10, 10.0, 10) - 10.0) < 1e-10);
    PASS();
}

/* ---------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */
int main(void)
{
    printf("=== Time-Series Compression Algorithms Test Suite ===\n\n");

    test_deadband_absolute();
    test_deadband_none();
    test_deadband_compression_estimate();
    test_swinging_door_basic();
    test_swinging_door_noisy_signal();
    test_delta_encoding_roundtrip();
    test_zigzag_encoding();
    test_varint_roundtrip();
    test_pla_sliding_window();
    test_pla_point_segment_distance();
    test_dft_roundtrip();
    test_dct_roundtrip();
    test_window_generation();
    test_universal_threshold();
    test_empirical_entropy();
    test_huffman_build();
    test_quality_macros();
    test_interpolation_linear();
    test_interpolation_step();
    test_lerp();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
