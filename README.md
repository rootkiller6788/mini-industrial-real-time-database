# Mini Industrial Real-Time Database

A collection of **from-scratch, zero-dependency C implementations** of industrial real-time database and historian components, modeled after the AVEVA/OSIsoft PI System architecture. Each module maps to university-level courses in databases, distributed systems, compression, and industrial automation, bridging theory and practice by translating industrial data infrastructure concepts into runnable C code.

## Sub-Modules

| Sub-Module | Topics | Key Courses |
|--------|--------|-------------|
| [mini-historian-data-retrieval-sql](mini-historian-data-retrieval-sql/) | Time-series aggregates, SQL retrieval, interpolation, window functions, deadband compression | MIT 6.830, CMU 15-721 |
| [mini-pi-asset-analytics-af-analytics](mini-pi-asset-analytics-af-analytics/) | AF Analytics engine, event frame triggers, expression parser (lexer/AST), KPI rollup, asset hierarchy, EMA/Holt-Winters | MIT 15.071, MIT 6.035 |
| [mini-pi-asset-framework-af](mini-pi-asset-framework-af/) | AFElement, AFAttribute, AFTemplate, data reference pipeline, enumeration sets, search API | MIT 6.830, ISA-95 |
| [mini-pi-event-frames-notifications](mini-pi-event-frames-notifications/) | Event frame time-indexed storage, correlation analysis, trigger evaluation engine, event templates, notification delivery | MIT 6.824, Stanford CS347 |
| [mini-pi-integrator-opcua-mqtt](mini-pi-integrator-opcua-mqtt/) | PI Integrator pipeline/scheduler/health monitor, OPC UA bridge, MQTT data streaming, data model | MIT 6.824, Stanford CS244E |
| [mini-pi-system-osisoft-architecture](mini-pi-system-osisoft-architecture/) | PI Archive, Snapshot subsystem, Point Database, Buffer, Collective HA, Security model, System management | MIT 6.824, MIT 6.033 |
| [mini-pi-vision-display-dashboard](mini-pi-vision-display-dashboard/) | Dashboard grid layout engine, display object model, ISA-101 HMI compliance, rendering pipeline, symbol library, trend visualization | MIT 6.813, Stanford CS147 |
| [mini-time-series-compression-algorithms](mini-time-series-compression-algorithms/) | Deadband, delta-of-delta/Gorilla encoding, Huffman/RLE/Arithmetic entropy coding, piecewise linear approximation, swinging door, DFT/DCT/Wavelet | MIT 6.441, Stanford EE376A |

## Design Philosophy

- **Zero external dependencies** — pure C (C99/C11), only `libc` and `libm`
- **Self-contained modules** — each directory has its own `Makefile`, `include/`, `src/`, `examples/`, `demos/`, `tests/`
- **Industrial-grade patterns** — every module mirrors real AVEVA/OSIsoft PI System components and architecture
- **Theory-to-code mapping** — modules include `docs/` with architecture notes, algorithm references, and industry standards (ISA-95, ISA-101, ISO 22400)

## Building

Each module is standalone. Navigate to a module directory and run:

```bash
cd mini-historian-data-retrieval-sql
make all    # build everything
make test   # run tests
```

Requires **GCC** and **GNU Make**.

## Project Structure

```
mini-industrial-real-time-database/
├── mini-historian-data-retrieval-sql/           # Time-series aggregates, SQL retrieval, interpolation, compression
├── mini-pi-asset-analytics-af-analytics/        # AF Analytics engine, expression parser, KPI rollup
├── mini-pi-asset-framework-af/                  # AFElement, AFTemplate, data reference pipeline
├── mini-pi-event-frames-notifications/          # Event frame storage, correlation, notification delivery
├── mini-pi-integrator-opcua-mqtt/               # Integrator pipeline, OPC UA bridge, MQTT streaming
├── mini-pi-system-osisoft-architecture/         # PI Archive, Snapshot, Point DB, Buffer, HA, Security
├── mini-pi-vision-display-dashboard/            # Dashboard layout, ISA-101 HMI, rendering, trends
└── mini-time-series-compression-algorithms/     # Deadband, delta encoding, entropy coding, PLA, transform
```

## License

MIT
