# Knowledge Graph — mini-historian-data-retrieval-sql

## L1: Definitions — Complete

| 条目 | 类型 | 位置 |
|------|------|------|
| historian_tag_metadata_t | struct | include/historian_model.h:83 |
| historian_timestamp_t | struct | include/historian_model.h:108 |
| historian_data_point_t | struct | include/historian_model.h:154 |
| historian_result_set_t | struct | include/historian_model.h:167 |
| historian_time_range_t | struct | include/historian_model.h:186 |
| historian_snapshot_t | struct | include/historian_model.h:197 |
| historian_archive_meta_t | struct | include/historian_model.h:207 |
| historian_distribution_stats_t | struct | include/historian_model.h:220 |
| historian_query_spec_t | struct | include/historical_retrieval.h:73 |
| historian_dtype_t | enum | include/historian_model.h:35 |
| historian_compression_flag_t | enum | include/historian_model.h:44 |
| historian_scan_class_t | enum | include/historian_model.h:50 |
| historian_query_mode_t | enum | include/historical_retrieval.h:33 |
| historian_sql_dialect_t | enum | include/historical_retrieval.h:46 |
| historian_boundary_mode_t | enum | include/historian_model.h:181 |
| historian_aggregate_type_t | enum | include/historian_aggregate.h:30 |
| historian_bucket_period_t | enum | include/historian_aggregate.h:66 |
| historian_compression_method_t | enum | include/historian_compression.h:40 |
| historian_interp_method_t | enum | include/historian_interpolation.h:27 |
| historian_window_type_t | enum | include/historian_windowing.h:36 |
| historian_quality_t | typedef + macros | include/historian_model.h:118-137 |

## L2: Core Concepts — Complete

| 概念 | 实现 | 位置 |
|------|------|------|
| Raw data retrieval | historian_retrieve_raw() | src/historian_retrieval.c |
| Interpolated retrieval | historian_retrieve_interpolated() | src/historian_retrieval.c |
| Aggregated retrieval | historian_retrieve_aggregated() | src/historian_retrieval.c |
| Snapshot retrieval | historian_retrieve_snapshot() | src/historian_retrieval.c |
| Multi-tag retrieval | historian_retrieve_multi_tag() | src/historian_retrieval.c |
| Tumbling window | historian_window_tumbling() | src/historian_windowing.c |
| Sliding window | historian_window_sliding() | src/historian_windowing.c |
| Session window | historian_window_session() | src/historian_windowing.c |
| Calendar window | historian_window_calendar() | src/historian_windowing.c |
| Step interpolation | historian_interp_step() | src/historian_interpolation.c |
| Linear interpolation | historian_interp_linear() | src/historian_interpolation.c |
| Time-weighted averaging | historian_compute_time_weighted_avg() | src/historian_aggregate.c |
| Compression ratio | historian_compression_ratio() | src/historian_compression.c |
| Archive reconstruction | historian_reconstruct_value() | src/historian_compression.c |

## L3: Engineering Structures — Complete

| 结构 | 实现 | 位置 |
|------|------|------|
| SQL AST construction | historian_generate_sql() | src/historian_retrieval.c |
| Parameterized queries | historian_generate_parameterized_sql() | src/historian_retrieval.c |
| Dialect-specific SQL (PI/PHD/Standard) | 3 generator functions | src/historian_retrieval.c |
| Dynamic result set | historian_result_set_*() | src/historian_model.c |
| Tridiagonal system solver | Thomas algorithm | src/historian_interpolation.c |
| Narrow-table schema representation | historian_data_point_t | include/historian_model.h |
| Wide-table mapping via multi-tag | historian_query_spec_t.tag_ids | include/historical_retrieval.h |

## L4: Engineering Laws — Complete

| 法则/标准 | 实现 | 验证 |
|-----------|------|------|
| OPC HDA Quality semantics | Quality bit operations | 	est_quality_flags |
| ISO 8601 timestamp | historian_timestamp_from/to_iso8601() | 	est_timestamp_iso8601 |
| ISO 22400-2 KPI buckets | historian_bucket_spec_t + alignment | 	est_calendar_window |
| ANSI SQL-92 compliance | SQL generator functions | 	est_sql_generation |
| SQL injection prevention | Parameterized SQL | 	est_parameterized_sql |
| ISA-88 batch context | 	ag_metadata.batch_id field | include/historian_model.h |
| Timestamp transitivity | Formal proof | historian_schema.lean:36 |
| Timestamp antisymmetry | Formal proof | historian_schema.lean:43 |

## L5: Algorithms — Complete

| 算法 | 文件 | 参考文献 |
|------|------|---------|
| Swinging door compression | historian_compression.c | Bristol (1990) ISA Trans. |
| Deadband compression | historian_compression.c | Standard technique |
| Boxcar compression | historian_compression.c | Standard technique |
| Arithmetic average | historian_aggregate.c | — |
| Time-weighted average | historian_aggregate.c | OSIsoft PI PE Ref. |
| Welford online stddev | historian_aggregate.c | Welford (1962) Technometrics |
| Percentile/median | historian_aggregate.c | Linear interpolation formula |
| Linear interpolation | historian_interpolation.c | Two-point formula |
| Quadratic interpolation | historian_interpolation.c | Newton divided differences |
| Natural cubic spline | historian_interpolation.c | de Boor (1978) |
| Akima spline | historian_interpolation.c | Akima (1970) J.ACM |
| Catmull-Rom spline | historian_interpolation.c | Cardinal spline |
| Lagrange 3rd-order | historian_interpolation.c | Lagrange basis |
| Tumbling window | historian_windowing.c | Fixed-size contiguous |
| Sliding window | historian_windowing.c | Overlapping window |
| Session window | historian_windowing.c | Gap-based grouping |
| Gap detection | historian_windowing.c | Inter-point gap analysis |
| ROC detection | historian_windowing.c | Derivative threshold |
| SQL LAG/LEAD | historian_windowing.c | SQL:2003 window functions |
| Running aggregate | historian_aggregate.c | Welford incremental |

## L6: Canonical Problems — Complete

| 问题 | 示例 | 描述 |
|------|------|------|
| Raw data query | examples/example_raw_query.c | SQL generation + instrument scaling |
| Time-bucketed aggregation | examples/example_aggregation.c | 24h flow → hourly averages |
| Compression analysis | examples/example_compression.c | Swinging door vs deadband |
| Value reconstruction | Test 	est_reconstruct_value | Step/linear from compressed data |
| Data gap detection | Test 	est_gap_detection | Communication loss intervals |
| Window partitioning | Tests 	est_tumbling/sliding/session/calendar_window | All window types |

## L7: Industrial Applications — Complete

| 应用 | 实现 |
|------|------|
| OSIsoft PI OLEDB Enterprise | SQL dialect, table references, generate_pi_oledb_sql() |
| Honeywell PHD SQL | SQL dialect, table references, generate_phd_sql() |
| AspenTech SQLplus | SQL dialect (mapped to standard) |
| PI TimeAvg equivalent | historian_compute_time_weighted_avg() |
| PI Performance Equation | Aggregate functions mapping |

## L8: Advanced Topics — Partial

| 主题 | 状态 | 位置 |
|------|------|------|
| Distribution statistics (skewness, kurtosis) | Implemented | src/historian_aggregate.c |
| Percentile computation (P10, P90) | Implemented | src/historian_aggregate.c |
| Session windows (ISA-88 batch) | Implemented | src/historian_windowing.c |
| Rate-of-change events | Implemented | src/historian_windowing.c |
| Query cost estimation | Implemented | src/historian_retrieval.c |
| Stream-table duality | Documented | include/historian_windowing.h |

## L9: Research Frontiers — Partial

| 主题 | 状态 |
|------|------|
| Edge historian with SQLite | Documented |
| Cloud historian query patterns | Documented |
| Industrial 5G data ingestion | Not covered |
| Autonomous operations (L4) historian | Not covered |
