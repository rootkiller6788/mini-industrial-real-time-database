# Course Alignment — mini-historian-data-retrieval-sql

| School | Course | Topic Mapping |
|--------|--------|---------------|
| **MIT** | 6.302 Feedback Systems | Data acquisition, signal reconstruction from samples |
| **Stanford** | EE392 Industrial AI | Time-series data infrastructure, feature extraction |
| **Berkeley** | ME233 Advanced Control | Process data logging, system identification data preparation |
| **CMU** | 24-677 Adv Ctrl Systems | Real-time databases for control systems |
| **Georgia Tech** | AE 6530 Optimal Estimation | Data interpolation for Kalman filter initialization |
| **Purdue** | ME 575 Industrial Control | Process historian configuration and retrieval |
| **RWTH Aachen** | SCADA Engineering | Historian as SCADA component, PHD/PI architecture |
| **Tsinghua** | Process Control Engineering | Industrial IoT time-series databases |
| **ISA/IEC** | ISA-88, ISA-95, OPC HDA, ISO 22400-2 | International standards direct alignment |

## Key Standard Alignments

| Standard | Section | Implementation |
|----------|---------|---------------|
| OPC HDA 1.20 | Section 5.3 Quality | historian_quality_t |
| OPC HDA 1.20 | Section 6 ReadRaw | historian_retrieve_raw() |
| OPC HDA 1.20 | Section 7 ReadProcessed | historian_retrieve_aggregated() |
| ISA-88 Part 1 | Batch context fields | historian_tag_metadata_t.batch_id |
| ISO 22400-2:2014 | KPI time buckets | historian_bucket_period_t |
| ANSI SQL-92 | SELECT syntax | SQL generator functions |
| ISO 8601-1:2019 | Timestamp format | historian_timestamp_to_iso8601() |
