# Knowledge Graph — PI Asset Framework (AF) Analytics

## L1 — Definitions (Complete ✅)

| # | Definition | C Type / Enum | Reference |
|---|-----------|---------------|-----------|
| 1 | AF Analytic | `pi_af_analytic_t` | OSIsoft PI AF SDK |
| 2 | Scheduling Types | `pi_af_schedule_type_t` (Periodic/Event/Natural/OnDemand) | PI AF Analytics Service |
| 3 | Calculation Status | `pi_af_calc_status_t` (Idle→Queued→Running→Complete/Error) | PI AF SDK |
| 4 | Output Destination | `pi_af_output_type_t` (PI Point/AF Attribute/Table) | PI System Architecture |
| 5 | Timestamp Mapping | `pi_af_ts_mapping_t` (Source/CalcTime/Min/Max/IntervalEnd) | OPC UA Part 11 §6.4 |
| 6 | Backfilling Policy | `pi_af_backfill_policy_t` | PI AF Analytics Config |
| 7 | Data Quality | `pi_af_data_quality_t` (Good/Questionable/Bad/Unknown) | OPC UA Part 8 §5.3 |
| 8 | Expression Token | `pi_af_token_t` (Number/String/Attribute/Function/Operator) | PI Performance Equation |
| 9 | AST Node | `pi_af_ast_node_t` (Literal/Attribute/BinOp/UnaryOp/FuncCall/IfExpr) | Compiler Theory |
| 10 | Aggregate Types | `pi_af_aggregate_t` (Sum/Avg/Min/Max/StdDev/Median/Delta/PercentGood) | OPC UA Part 11 §B.1 |
| 11 | Time Window | `pi_af_time_window_t` (Absolute/Relative/WideOpen/Count) | PI AF Analytics |
| 12 | Interpolation | `pi_af_interp_method_t` (None/Step/Linear/Spline) | PI Data Archive |
| 13 | KPI Definition | `pi_af_kpi_t` (Target/Thresholds/Direction/Score) | ISO 22400-2:2014 |
| 14 | KPI Status | `pi_af_kpi_status_t` (Good/Warning/Critical/Unknown) | ISA-95 Dashboard |
| 15 | Event Frame | `pi_af_ef_instance_t` (Idle/Active/Closed/Canceled/Acked) | OSIsoft PI AF |
| 16 | Event Trigger | `pi_af_ef_trigger_t` (Expression/Threshold/Digital/Schedule/Manual) | PI AF |
| 17 | Asset Node | `pi_af_asset_node_t` (ISA-95 Categories) | ISA-95 Part 2 §4.1 |
| 18 | Rollup Methods | `pi_af_rollup_method_t` (Sum/Avg/Min/Max/Weighted/Formula/Count/Worst) | PI AF SDK |
| 19 | Error Codes | `pi_af_error_t` (15 distinct error codes) | PI AF SDK HRESULT |

## L2 — Core Concepts (Complete ✅)

| # | Concept | Implementation | Institution |
|---|---------|----------------|-------------|
| 1 | Asset-Centric Analytics | `pi_af_asset_node_t` hierarchy + rollup | Stanford ENGR205 |
| 2 | Expression Evaluation | Tokenize → Parse → AST → Evaluate pipeline | MIT 6.035 |
| 3 | Schedule-Driven Execution | Priority queue min-heap | CMU 24-677 |
| 4 | Event-Triggered Analytics | Event frame → trigger → analytic | MIT 6.302 |
| 5 | Data Quality Propagation | Percent Good, quality-weighted stats | RWTH Aachen |
| 6 | KPI Hierarchical Rollup | Bottom-up post-order tree aggregation | Purdue ME 575 |
| 7 | Short-Circuit Evaluation | && and || logical operators | Stanford CS143 |
| 8 | Time-Weighted Averaging | Trapezoidal integration over intervals | OPC UA Part 11 |
| 9 | Natural Scheduling | Input-change-driven recalculation | OSIsoft PI AF |
| 10 | Dependency Resolution | Topological sort (Kahn's algorithm) | CMU 24-677 |
| 11 | Backfilling | Historical recalculation policies | OSIsoft PI |
| 12 | Interpolation | Linear, step, spline for gap filling | Berkeley ME233 |

## L3 — Engineering Structures (Complete ✅)

| # | Structure | Data Type | Algorithm |
|---|-----------|-----------|-----------|
| 1 | Priority Queue (Min-Heap) | `pi_af_schedule_entry_t[]` | heap_sift_up / heap_sift_down |
| 2 | Abstract Syntax Tree | `pi_af_ast_node_t` pool | Recursive descent parser |
| 3 | Sliding Window Buffer | `pi_af_sliding_window_t` | Ring buffer (circular) |
| 4 | Event Frame Manager | `pi_af_ef_context_t` | Linked list of active EFs |
| 5 | Asset Hierarchy Tree | `pi_af_asset_context_t` | Parent-child linked nodes |
| 6 | KPI Engine | `pi_af_kpi_context_t` | Indexed array + calc order |
| 7 | Dependency Graph | `dependency_graph` adjacency matrix | Kahn's topo sort |
| 8 | Function Registry | `g_function_table[]` | Static lookup table |
| 9 | Lexer DFA | `pi_af_expression_tokenize()` | DFA-based scanner |
| 10 | Holt-Winters State | `pi_af_holt_winters_state_t` | Triple smoothing buffer |

## L4 — Engineering Laws & Standards (Complete ✅)

| # | Standard/Law | Where Applied | Reference |
|---|-------------|---------------|-----------|
| 1 | ISA-95 Equipment Hierarchy | Asset categories (Enterprise→Sensor) | ISA-95 Part 2 §4.1 |
| 2 | ISO 22400-2 KPI Definitions | KPI evaluation semantics | ISO 22400-2:2014 |
| 3 | OPC UA Part 11 Aggregates | Time-weighted avg, aggregate types | OPC UA Part 11 §B.1 |
| 4 | IEEE 754 Floating-Point | All double computations, divide-by-zero checks | IEEE 754-2008 |
| 5 | ISA-88 Batch Models | Event frame ↔ phase boundaries | ISA-88 Part 1 |
| 6 | OPC UA Data Quality | Good/Questionable/Bad/Unknown | OPC UA Part 8 §5.3 |
| 7 | Dijkstra Shunting-Yard | Expression parsing operator precedence | Dijkstra (1961) |
| 8 | Welford's Algorithm | Numerically stable variance | Welford (1962) |
| 9 | Page's CUSUM | Cumulative sum change detection | Page (1954) |
| 10 | Holt-Winters Smoothing | Triple exponential smoothing | Holt (1957), Winters (1960) |

## L5 — Algorithms/Methods (Complete ✅)

| # | Algorithm | Function | Complexity | Source |
|---|-----------|----------|------------|--------|
| 1 | Min-Heap Scheduling | `heap_sift_up/down` | O(log n) | CLRS §6 |
| 2 | Kahn's Topological Sort | `pi_af_build_execution_order()` | O(V+E) | Kahn (1962) |
| 3 | DFA Tokenizer | `pi_af_expression_tokenize()` | O(n) | Aho et al. (1986) |
| 4 | Recursive Descent Parser | `parse_expression()` | O(n) | Aho et al. §4.4 |
| 5 | AST Tree-Walk Eval | `eval_node()` | O(n) | Compiler standard |
| 6 | Welford's Online Variance | `pi_af_window_aggregate()` | O(n) | Welford (1962) |
| 7 | Trapezoidal Integration | `pi_af_time_weighted_average()` | O(n) | OPC UA Part 11 |
| 8 | EMA (Exponential Smoothing) | `pi_af_ema_update()` | O(1)/sample | Hunter (1986) |
| 9 | Holt-Winters Triple ES | `pi_af_holt_winters_update()` | O(1)/sample | Holt-Winters (1960) |
| 10 | OLS Linear Regression | `pi_af_rate_of_change()` | O(n) | Press et al. §15.2 |
| 11 | CUSUM Change Detection | `pi_af_cusum_detect()` | O(n) | Page (1954) |
| 12 | Cycle Detection | `pi_af_detect_cycles()` | O(n) | Signal processing |
| 13 | KPI Status Evaluation | `pi_af_kpi_evaluate()` | O(t), t=thresholds | ISO 22400 |
| 14 | KPI Rollup | `pi_af_kpi_rollup()` | O(c), c=children | ISO 22400 |
| 15 | Composite Scoring | `pi_af_kpi_composite_score()` | O(n) | Saaty AHP (1980) |
| 16 | DFS (Post-Order) | `pi_af_asset_dfs()` | O(n) | CLRS §22.3 |
| 17 | BFS (Level-Order) | `pi_af_asset_bfs()` | O(n) | CLRS §22.2 |
| 18 | LCA (Path Method) | `pi_af_asset_lowest_common_ancestor()` | O(d) | Bender (2000) |
| 19 | Median (Insertion Sort) | `pi_af_window_aggregate(MEDIAN)` | O(n²) small n | CLRS §2.1 |
| 20 | Binary Search Interp | `pi_af_interpolate_step()` | O(log n) | CLRS §12 |
| 21 | Range Intersection | `pi_af_time_range_intersection()` | O(1) | Interval geometry |
| 22 | Ring Buffer Push | `pi_af_sliding_window_push()` | O(1) | Circular buffer |

## L6 — Canonical Problems (Complete ✅)

| # | Problem | Solution | Example |
|---|---------|----------|---------|
| 1 | Pump Efficiency Monitoring | Sliding window + EMA + CUSUM | `example_rolling_analytics.c` |
| 2 | KPI Dashboard | OEE decomposition + composite score | `example_kpi_dashboard.c` |
| 3 | Batch Event Tracking | Event frame lifecycle management | `example_event_triggered.c` |
| 4 | Asset Performance Rollup | Bottom-up tree aggregation | `src/pi_af_analytics_rollup.c` |
| 5 | Expression-Based Calculations | Full parser + evaluator | `src/pi_af_analytics_expression.c` |
| 6 | Schedule Management | Priority queue execution loop | `src/pi_af_analytics_core.c` |
| 7 | Data Quality Assessment | Percent Good calculation | `src/pi_af_analytics_timeseries.c` |
| 8 | Trend Analysis | Linear regression slope detection | `src/pi_af_analytics_kpi.c` |

## L7 — Industrial Applications (Complete ✅)

| # | Application | Tag Reference | Implementation |
|---|------------|---------------|----------------|
| 1 | OSIsoft PI AF SDK Patterns | `pi_af_analytics_core.c` | Analytics service architecture |
| 2 | AVEVA PI System | `pi_af_analytics_expression.c` | Performance Equation syntax |
| 3 | ISA-95 Level 3/4 Analytics | `pi_af_analytics_kpi.c` | Manufacturing KPI rollup |
| 4 | ISA-88 Batch Events | `pi_af_analytics_eventframe.c` | Phase boundary event frames |
| 5 | ISO 22400 OEE Calculation | `example_kpi_dashboard.c` | OEE = A × P × Q |

## L8 — Advanced Topics (Complete ✅)

| # | Topic | Implementation | Reference |
|---|-------|----------------|-----------|
| 1 | Holt-Winters Forecasting | `pi_af_holt_winters_*()` | Winters (1960) |
| 2 | CUSUM Statistical Process Control | `pi_af_cusum_detect()` | Page (1954) |
| 3 | Welford Online Variance | `pi_af_window_aggregate()` | Welford (1962) |
| 4 | Adaptive Thresholds | `pi_af_kpi_evaluate()` hysteresis | ISO 22400 |

## L9 — Industry Frontiers (Partial — Documented)

| # | Topic | Status | Reference |
|---|-------|--------|-----------|
| 1 | IT/OT Convergence Analytics | Documented | OSIsoft PI + MQTT/OPC UA |
| 2 | Predictive Maintenance | CUSUM foundation in place | PI AF + ML integration |
| 3 | Edge Analytics | Structure supports distributed deployment | PI Integrator for BA |
| 4 | Autonomous Operations (L4) | Architecture supports event-triggered response | ISO 22400 |
