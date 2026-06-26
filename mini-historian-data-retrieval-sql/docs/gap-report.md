# Gap Report — mini-historian-data-retrieval-sql

## Missing Items

### L8 Advanced Topics (not yet implemented)
1. **Wavelet-based compression** — Only swinging door/deadband/boxcar implemented. Wavelet (DWT) compression for high-frequency vibration data.
2. **Bayesian change-point detection** — For automatic event detection in time series.
3. **Stream processing integration** — Kafka Streams / Apache Flink historian connector.
4. **Time-series forecasting** — ARIMA / LSTM integration with historian data.
5. **Multi-resolution storage** — Automatic rollup from raw (1s) to hourly to daily aggregates.

### L9 Research Frontiers (not implemented)
1. **Edge historian with SQLite** — Architectural design, not implemented.
2. **Industrial 5G data ingestion pipeline** — Not covered.
3. **Autonomous operations (L4) historian requirements** — Not covered.
4. **Blockchain-anchored data integrity** — Not covered.

## Priority
1. **HIGH**: Wavelet compression for vibration/spectral data.
2. **MEDIUM**: Change-point detection for automatic event frame generation.
3. **LOW**: Edge historian, 5G integration (L9 frontiers).
