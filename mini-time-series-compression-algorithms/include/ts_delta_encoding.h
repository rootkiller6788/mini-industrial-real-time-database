/**
 * @file ts_delta_encoding.h
 * @brief Delta, Delta-of-Delta, XOR, and Gorilla-Style Time-Series Encoding
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge Coverage: L1 Definitions, L2 Concepts, L5 Algorithms
 *
 * Delta encoding stores differences between consecutive values rather
 * than absolute values, exploiting temporal correlation for compression.
 *
 * Variants:
 *   1. Delta: store (v_i - v_{i-1})
 *   2. Delta-of-Delta (DoD): store (delta_i - delta_{i-1})
 *   3. XOR (Gorilla): XOR current float with previous
 *   4. Simple-8b: pack multiple small integers into 64-bit words
 *   5. ZigZag: map signed integers to unsigned for better varint encoding
 *
 * Reference: Pelkonen, T. et al. (2015). "Gorilla: A Fast, Scalable,
 *            In-Memory Time Series Database." VLDB 2015.
 * Curriculum: MIT 6.302, Stanford EE392, CMU 18-771
 */

#ifndef TS_DELTA_ENCODING_H
#define TS_DELTA_ENCODING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ts_deadband.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * L1: Delta Encoding Data Structures
 * ------------------------------------------------------------------------- */

typedef struct {
    int64_t  base_timestamp_us;
    double   base_value;
    uint32_t num_points;
    uint32_t block_size_bytes;
    uint16_t encoding_type;
    uint16_t reserved;
} ts_delta_block_header_t;

#define TS_ENC_DELTA            0x0001
#define TS_ENC_DELTA_OF_DELTA   0x0002
#define TS_ENC_XOR_FLOAT        0x0004
#define TS_ENC_ZIGZAG           0x0008
#define TS_ENC_VARINT           0x0010
#define TS_ENC_SIMPLE8B         0x0020
#define TS_ENC_RLE              0x0040
#define TS_ENC_HUFFMAN          0x0080

/* ---------------------------------------------------------------------------
 * L1: Delta Encoding Buffer
 * ------------------------------------------------------------------------- */

#define TS_DELTA_MAX_BLOCK_SIZE 65536

typedef struct {
    uint8_t  *data;
    size_t    capacity;
    size_t    size;
    int64_t   prev_timestamp;
    double    prev_value;
    int64_t   prev_timestamp_delta;
    double    prev_value_delta;
    bool      initialized;
    uint32_t  point_count;
    uint32_t  num_overflows;
} ts_delta_encode_buffer_t;

typedef struct {
    const uint8_t *data;
    size_t    size;
    size_t    read_pos;
    int64_t   prev_timestamp;
    double    prev_value;
    int64_t   prev_timestamp_delta;
    double    prev_value_delta;
    bool      initialized;
    uint32_t  point_count;
    uint32_t  points_decoded;
} ts_delta_decode_buffer_t;

/* ---------------------------------------------------------------------------
 * L1: ZigZag and Varint Encoding
 * ------------------------------------------------------------------------- */

/**
 * @brief ZigZag-encode a signed 64-bit integer into unsigned 64-bit.
 * Formula: (n << 1) ^ (n >> 63)
 */
static inline uint64_t ts_zigzag_encode(int64_t n) {
    return ((uint64_t)(n) << 1) ^ (uint64_t)(n >> 63);
}

/** Decode a ZigZag-encoded value back to signed 64-bit. */
static inline int64_t ts_zigzag_decode(uint64_t n) {
    return (int64_t)(n >> 1) ^ -(int64_t)(n & 1);
}

/**
 * @brief Write a variable-length unsigned integer (LEB128-style).
 * Each byte uses 7 bits for data and 1 continuation bit (MSB).
 * Complexity: O(log_128(value))
 */
int ts_varint_encode(ts_delta_encode_buffer_t *buf, uint64_t value);

int ts_varint_decode(ts_delta_decode_buffer_t *buf, uint64_t *value);

/* ---------------------------------------------------------------------------
 * L2: Delta Encoding API
 * ------------------------------------------------------------------------- */

int ts_delta_encode_init(ts_delta_encode_buffer_t *buf, size_t capacity);
void ts_delta_encode_free(ts_delta_encode_buffer_t *buf);

int ts_delta_encode_point(ts_delta_encode_buffer_t *buf,
                           const ts_data_point_t *point);

int ts_delta_encode_batch(ts_delta_encode_buffer_t *buf,
                           const ts_data_point_t *points,
                           size_t n);

int ts_delta_decode_init(ts_delta_decode_buffer_t *buf,
                          const uint8_t *data, size_t size);

int ts_delta_decode_point(ts_delta_decode_buffer_t *buf,
                           ts_data_point_t *point);

int ts_delta_decode_batch(ts_delta_decode_buffer_t *buf,
                           ts_data_point_t *points,
                           size_t max_points,
                           size_t *decoded);

/* ---------------------------------------------------------------------------
 * L5: Delta-of-Delta Encoding — Gorilla Timestamp Compression
 * ------------------------------------------------------------------------- */

/**
 * @brief Encode a timestamp using delta-of-delta encoding.
 *
 * From Facebook Gorilla (Pelkonen et al., VLDB 2015, Section 3.1):
 *   1. First timestamp stored in full (64-bit)
 *   2. Compute delta = t_i - t_{i-1}
 *   3. Compute delta-of-delta = delta_i - delta_{i-1}
 *   4. If DoD = 0:       store 1 bit '0'
 *   5. If DoD in [-63,64]: store '10' + 7 bits
 *   6. If DoD in [-255,256]: store '110' + 9 bits
 *   7. If DoD in [-2047,2048]: store '1110' + 12 bits
 *   8. Else: store '1111' + 32 bits
 */
int ts_delta_of_delta_encode_timestamp(ts_delta_encode_buffer_t *buf,
                                         int64_t ts_us);

int ts_delta_of_delta_decode_timestamp(ts_delta_decode_buffer_t *buf,
                                         int64_t *ts_us);

/* ---------------------------------------------------------------------------
 * L5: XOR Float Compression — Gorilla Value Compression
 * ------------------------------------------------------------------------- */

/**
 * @brief Encode a float64 value using XOR compression.
 *
 * From Facebook Gorilla (Section 3.2):
 *   1. First value stored in full (64-bit)
 *   2. XOR current value with previous
 *   3. If xor == 0: store 1 bit '0'
 *   4. Compute leading zeros (LZ) and trailing zeros (TZ)
 *   5. If LZ and TZ match previous: store '10' + meaningful bits
 *   6. Else: store '11' + 5 bits LZ + 6 bits meaningful length + bits
 */
int ts_xor_encode_value(ts_delta_encode_buffer_t *buf, double value);

int ts_xor_decode_value(ts_delta_decode_buffer_t *buf, double *value);

/* ---------------------------------------------------------------------------
 * L5: Simple-8b Packing
 * ------------------------------------------------------------------------- */

/**
 * @brief Encode an array of small deltas using Simple-8b.
 *
 * Reference: Anh & Moffat (2010), "Index Compression Using 64-Bit Words"
 */
int ts_simple8b_encode(ts_delta_encode_buffer_t *buf,
                        const uint64_t *deltas, size_t n);

int ts_simple8b_decode(ts_delta_decode_buffer_t *buf,
                        uint64_t *deltas, size_t max_n, size_t *decoded);

/* ---------------------------------------------------------------------------
 * L2: Combined Encoding/Decoding
 * ------------------------------------------------------------------------- */

int ts_delta_encode_block(ts_delta_encode_buffer_t *buf,
                           const ts_data_point_t *points,
                           size_t n, uint16_t enc_flags);

int ts_delta_decode_block(const uint8_t *data, size_t size,
                           ts_data_point_t *points,
                           size_t max_n, size_t *decoded);

double ts_delta_compression_ratio(const ts_delta_encode_buffer_t *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* TS_DELTA_ENCODING_H */
