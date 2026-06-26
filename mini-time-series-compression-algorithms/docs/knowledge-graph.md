# Knowledge Graph — Time-Series Compression Algorithms

## L1: Definitions
- Deadband (Absolute, Percent, Time, Rate-of-Change, Adaptive)
- Swinging Door / Boxcar / Back-Slope compression
- Compression Deviation (compdev), Compression Maximum (compmax)
- Delta encoding, Delta-of-Delta (DoD)
- XOR float compression (Gorilla)
- ZigZag encoding (signed-to-unsigned mapping)
- Varint (LEB128 variable-length integer)
- Simple-8b packed integer format
- Piecewise Linear Approximation (PLA) segment
- PLA algorithms: Sliding Window, Top-Down (Douglas-Peucker), Bottom-Up, SWAB
- DFT (Discrete Fourier Transform), FFT (Cooley-Tukey)
- DCT (Discrete Cosine Transform, DCT-II)
- Window functions: Rectangular, Hann, Hamming, Blackman
- Soft/hard thresholding, universal threshold (VisuShrink)
- Huffman coding, prefix code
- Run-Length Encoding (RLE)
- Shannon entropy (empirical)
- OPC UA quality flags (GOOD/UNCERTAIN/BAD)
- Compression ratio, RMSE, PSNR

## L2: Core Concepts
- Deadband filtering: archive-to-incoming comparison
- Swinging door parallelogram geometry
- Delta-of-delta timestamp compression (Gorilla §3.1)
- XOR float value compression (Gorilla §3.2)
- PLA error tolerance and segment optimization
- Transform-domain sparsity for compression
- Entropy as theoretical compression limit
- Quality-aware compression and exception filtering

## L3: Engineering Structures
- ts_data_point_t: 24-byte aligned time-value-quality triple
- Compression state machines (deadband, swinging door)
- Bit-level encoding/decoding buffers
- Huffman tree node and heap structures
- PLA segment model with capacity management
- Complex number representation for DFT
- Window coefficient array generation

## L4: Engineering Laws
- Shannon Source Coding Theorem (H <= L < H+1)
- Huffman optimality (minimum expected length prefix code)
- Bristol Swinging Door patent (US 4,669,097)
- Nyquist-Shannon sampling theorem (DFT context)
- Cooley-Tukey FFT (O(N log N) vs O(N^2))
- DCT energy compaction property (Ahmed et al., 1974)
- Donoho-Johnstone thresholding (VisuShrink, 1994)
- Rissanen MDL principle (1978)
- OPC UA Part 8 quality model
- IEEE 754 float64 bit-level properties

## L5: Algorithms/Methods
- Deadband decision (absolute, percent, time, combined, rate, adaptive)
- Swinging door filter (Bristol, 1987)
- Delta-of-delta timestamp encoding (Gorilla, 2015)
- XOR float value encoding (Gorilla, 2015)
- Varint/LEB128 encoding
- Simple-8b word-aligned packing
- PLA: Sliding Window, Top-Down, Bottom-Up, SWAB
- Cooley-Tukey radix-2 FFT
- DCT-II (direct formulation)
- Hard/soft thresholding
- Universal threshold (MAD-based sigma estimation)
- Huffman tree construction (min-heap)
- RLE with zero-threshold detection
- MDL optimal segment count

## L6: Canonical Problems
- Compress noisy industrial sensor signal with deadband
- Compress process variable with step changes via swinging door
- PLA segmentation of trend data (4 algorithms compared)
- FFT-based vibration signal compression
- DCT-based compression with coefficient thresholding
- Optimal segment count via MDL

## L7: Industrial Applications
- OSIsoft PI swinging door compression (Bristol patent)
- Honeywell PHD exception/compression filtering
- Facebook Gorilla time-series database (VLDB 2015)
- OPC UA quality-based data filtering
- SCADA historian data reduction

## L8: Advanced Topics
- Adaptive deadband (EMA-based variance estimation)
- SWAB hybrid PLA (Keogh et al., 2001)
- MDL-based optimal segmentation
- MAD-based robust noise estimation for auto-tuning
- Universal threshold (VisuShrink) for coefficient denoising
- Frequency-domain compression with overlapping windows

## L9: Industry Frontiers
- Machine learning for compression parameter optimization
- Edge-based real-time compression for IIoT
- Digital twin data reduction strategies
- IT/OT convergence in historian systems
