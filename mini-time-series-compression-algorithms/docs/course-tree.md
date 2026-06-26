# Course Tree — Time-Series Compression Algorithms

## Prerequisite Dependency Tree

```
Time-Series Compression Algorithms
├── Fundamentals
│   ├── IEEE 754 Floating Point (bit representation, XOR)
│   ├── Information Theory (entropy, source coding)
│   │   └── Shannon (1948), Huffman (1952)
│   ├── Digital Signal Processing
│   │   ├── Sampling Theorem (Nyquist-Shannon)
│   │   ├── DFT / FFT (Cooley-Tukey, 1965)
│   │   ├── DCT (Ahmed et al., 1974)
│   │   └── Window Functions (Hann, Hamming, Blackman)
│   └── Data Structures
│       ├── Min-Heap (Huffman tree construction)
│       ├── Bit Buffer (variable-length encoding)
│       └── Sorted Arrays (binary search for interpolation)
├── Industrial Context
│   ├── OPC UA Data Model (quality flags, timestamps)
│   ├── OSIsoft PI Architecture
│   │   └── Bristol Swinging Door (US 4,669,097, 1987)
│   ├── SCADA / Historian Systems
│   └── ISA-88 / ISA-101 Standards
├── Compression Algorithms
│   ├── Deadband Filtering (this module)
│   ├── Swinging Door (this module)
│   ├── Delta/DoD/XOR Encoding (this module)
│   ├── PLA Segmentation (this module)
│   ├── Transform Compression (this module)
│   └── Entropy Coding (this module)
└── Advanced Topics
    ├── MDL Principle (Rissanen, 1978)
    ├── Robust Statistics (MAD, Rousseeuw & Croux, 1993)
    ├── Wavelet Thresholding (Donoho & Johnstone, 1994)
    └── Sparse FFT (Hassanieh et al., 2012)

## Downstream Dependencies
- mini-historian-data-retrieval-sql (stores compressed data)
- mini-pi-system-osisoft-architecture (uses swinging door)
- mini-advanced-process-control-apc (uses compressed trends)
- mini-soft-sensor-inferential (model inputs from compressed data)
- mini-industrial-ai-control-fusion (ML on compressed features)
