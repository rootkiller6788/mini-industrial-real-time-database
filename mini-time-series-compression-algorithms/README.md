# mini-time-series-compression-algorithms

Time-Series Compression Algorithms for Industrial Real-Time Databases

## Module Status: COMPLETE ✅

- **L1-L6:** Complete
- **L7:** Complete (4 industrial applications: OSIsoft PI, Honeywell PHD, Facebook Gorilla, OPC UA)
- **L8:** Partial+ (adaptive deadband, SWAB, MDL, MAD auto-tuning, VisuShrink — all implemented)
- **L9:** Partial (documented: ML optimization, edge IIoT, digital twin)

## Overview

Implements the core compression algorithms used in industrial historian databases (OSIsoft PI, Honeywell PHD, Facebook Gorilla). Covers deadband filtering, swinging door (Bristol patent), delta/delta-of-delta/XOR encoding, piecewise linear approximation, transform-based compression (DFT/DCT), and entropy coding (Huffman/RLE).

## Nine-Level Knowledge Coverage

| Level | Name | Status | Key Items |
|-------|------|--------|-----------|
| **L1** | Definitions | Complete | Deadband, Swinging Door, DoD, XOR, PLA, DFT, DCT, Huffman, RLE, entropy, OPC quality, compression ratio |
| **L2** | Core Concepts | Complete | Archive-to-incoming comparison, parallelogram geometry, frequency-domain sparsity, prefix code optimality |
| **L3** | Engineering Structures | Complete | ts_data_point_t (24-byte), state machines, bit buffers, Huffman heap, segment models, complex numbers |
| **L4** | Engineering Laws | Complete | Shannon source coding, Huffman optimality, Bristol patent, Cooley-Tukey FFT, DCT energy compaction, Donoho-Johnstone, MDL, OPC UA Part 8 |
| **L5** | Algorithms/Methods | Complete | 6 deadband modes, swinging door, DoD, XOR, varint, Simple-8b, 4 PLA algos, FFT, DCT, hard/soft threshold, Huffman, RLE, MDL |
| **L6** | Canonical Problems | Complete | 4 examples: deadband epsilon sweep, swinging door with step, 4-way PLA comparison, DFT/DCT compression |
| **L7** | Industrial Applications | Complete | OSIsoft PI (swinging door), Honeywell PHD, Facebook Gorilla (DoD/XOR), OPC UA quality filtering |
| **L8** | Advanced Topics | Partial+ | Adaptive deadband (EMA), SWAB hybrid, MDL optimization, MAD auto-tuning, VisuShrink thresholding |
| **L9** | Industry Frontiers | Partial | ML optimization, edge IIoT compression, digital twin data reduction |

## Core Definitions

- **Deadband (epsilon):** Minimum change in process variable to trigger archiving
- **Compression Deviation (compdev):** Half-width of swinging door parallelogram corridor
- **Compression Maximum (compmax):** Maximum time between archived points
- **Delta-of-Delta (DoD):** Second-order difference for timestamp compression
- **XOR Float:** Bitwise XOR of consecutive IEEE 754 values for compression
- **PLA Segment:** Linear segment connecting two archived points, all intermediate values within epsilon
- **DFT/DCT Coefficient:** Frequency-domain representation of signal
- **Entropy (H):** Minimum average bits per symbol for lossless compression
- **Compression Ratio:** original_size / compressed_size (>= 1.0)

## Core Theorems

1. **Shannon Source Coding (1948):** H <= L_avg < H + 1 where H is entropy, L_avg is average code length
2. **Huffman Optimality (1952):** Huffman code minimizes expected code length among all prefix-free codes
3. **Bristol Swinging Door (1987):** Parallelogram with compdev guarantees reconstruction error <= compdev
4. **Cooley-Tukey FFT (1965):** DFT computed in O(N log N) via radix-2 decomposition
5. **DCT Energy Compaction (1974):** DCT-II asymptotically approaches KLT for Markov-1 signals
6. **Donoho-Johnstone (1994):** Soft thresholding with universal threshold asymptotically minimax
7. **MDL Principle (Rissanen, 1978):** Optimal model minimizes description length = model cost + data cost

## Core Algorithms

1. **Deadband Filter** — O(1) per point, 6 modes: absolute, percent, time, combined, rate-of-change, adaptive
2. **Swinging Door** — O(1) per point, Bristol 1987, OSIsoft PI core algorithm
3. **Delta-of-Delta Timestamp** — Facebook Gorilla §3.1, 1 bit per regular-interval point
4. **XOR Float Value** — Facebook Gorilla §3.2, exploits leading/trailing zero structure in float64 XOR
5. **Simple-8b Packing** — Word-aligned integer packing, 16 selector modes
6. **PLA Sliding Window** — Online segmentation, O(n*L) average
7. **PLA Top-Down** — Douglas-Peucker recursive split, O(n log n) average
8. **PLA Bottom-Up** — Greedy merge with priority queue, O(n log n)
9. **PLA SWAB** — Keogh's hybrid, O(n*B) bounded by buffer
10. **Radix-2 FFT** — Cooley-Tukey, O(N log N), bit-reversal + butterfly
11. **DCT-II** — Type-II discrete cosine transform, O(N^2) direct formulation
12. **Huffman Coding** — Min-heap construction, O(k log k) for k unique symbols
13. **Run-Length Encoding** — Zero-threshold compression for residual streams
14. **MDL Optimal Segments** — Binary search on tolerance, O(K^2 log tol_range)

## File Structure

```
mini-time-series-compression-algorithms/
├── Makefile                      # make test / make examples / make lean
├── README.md                     # This file
├── include/
│   ├── ts_deadband.h             # Deadband filter types and API
│   ├── ts_swinging_door.h         # Swinging door (Bristol) algorithm
│   ├── ts_delta_encoding.h        # Delta, DoD, XOR, Varint, Simple-8b
│   ├── ts_piecewise_linear.h      # PLA segmentation (SW, TD, BU, SWAB)
│   ├── ts_transform.h             # DFT, DCT, window functions, thresholding
│   ├── ts_entropy.h               # Huffman, RLE, symbol frequency analysis
│   └── ts_quality.h               # OPC quality, exception filtering, interpolation
├── src/
│   ├── ts_deadband.c              # Deadband implementation (6 modes)
│   ├── ts_swinging_door.c         # Swinging door with step/compmax logic
│   ├── ts_delta_encoding.c        # Gorilla-style encoding implementation
│   ├── ts_piecewise_linear.c      # PLA algorithms (SW, TD, BU, SWAB, MDL)
│   ├── ts_transform.c             # FFT, DCT, windowing, thresholding
│   ├── ts_entropy.c               # Huffman tree, RLE encoder
│   ├── ts_quality.c               # Interpolation, quality KPIs
│   └── ts_compression_model.lean  # Lean 4 formalization (10 theorems)
├── tests/
│   └── test_ts_algorithms.c       # 20 assert-based tests
├── examples/
│   ├── example_deadband.c         # Deadband epsilon sweep
│   ├── example_swinging_door.c    # Swinging door on process signal
│   ├── example_piecewise_linear.c # 4-way PLA comparison
│   └── example_fft_compress.c     # DFT/DCT with window comparison
└── docs/
    ├── knowledge-graph.md         # L1-L9 knowledge mapping
    ├── coverage-report.md         # Coverage assessment
    ├── gap-report.md              # Knowledge gaps and priorities
    ├── course-alignment.md        # 9-school curriculum mapping
    └── course-tree.md             # Prerequisite dependency tree
```

## Build

```bash
make          # Build static library libtscompress.a
make test     # Build and run 20-test suite
make examples # Build 4 example programs
make lean     # Type-check Lean 4 formalization
make check    # Build all + run tests
```

## References

- Pelkonen, T. et al. (2015). "Gorilla: A Fast, Scalable, In-Memory Time Series Database." VLDB 2015.
- Bristol, J. (1987). "Swinging Door Trending." U.S. Patent 4,669,097.
- Shannon, C.E. (1948). "A Mathematical Theory of Communication." BSTJ 27:379-423.
- Huffman, D.A. (1952). "A Method for the Construction of Minimum-Redundancy Codes." Proc. IRE 40(9):1098-1101.
- Cooley, J.W. & Tukey, J.W. (1965). "An Algorithm for the Machine Calculation of Complex Fourier Series." Math. Comp. 19:297-301.
- Ahmed, N., Natarajan, T. & Rao, K.R. (1974). "Discrete Cosine Transform." IEEE Trans. Computers C-23(1):90-93.
- Donoho, D.L. & Johnstone, I.M. (1994). "Ideal Spatial Adaptation by Wavelet Shrinkage." Biometrika 81(3):425-455.
- Keogh, E. et al. (2001). "An Online Algorithm for Segmenting Time Series." ICDM 2001.
- Rissanen, J. (1978). "Modeling by Shortest Data Description." Automatica 14:465-471.
- Rousseeuw, P.J. & Croux, C. (1993). "Alternatives to the Median Absolute Deviation." JASA 88(424):1273-1283.
- Anh, V.N. & Moffat, A. (2010). "Index Compression Using 64-Bit Words." Software: Practice and Experience 40(2):131-147.
- OSIsoft PI Server Reference Guide, Chapter 4: Compression.
- OPC UA Part 8: Data Access, §6.4 Quality.
- IEC 62541: OPC Unified Architecture.

---
**COMPLETE** | Lines: include/ + src/ = 5783 | L1-L6 Complete | L7 Complete | L8 Partial+ | L9 Partial
