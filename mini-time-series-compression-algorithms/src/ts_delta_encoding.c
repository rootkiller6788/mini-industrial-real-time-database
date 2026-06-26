/**
 * @file ts_delta_encoding.c
 * @brief Delta, Delta-of-Delta, XOR, Gorilla, Simple-8b Encoding Implementation
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge: L2 Concepts, L5 Algorithms, L7 Industrial Applications
 *
 * Implements the Facebook Gorilla encoding scheme (Pelkonen et al., VLDB 2015)
 * along with delta, delta-of-delta, varint, ZigZag, and Simple-8b.
 *
 * The Gorilla paper demonstrated 10-12x compression of monitoring time
 * series using:
 *   - Delta-of-delta for timestamps (1 bit per regular-interval point)
 *   - XOR for float64 values (exploits small changes in floating-point bits)
 *
 * Reference: Pelkonen, T. et al. (2015). VLDB 2015.
 * Curriculum: MIT 6.302, Stanford EE392, CMU 18-771
 */

#include "ts_delta_encoding.h"
#include "ts_deadband.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---------------------------------------------------------------------------
 * L5: Bit-Level Operations for Variable-Length Encoding
 *
 * All encoding functions write individual bits into a byte buffer.
 * The bit position is tracked separately from the byte position:
 *   - bit_pos: which bit within the current byte (0-7, 0=MSB for clarity,
 *              but we use 0=LSB convention here for simplicity)
 *
 * We use LSB-first bit packing: bits are packed from LSB to MSB within
 * each byte.
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * L5: Write a sequence of N bits (MSB first within each byte)
 *
 * This function writes arbitrary bit sequences for Gorilla-style encoding.
 * ------------------------------------------------------------------------- */

static size_t bit_write_pos = 0;  /* 0-7, next bit position within byte */
static size_t byte_write_idx = 0; /* Current byte index */

static int write_bits(ts_delta_encode_buffer_t *buf, uint64_t value, int num_bits)
{
    if (!buf || !buf->data || num_bits <= 0 || num_bits > 64) return -1;

    for (int i = num_bits - 1; i >= 0; i--) {
        /* Expand buffer if needed */
        while (buf->size <= byte_write_idx) {
            if (buf->size >= buf->capacity) return -1;
            buf->data[buf->size] = 0;
            buf->size++;
        }

        uint8_t bit = (value >> i) & 1;
        buf->data[byte_write_idx] |= bit << (7 - bit_write_pos);

        bit_write_pos++;
        if (bit_write_pos >= 8) {
            bit_write_pos = 0;
            byte_write_idx++;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Read N bits from decode buffer
 * ------------------------------------------------------------------------- */

static int read_bits(ts_delta_decode_buffer_t *buf, uint64_t *value, int num_bits)
{
    if (!buf || !buf->data || num_bits <= 0 || num_bits > 64) return -1;

    *value = 0;
    size_t byte_idx = buf->read_pos / 8;
    int    bit_idx   = buf->read_pos % 8;

    for (int i = 0; i < num_bits; i++) {
        if (byte_idx >= buf->size) return -1;  /* Buffer exhausted */

        uint8_t bit = (buf->data[byte_idx] >> (7 - bit_idx)) & 1;
        *value = (*value << 1) | bit;

        bit_idx++;
        if (bit_idx >= 8) {
            bit_idx = 0;
            byte_idx++;
        }
        buf->read_pos++;
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Varint Encoding (LEB128-style)
 *
 * Each byte: [continuation:1][data:7]
 *   continuation=1: more bytes follow
 *   continuation=0: last byte
 *
 * This is the same encoding used by Protocol Buffers and many
 * compressed file formats. For small values (like deltas), it
 * typically uses only 1-2 bytes.
 * ------------------------------------------------------------------------- */

int ts_varint_encode(ts_delta_encode_buffer_t *buf, uint64_t value)
{
    if (!buf) return -1;

    int bytes = 0;
    do {
        if (buf->size >= buf->capacity) return -1;
        if (bytes > 10) return -1;  /* Max 10 bytes for uint64 */

        uint8_t byte_val = value & 0x7F;
        value >>= 7;

        if (value != 0) {
            byte_val |= 0x80;  /* Continuation bit */
        }

        buf->data[buf->size] = byte_val;
        buf->size++;
        bytes++;
    } while (value != 0);

    return bytes;
}

int ts_varint_decode(ts_delta_decode_buffer_t *buf, uint64_t *value)
{
    if (!buf || !value) return -1;

    *value = 0;
    int shift = 0;
    int bytes = 0;

    while (1) {
        if (buf->read_pos >= buf->size) return -1;
        if (bytes > 10) return -1;

        uint8_t byte_val = buf->data[buf->read_pos];
        buf->read_pos++;
        bytes++;

        *value |= (uint64_t)(byte_val & 0x7F) << shift;
        shift += 7;

        if (!(byte_val & 0x80)) break;  /* No continuation */
    }

    return bytes;
}

/* ---------------------------------------------------------------------------
 * L2: Delta Encoding Init/Free
 * ------------------------------------------------------------------------- */

int ts_delta_encode_init(ts_delta_encode_buffer_t *buf, size_t capacity)
{
    if (!buf || capacity == 0 || capacity > TS_DELTA_MAX_BLOCK_SIZE) return -1;

    buf->data = (uint8_t *)malloc(capacity);
    if (!buf->data) return -1;

    buf->capacity = capacity;
    buf->size = 0;
    buf->prev_timestamp = 0;
    buf->prev_value = 0.0;
    buf->prev_timestamp_delta = 0;
    buf->prev_value_delta = 0.0;
    buf->initialized = false;
    buf->point_count = 0;
    buf->num_overflows = 0;

    return 0;
}

void ts_delta_encode_free(ts_delta_encode_buffer_t *buf)
{
    if (buf && buf->data) {
        free(buf->data);
        buf->data = NULL;
        buf->capacity = 0;
        buf->size = 0;
    }
}

/* ---------------------------------------------------------------------------
 * L5: Basic Delta Encoding for a Single Point
 *
 * First point: store full timestamp and value (absolute).
 * Subsequent points: store deltas.
 *
 * Timestamp delta: (t_i - t_{i-1}) ZigZag-encoded
 * Value delta: (v_i - v_{i-1}) as raw double (8 bytes)
 *
 * This is a simple baseline — the Gorilla-style DoD and XOR variants
 * achieve much better compression for regular-interval data.
 * ------------------------------------------------------------------------- */

int ts_delta_encode_point(ts_delta_encode_buffer_t *buf,
                           const ts_data_point_t *point)
{
    if (!buf || !point) return -1;

    if (buf->size + 16 > buf->capacity) {
        buf->num_overflows++;
        return -1;  /* Would overflow */
    }

    if (!buf->initialized) {
        /* First point: store absolute values */
        memcpy(&buf->data[buf->size], &point->epoch_us, 8);
        buf->size += 8;
        memcpy(&buf->data[buf->size], &point->value, 8);
        buf->size += 8;

        buf->prev_timestamp = point->epoch_us;
        buf->prev_value = point->value;
        buf->initialized = true;
        buf->point_count = 1;
        return 0;
    }

    /* Delta encoding for subsequent points */
    int64_t delta_ts = point->epoch_us - buf->prev_timestamp;
    double delta_val = point->value - buf->prev_value;

    /* ZigZag-encode the timestamp delta into uint64 */
    uint64_t zz_ts = ts_zigzag_encode(delta_ts);

    /* Write varint-encoded timestamp delta */
    int ts_bytes = ts_varint_encode(buf, zz_ts);
    if (ts_bytes < 0) return -1;

    /* Write raw double value delta (8 bytes) */
    if (buf->size + 8 > buf->capacity) { buf->num_overflows++; return -1; }
    memcpy(&buf->data[buf->size], &delta_val, 8);
    buf->size += 8;

    buf->prev_timestamp = point->epoch_us;
    buf->prev_value = point->value;
    buf->prev_value_delta = delta_val;
    buf->point_count++;

    return 0;
}

int ts_delta_encode_batch(ts_delta_encode_buffer_t *buf,
                           const ts_data_point_t *points,
                           size_t n)
{
    if (!buf || !points) return -1;

    size_t encoded = 0;
    for (size_t i = 0; i < n; i++) {
        if (ts_delta_encode_point(buf, &points[i]) != 0) break;
        encoded++;
    }
    return (int)encoded;
}

/* ---------------------------------------------------------------------------
 * L5: Delta Decoding
 * ------------------------------------------------------------------------- */

int ts_delta_decode_init(ts_delta_decode_buffer_t *buf,
                          const uint8_t *data, size_t size)
{
    if (!buf || !data || size < 16) return -1;

    buf->data = data;
    buf->size = size;
    buf->read_pos = 0;
    buf->initialized = false;
    buf->point_count = 0;
    buf->points_decoded = 0;

    return 0;
}

int ts_delta_decode_point(ts_delta_decode_buffer_t *buf,
                           ts_data_point_t *point)
{
    if (!buf || !point) return -1;

    if (!buf->initialized) {
        /* Read first point: full timestamp (8 bytes) + full value (8 bytes) */
        if (buf->read_pos + 16 > buf->size) return 1;  /* EOF */

        memcpy(&point->epoch_us, &buf->data[buf->read_pos], 8);
        buf->read_pos += 8;
        memcpy(&point->value, &buf->data[buf->read_pos], 8);
        buf->read_pos += 8;
        point->quality = TS_QUALITY_GOOD;

        buf->prev_timestamp = point->epoch_us;
        buf->prev_value = point->value;
        buf->initialized = true;
        buf->points_decoded = 1;
        return 0;
    }

    /* Read subsequent points: varint timestamp delta + raw double value delta */
    if (buf->read_pos >= buf->size) return 1;  /* EOF */

    uint64_t zz_ts;
    int ts_bytes = ts_varint_decode(buf, &zz_ts);
    if (ts_bytes < 0) return 1;

    int64_t delta_ts = ts_zigzag_decode(zz_ts);

    if (buf->read_pos + 8 > buf->size) return 1;

    double delta_val;
    memcpy(&delta_val, &buf->data[buf->read_pos], 8);
    buf->read_pos += 8;

    point->epoch_us = buf->prev_timestamp + delta_ts;
    point->value = buf->prev_value + delta_val;
    point->quality = TS_QUALITY_GOOD;

    buf->prev_timestamp = point->epoch_us;
    buf->prev_value = point->value;
    buf->points_decoded++;

    return 0;
}

int ts_delta_decode_batch(ts_delta_decode_buffer_t *buf,
                           ts_data_point_t *points,
                           size_t max_points,
                           size_t *decoded)
{
    if (!buf || !points || !decoded) return -1;

    *decoded = 0;
    for (size_t i = 0; i < max_points; i++) {
        int ret = ts_delta_decode_point(buf, &points[i]);
        if (ret == 1) break;   /* EOF reached */
        if (ret < 0) return ret;
        (*decoded)++;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Delta-of-Delta Timestamp Encoding (Gorilla §3.1)
 *
 * For regularly-sampled data (e.g., sensor sampled every 1 second),
 * the deltas themselves change very little. DoD exploits this double
 * correlation.
 *
 * Encoding Table:
 *   DoD = 0:                   '0'
 *   DoD in [-63, 64]:          '10' + 7-bit value
 *   DoD in [-255, 256]:        '110' + 9-bit value
 *   DoD in [-2047, 2048]:      '1110' + 12-bit value
 *   Other:                     '1111' + 32-bit value
 *
 * The ranges are stored as signed values offset to unsigned:
 *   For 7-bit:  store (DoD + 63)  — range [0, 127]
 *   For 9-bit:  store (DoD + 255) — range [0, 511]
 *   For 12-bit: store (DoD + 2047) — range [0, 4095]
 * ------------------------------------------------------------------------- */

int ts_delta_of_delta_encode_timestamp(ts_delta_encode_buffer_t *buf,
                                         int64_t ts_us)
{
    if (!buf || !buf->data) return -1;

    if (!buf->initialized) {
        /* Store first timestamp as full 64-bit */
        uint64_t ts_bits;
        memcpy(&ts_bits, &ts_us, 8);
        buf->prev_timestamp = ts_us;
        buf->prev_timestamp_delta = 0;
        return write_bits(buf, ts_bits, 64);
    }

    int64_t delta = ts_us - buf->prev_timestamp;
    int64_t dod   = delta - buf->prev_timestamp_delta;

    buf->prev_timestamp = ts_us;
    buf->prev_timestamp_delta = delta;

    /* Encode DoD according to Gorilla scheme */
    if (dod == 0) {
        /* '0' — 1 bit */
        return write_bits(buf, 0, 1);
    } else if (dod >= -63 && dod <= 64) {
        /* '10' + 7-bit value */
        write_bits(buf, 2, 2);  /* '10' */
        uint64_t val = (uint64_t)(dod + 63);
        return write_bits(buf, val, 7);
    } else if (dod >= -255 && dod <= 256) {
        /* '110' + 9-bit value */
        write_bits(buf, 6, 3);  /* '110' */
        uint64_t val = (uint64_t)(dod + 255);
        return write_bits(buf, val, 9);
    } else if (dod >= -2047 && dod <= 2048) {
        /* '1110' + 12-bit value */
        write_bits(buf, 14, 4);  /* '1110' */
        uint64_t val = (uint64_t)(dod + 2047);
        return write_bits(buf, val, 12);
    } else {
        /* '1111' + 32-bit value */
        write_bits(buf, 15, 4);  /* '1111' */
        uint64_t val = (uint64_t)(int64_t)dod;
        return write_bits(buf, val, 32);
    }
}

int ts_delta_of_delta_decode_timestamp(ts_delta_decode_buffer_t *buf,
                                         int64_t *ts_us)
{
    if (!buf || !ts_us) return -1;

    if (!buf->initialized) {
        /* First timestamp: read 64 bits */
        uint64_t ts_bits;
        if (read_bits(buf, &ts_bits, 64) != 0) return -1;
        *ts_us = (int64_t)ts_bits;
        buf->prev_timestamp = *ts_us;
        buf->prev_timestamp_delta = 0;
        buf->initialized = true;
        return 0;
    }

    /* Read first bit to determine encoding */
    uint64_t first_bit;
    if (read_bits(buf, &first_bit, 1) != 0) return -1;

    int64_t dod;
    if (first_bit == 0) {
        /* '0': DoD = 0 */
        dod = 0;
    } else {
        /* Read second bit */
        uint64_t second_bit;
        if (read_bits(buf, &second_bit, 1) != 0) return -1;

        if (second_bit == 0) {
            /* '10': 7-bit value [-63, 64] */
            uint64_t val;
            if (read_bits(buf, &val, 7) != 0) return -1;
            dod = (int64_t)val - 63;
        } else {
            /* Read third bit */
            uint64_t third_bit;
            if (read_bits(buf, &third_bit, 1) != 0) return -1;

            if (third_bit == 0) {
                /* '110': 9-bit value [-255, 256] */
                uint64_t val;
                if (read_bits(buf, &val, 9) != 0) return -1;
                dod = (int64_t)val - 255;
            } else {
                /* Read fourth bit */
                uint64_t fourth_bit;
                if (read_bits(buf, &fourth_bit, 1) != 0) return -1;

                if (fourth_bit == 0) {
                    /* '1110': 12-bit value [-2047, 2048] */
                    uint64_t val;
                    if (read_bits(buf, &val, 12) != 0) return -1;
                    dod = (int64_t)val - 2047;
                } else {
                    /* '1111': 32-bit value */
                    uint64_t val;
                    if (read_bits(buf, &val, 32) != 0) return -1;
                    dod = (int64_t)val;
                }
            }
        }
    }

    int64_t delta = buf->prev_timestamp_delta + dod;
    *ts_us = buf->prev_timestamp + delta;

    buf->prev_timestamp = *ts_us;
    buf->prev_timestamp_delta = delta;

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: XOR Float Value Encoding (Gorilla §3.2)
 *
 * If successive float64 values share the same sign, exponent, and
 * upper mantissa bits, their XOR will have many leading and trailing
 * zeros. Gorilla encodes only the "meaningful" middle bits.
 *
 * Encoding:
 *   XOR = 0:                    '0'  (value unchanged)
 *   LZ & TZ match previous:     '10' + meaningful bits
 *   LZ & TZ differ:             '11' + 5-bit LZ + 6-bit meaningful_len + bits
 * ------------------------------------------------------------------------- */

/** Count leading zeros in a uint64_t */
static int clz64(uint64_t x)
{
    if (x == 0) return 64;
    int n = 0;
    if ((x >> 32) == 0) { n += 32; x <<= 32; }
    if ((x >> 48) == 0) { n += 16; x <<= 16; }
    if ((x >> 56) == 0) { n += 8;  x <<= 8;  }
    if ((x >> 60) == 0) { n += 4;  x <<= 4;  }
    if ((x >> 62) == 0) { n += 2;  x <<= 2;  }
    if ((x >> 63) == 0) { n += 1;  }
    return n;
}

/** Count trailing zeros in a uint64_t */
static int ctz64(uint64_t x)
{
    if (x == 0) return 64;
    int n = 0;
    if ((x & 0xFFFFFFFF) == 0) { n += 32; x >>= 32; }
    if ((x & 0xFFFF) == 0)     { n += 16; x >>= 16; }
    if ((x & 0xFF) == 0)       { n += 8;  x >>= 8;  }
    if ((x & 0xF) == 0)        { n += 4;  x >>= 4;  }
    if ((x & 0x3) == 0)        { n += 2;  x >>= 2;  }
    if ((x & 0x1) == 0)        { n += 1;  }
    return n;
}

/* State for XOR encoding: previous value bits and previous LZ/TZ */
static uint64_t prev_value_bits_xor = 0;
static int prev_lz_xor = 0;
static int prev_tz_xor = 0;
static bool xor_initialized = false;

int ts_xor_encode_value(ts_delta_encode_buffer_t *buf, double value)
{
    if (!buf) return -1;

    uint64_t val_bits;
    memcpy(&val_bits, &value, 8);

    if (!xor_initialized) {
        /* Store first value as full 64-bit */
        xor_initialized = true;
        prev_value_bits_xor = val_bits;
        prev_lz_xor = 0;
        prev_tz_xor = 0;
        return write_bits(buf, val_bits, 64);
    }

    uint64_t xor_val = val_bits ^ prev_value_bits_xor;
    prev_value_bits_xor = val_bits;

    if (xor_val == 0) {
        /* Value unchanged: write single '0' bit */
        return write_bits(buf, 0, 1);
    }

    int lz = clz64(xor_val);
    int tz = ctz64(xor_val);

    if (lz >= prev_lz_xor && tz >= prev_tz_xor) {
        /* LZ and TZ match or exceed previous: write '10' + meaningful bits */
        write_bits(buf, 2, 2);  /* '10' */

        int meaningful_len = 64 - prev_lz_xor - prev_tz_xor;
        uint64_t meaningful = xor_val >> prev_tz_xor;

        return write_bits(buf, meaningful, meaningful_len);
    } else {
        /* LZ and/or TZ changed: write '11' + LZ + length + bits */
        write_bits(buf, 3, 2);  /* '11' */
        write_bits(buf, (uint64_t)lz, 5);  /* 5-bit leading zeros count */

        int meaningful_len = 64 - lz - tz;
        write_bits(buf, (uint64_t)(meaningful_len), 6);  /* 6-bit length */

        uint64_t meaningful = xor_val >> tz;
        write_bits(buf, meaningful, meaningful_len);

        prev_lz_xor = lz;
        prev_tz_xor = tz;
    }

    return 0;
}

int ts_xor_decode_value(ts_delta_decode_buffer_t *buf, double *value)
{
    if (!buf || !value) return -1;

    if (!xor_initialized) {
        /* First value: read 64 bits */
        uint64_t val_bits;
        if (read_bits(buf, &val_bits, 64) != 0) return -1;
        memcpy(value, &val_bits, 8);
        prev_value_bits_xor = val_bits;
        prev_lz_xor = 0;
        prev_tz_xor = 0;
        xor_initialized = true;
        return 0;
    }

    /* Read first bit */
    uint64_t first_bit;
    if (read_bits(buf, &first_bit, 1) != 0) return -1;

    uint64_t xor_val;
    if (first_bit == 0) {
        /* '0': value unchanged */
        xor_val = 0;
    } else {
        /* Read second bit */
        uint64_t second_bit;
        if (read_bits(buf, &second_bit, 1) != 0) return -1;

        if (second_bit == 0) {
            /* '10': same LZ/TZ, meaningful bits */
            int meaningful_len = 64 - prev_lz_xor - prev_tz_xor;
            uint64_t meaningful;
            if (read_bits(buf, &meaningful, meaningful_len) != 0) return -1;
            xor_val = meaningful << prev_tz_xor;
        } else {
            /* '11': new LZ/TZ */
            uint64_t lz_bits, len_bits;
            if (read_bits(buf, &lz_bits, 5) != 0) return -1;
            if (read_bits(buf, &len_bits, 6) != 0) return -1;

            int lz = (int)lz_bits;
            int meaningful_len = (int)len_bits;
            int tz = 64 - lz - meaningful_len;

            uint64_t meaningful;
            if (read_bits(buf, &meaningful, meaningful_len) != 0) return -1;
            xor_val = meaningful << tz;

            prev_lz_xor = lz;
            prev_tz_xor = tz;
        }
    }

    uint64_t val_bits = prev_value_bits_xor ^ xor_val;
    prev_value_bits_xor = val_bits;
    memcpy(value, &val_bits, 8);

    return 0;
}

/* ---------------------------------------------------------------------------
 * L5: Simple-8b Encoding
 *
 * Selector table for Simple-8b (16 modes):
 *   Selector 0:  240 x 1-bit
 *   Selector 1:  120 x 2-bit
 *   Selector 2:  60 x 4-bit
 *   Selector 3:  30 x 8-bit
 *   etc.
 *
 * For each group of consecutive deltas, choose the selector that
 * packs all values with the smallest bits-per-value.
 * ------------------------------------------------------------------------- */

/* Selector table: {selector_code, bits_per_value, values_per_word} */
static const int S8B_SELECTORS[16][3] = {
    {0,  0,  240},  /* Skip / not used */
    {1,  0,  120},
    {2,  1,  120},
    {3,  2,  60},
    {4,  3,  30},
    {5,  4,  20},
    {6,  5,  15},
    {7,  6,  12},
    {8,  7,  10},
    {9,  8,  8},
    {10, 10, 6},
    {11, 12, 5},
    {12, 15, 4},
    {13, 20, 3},
    {14, 30, 2},
    {15, 60, 1}
};

int ts_simple8b_encode(ts_delta_encode_buffer_t *buf,
                        const uint64_t *deltas, size_t n)
{
    if (!buf || !deltas) return -1;

    size_t encoded = 0;
    size_t idx = 0;

    while (idx < n) {
        /* Find best selector for the next batch */
        int best_sel = 0;
        int best_count = 0;

        for (int s = 0; s < 16; s++) {
            int bps = S8B_SELECTORS[s][1];
            int vpw = S8B_SELECTORS[s][2];
            uint64_t max_val = (bps == 60) ? ((uint64_t)1 << 60) - 1
                                          : ((uint64_t)1 << bps) - 1;

            if (bps == 0) continue;  /* Skip 0-bit selectors */

            int count = 0;
            for (size_t j = idx; j < n && count < vpw; j++, count++) {
                if (deltas[j] > max_val) break;
            }

            if (count > best_count) {
                best_sel = s;
                best_count = count;
            }

            if (count == vpw) break;  /* Perfect fit, take it */
        }

        if (best_count == 0) break;  /* Cannot encode remaining values */

        /* Write 64-bit word: 4-bit selector + packed values */
        uint64_t word = (uint64_t)S8B_SELECTORS[best_sel][0] << 60;
        int bps = S8B_SELECTORS[best_sel][1];

        for (int i = 0; i < best_count; i++) {
            uint64_t val = deltas[idx + i] & ((((uint64_t)1) << bps) - 1);
            word |= val << (60 - bps * (i + 1));
        }

        /* Write the 8 bytes */
        if (buf->size + 8 > buf->capacity) return -1;
        for (int i = 7; i >= 0; i--) {
            buf->data[buf->size + (7 - i)] = (word >> (i * 8)) & 0xFF;
        }
        buf->size += 8;

        idx += best_count;
        encoded += best_count;
    }

    return (int)encoded;
}

/* ---------------------------------------------------------------------------
 * L2: Combined Block Encode/Decode
 * ------------------------------------------------------------------------- */

int ts_delta_encode_block(ts_delta_encode_buffer_t *buf,
                           const ts_data_point_t *points,
                           size_t n, uint16_t enc_flags)
{
    if (!buf || !points || n == 0) return -1;

    /* Store encoding flags */
    buf->data[buf->size] = enc_flags & 0xFF;
    buf->size++;
    buf->data[buf->size] = (enc_flags >> 8) & 0xFF;
    buf->size++;

    /* Encode all points */
    for (size_t i = 0; i < n; i++) {
        int ret = ts_delta_encode_point(buf, &points[i]);
        if (ret < 0) return ret;
    }

    return 0;
}

double ts_delta_compression_ratio(const ts_delta_encode_buffer_t *buf, size_t n)
{
    if (!buf || n == 0) return 1.0;
    double orig_bytes = (double)n * (double)sizeof(ts_data_point_t);
    double comp_bytes = (double)buf->size;
    if (comp_bytes <= 0.0) return 1.0;
    return orig_bytes / comp_bytes;
}
