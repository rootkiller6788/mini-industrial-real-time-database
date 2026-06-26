/**
 * @file ts_entropy.h
 * @brief Entropy Coding for Time-Series Compression — Huffman, RLE, Arithmetic
 *
 * Module: mini-time-series-compression-algorithms
 * Knowledge Coverage: L1 Definitions, L2 Concepts, L5 Algorithms
 *
 * Entropy coding is the final stage of a compression pipeline. After
 * deadband/PLA/transform stages have produced a sequence of symbols
 * (deltas, coefficients, residuals), entropy coding maps commonly
 * occurring symbols to fewer bits.
 *
 * Techniques (Shannon, 1948):
 *   1. Huffman Coding: Optimal prefix code for known symbol frequencies
 *   2. Run-Length Encoding (RLE): Compress repeated symbols
 *   3. Shannon-Fano Coding: Divide-and-conquer prefix code
 *
 * Shannon's Source Coding Theorem:
 *   A source with entropy H can be compressed to an average of H bits
 *   per symbol, and no compression scheme can achieve fewer than H bits.
 *
 * Reference: Shannon, C.E. (1948). "A Mathematical Theory of Communication."
 *            Bell System Technical Journal 27:379-423, 623-656.
 *            Huffman, D.A. (1952). Proc. IRE 40(9):1098-1101.
 * Curriculum: MIT 6.302, Stanford ENGR205, CMU 18-771, Berkeley EECS 20
 */

#ifndef TS_ENTROPY_H
#define TS_ENTROPY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "ts_delta_encoding.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * L1: Entropy Coding Data Structures
 * ------------------------------------------------------------------------- */

/** Huffman tree node */
typedef struct ts_huffman_node_t {
    int32_t  symbol;                   /* Symbol value (-1 = internal node) */
    uint64_t frequency;                /* Occurrence count */
    struct ts_huffman_node_t *left;
    struct ts_huffman_node_t *right;
} ts_huffman_node_t;

/** Huffman code table entry */
typedef struct {
    int32_t  symbol;
    uint32_t code;                     /* Huffman bit code */
    uint8_t  code_length;             /* Number of bits in code */
} ts_huffman_code_t;

/** Complete Huffman coding model */
typedef struct {
    ts_huffman_node_t *root;           /* Root of the Huffman tree */
    ts_huffman_code_t  *code_table;    /* Array of codes, indexed by symbol */
    uint32_t num_symbols;              /* Size of alphabet */
    uint32_t max_symbol;               /* Maximum symbol value */
    uint64_t total_frequency;          /* Sum of all frequencies */
    double   entropy_bits;             /* Source entropy estimate */
} ts_huffman_model_t;

/** Run-Length Encoding configuration */
typedef struct {
    uint32_t min_run_length;           /* Minimum run to encode (>=2) */
    uint32_t max_run_length;           /* Maximum encodable run length */
    bool     encode_zeros_only;        /* Only encode runs of zero */
    double   zero_threshold;           /* |value| < threshold = zero */
} ts_rle_config_t;

/** RLE encoded run */
typedef struct {
    double   value;                    /* The repeated value */
    uint32_t run_length;               /* Number of consecutive occurrences */
} ts_rle_run_t;

/** RLE compression state and results */
typedef struct {
    ts_rle_run_t *runs;
    size_t    num_runs;
    size_t    capacity;
    uint64_t  original_points;
    double    compression_ratio;
} ts_rle_state_t;

/* ---------------------------------------------------------------------------
 * L1: Symbol Frequency Table
 * ------------------------------------------------------------------------- */

/**
 * @brief Build a frequency table from an array of symbols.
 *
 * Counts occurrences of each unique value. For floating-point time
 * series residuals, symbols are typically quantized integer values
 * or discrete categories.
 *
 * @param data     Array of integer symbols
 * @param n        Number of symbols
 * @param freqs    Output: frequency array (caller allocates max_symbol+1)
 * @param max_sym  Maximum possible symbol value (determines table size)
 * @param num_unique Output: number of unique symbols found
 * @return         0 on success
 */
int ts_symbol_frequencies(const int32_t *data, size_t n,
                           uint64_t *freqs, uint32_t max_sym,
                           uint32_t *num_unique);

/**
 * @brief Compute the empirical entropy of a symbol sequence.
 *
 * H = -sum_i p_i * log2(p_i)  where p_i = freq_i / total
 *
 * @param freqs     Frequency array
 * @param max_sym   Maximum symbol index
 * @param total     Total count of symbols
 * @return          Entropy in bits per symbol
 */
double ts_empirical_entropy(const uint64_t *freqs, uint32_t max_sym,
                              uint64_t total);

/* ---------------------------------------------------------------------------
 * L5: Huffman Coding
 * ------------------------------------------------------------------------- */

/**
 * @brief Build a Huffman tree from symbol frequencies.
 *
 * Algorithm (Huffman, 1952):
 *   1. Create leaf node for each symbol with nonzero frequency
 *   2. Place all leaves in a min-heap ordered by frequency
 *   3. While heap size > 1:
 *      a. Extract two nodes with smallest frequencies
 *      b. Create new internal node as their parent (freq = sum)
 *      c. Insert new node back into heap
 *   4. The last remaining node is the tree root
 *
 * Optimality: Huffman coding produces the minimum expected code length
 * among all prefix-free codes for a given frequency distribution.
 *
 * Complexity: O(k log k) where k = number of unique symbols.
 *
 * @param model    Uninitialized model (will be filled)
 * @param freqs    Symbol frequencies
 * @param max_sym  Maximum symbol value
 * @return         0 on success
 */
int ts_huffman_build(ts_huffman_model_t *model,
                      const uint64_t *freqs, uint32_t max_sym);

/** Free all memory associated with a Huffman model */
void ts_huffman_free(ts_huffman_model_t *model);

/**
 * @brief Encode a symbol using the Huffman code table.
 *
 * Writes the variable-length Huffman code bits to the output buffer.
 *
 * @param model   Huffman model with built code table
 * @param symbol  Symbol to encode
 * @param buf     Bit output buffer
 * @return        0 on success, -1 if symbol not in table
 */
int ts_huffman_encode_symbol(const ts_huffman_model_t *model,
                              int32_t symbol,
                              ts_delta_encode_buffer_t *buf);

/**
 * @brief Decode one symbol by traversing the Huffman tree.
 *
 * Reads bits one at a time from the input, following the tree
 * until a leaf node is reached.
 *
 * @param model   Huffman model
 * @param buf     Bit input buffer
 * @param symbol  Output: decoded symbol
 * @return        0 on success
 */
int ts_huffman_decode_symbol(const ts_huffman_model_t *model,
                              ts_delta_decode_buffer_t *buf,
                              int32_t *symbol);

/**
 * @brief Compute the average code length of the Huffman code.
 *
 * L_avg = sum_i p_i * len(code_i)
 *
 * Compare with entropy H: H <= L_avg < H + 1 (Shannon's theorem bound)
 *
 * @param model  Huffman model
 * @return       Average code length in bits
 */
double ts_huffman_average_code_length(const ts_huffman_model_t *model);

/**
 * @brief Encode an entire array using Huffman coding.
 *
 * @param model    Huffman model
 * @param symbols  Array of symbols to encode
 * @param n        Number of symbols
 * @param buf      Output: encoded bit stream
 * @param out_bytes Output: number of bytes written
 * @return         0 on success
 */
int ts_huffman_encode_array(const ts_huffman_model_t *model,
                             const int32_t *symbols, size_t n,
                             ts_delta_encode_buffer_t *buf,
                             size_t *out_bytes);

/* ---------------------------------------------------------------------------
 * L5: Run-Length Encoding (RLE)
 * ------------------------------------------------------------------------- */

/**
 * @brief Initialize RLE compression state.
 */
int ts_rle_init(ts_rle_state_t *state, size_t capacity);
void ts_rle_free(ts_rle_state_t *state);

/**
 * @brief Encode a sequence using run-length encoding.
 *
 * Detects runs of consecutive identical values (or near-identical
 * within threshold) and represents them as (value, count) pairs.
 *
 * @param state    RLE state
 * @param data     Input data array
 * @param n        Number of data points
 * @param config   RLE configuration
 * @return         Number of runs, -1 on error
 */
int ts_rle_encode(ts_rle_state_t *state,
                   const double *data, size_t n,
                   const ts_rle_config_t *config);

/**
 * @brief Decode an RLE-compressed sequence.
 *
 * @param runs        Array of RLE runs
 * @param num_runs    Number of runs
 * @param output      Output: decoded data (caller allocates)
 * @param max_output  Maximum output size
 * @param decoded     Output: number of values decoded
 * @return            0 on success
 */
int ts_rle_decode(const ts_rle_run_t *runs, size_t num_runs,
                   double *output, size_t max_output,
                   size_t *decoded);

/**
 * @brief Compute RLE compression ratio.
 *
 * Original: n * sizeof(double) bytes
 * Compressed: num_runs * sizeof(ts_rle_run_t) bytes
 */
double ts_rle_compression_ratio(size_t original_points,
                                 size_t num_runs);

#ifdef __cplusplus
}
#endif

#endif /* TS_ENTROPY_H */
