# Gap Report — Time-Series Compression Algorithms

## Identified Gaps

### L8 Advanced Topics (Partial)
- **Adaptive deadband** — Implemented (EMA-based variance estimation). Could add Kalman-filter-based noise estimation.
- **SWAB hybrid PLA** — Implemented. Could add online streaming variant with bounded memory.
- **MDL optimization** — Implemented. Tolerance sweep is O(K*log(tol_range)) — could use dynamic programming for exact optimal.
- **MAD auto-tuning** — Implemented. Could add Grubbs outlier test for robust noise estimation.
- **VisuShrink** — Implemented. Could add SureShrink (Stein's Unbiased Risk Estimate) adaptive threshold.
- **LZ77/LZ78 on deltas** — Not implemented. Could add dictionary-based compression on delta streams.
- **Sparse FFT** — Not implemented. MIT algorithm (Hassanieh et al., 2012) for sub-Nyquist frequency recovery.

### L9 Research Frontiers (Partial — documented only)
- ML-based compression parameter optimization (reinforcement learning)
- Edge AI for real-time adaptive compression in IIoT gateways
- Digital twin data reduction (only archive deviations from model)
- Quantum-inspired compression algorithms
- Federated compression for distributed historian systems

## Priority for Next Iteration
1. **High**: Sparse FFT for vibration data compression
2. **Medium**: ML-based parameter auto-tuning
3. **Low**: LZ dictionary compression on delta streams
