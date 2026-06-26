# Course Tree — PI Asset Framework Analytics Prerequisites

## Dependency Graph

```
                    ┌──────────────────────────────┐
                    │  PI AF Analytics (this module) │
                    └──────────────┬───────────────┘
                                   │
          ┌────────────────────────┼────────────────────────┐
          │                        │                        │
          ▼                        ▼                        ▼
┌─────────────────┐   ┌─────────────────────┐   ┌──────────────────┐
│  Expression      │   │  Time-Series         │   │  Asset Hierarchy │
│  Engine          │   │  Analytics           │   │  & Rollup        │
└────────┬────────┘   └──────────┬──────────┘   └────────┬─────────┘
         │                       │                        │
         ▼                       ▼                        ▼
┌─────────────────┐   ┌─────────────────────┐   ┌──────────────────┐
│ Compiler Theory  │   │ Statistics &         │   │ Data Structures  │
│ (Lexer, Parser,  │   │ Signal Processing    │   │ (Trees, Graphs,  │
│  AST, Evaluation)│   │ (Variance, EMA,      │   │  DFS, BFS, LCA)  │
│                  │   │  Holt-Winters,       │   │                  │
│                  │   │  CUSUM, Regression)  │   │                  │
└────────┬────────┘   └──────────┬──────────┘   └────────┬─────────┘
         │                       │                        │
         ▼                       ▼                        ▼
┌─────────────────┐   ┌─────────────────────┐   ┌──────────────────┐
│ CS Fundamentals │   │ Calculus & Linear    │   │ Discrete Math    │
│ (DFA, Stack,     │   │ Algebra              │   │ (Graph Theory,   │
│  Recursion)      │   │ (Integrals, OLS,     │   │  Topological     │
│                  │   │  Time Series)        │   │  Sort)           │
└─────────────────┘   └─────────────────────┘   └──────────────────┘
```

## Module-Internal Dependencies

```
pi_af_analytics_core.h/.c        ← Foundation: types, scheduling, topology
    ↑
    ├── pi_af_analytics_expression.h/.c   ← Uses: pi_af_error_t, pi_af_datapoint_t
    ├── pi_af_analytics_timeseries.h/.c   ← Uses: pi_af_datapoint_t, pi_af_error_t
    ├── pi_af_analytics_kpi.h/.c          ← Uses: pi_af_datapoint_t (trend), pi_af_error_t
    ├── pi_af_analytics_eventframe.h/.c   ← Uses: pi_af_error_t, pi_af_analytic_t
    └── pi_af_analytics_rollup.h/.c       ← Uses: pi_af_error_t
```

## External Prerequisites

| Prerequisite Module | Required Concepts |
|---------------------|-------------------|
| mini-pi-system-osisoft-architecture | PI System components (Data Archive, AF, Analytics, Notifications) |
| mini-pi-asset-framework-af | AF elements, attributes, templates, hierarchy |
| mini-time-series-compression-algorithms | Understanding of time-series data structure |
| mini-industrial-communication-protocol | OPC UA concepts (data quality, timestamps) |
| mini-advanced-process-control-apc | Statistical process control (CUSUM, trend analysis) |
