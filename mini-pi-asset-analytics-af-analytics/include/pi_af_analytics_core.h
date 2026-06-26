/**
 * @file include/pi_af_analytics_core.h
 * @brief PI Asset Framework Analytics — Core Data Structures & Scheduling Engine
 *
 * Models OSIsoft PI AF Analytics — the industrial real-time calculation engine
 * that executes expressions, KPIs, and rollups against PI AF asset data.
 *
 * Reference Standards:
 *   - OSIsoft PI AF SDK 2.x/3.x — Analytics service architecture
 *   - ISA-95 Part 1 & 2 — Enterprise-Control System Integration (Level 3→4 analytics)
 *   - ISO 22400-2:2014 — Key Performance Indicators for MOM
 *   - OPC UA Part 11 — Historical Access (HA) data retrieval semantics
 *   - IEC 62541 — OPC Unified Architecture
 *
 * Knowledge Coverage: L1 (Definitions), L2 (Core Concepts), L3 (Structures),
 *                     L4 (Standards), L5 (Algorithms)
 *
 * MIT 6.302 — Discrete-event scheduling supervisor pattern
 * Stanford ENGR205 — Industrial KPI calculation architecture
 * CMU 24-677 — Priority-driven calculation ordering
 * RWTH Aachen — PLC/SCADA analytics integration patterns
 */

#ifndef PI_AF_ANALYTICS_CORE_H
#define PI_AF_ANALYTICS_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* --------------------------------------------------------------------------
 * L1 — Core Definitions: PI AF Analytics Domain Model
 * ------------------------------------------------------------------------*/

/** Maximum length for analytic names (PI AF SDK convention) */
#define PI_AF_MAX_ANALYTIC_NAME   256

/** Maximum length for expression strings */
#define PI_AF_MAX_EXPRESSION_LEN  1024

/** Maximum number of analytics that can be registered in one context */
#define PI_AF_MAX_ANALYTICS       64

/** Maximum dependency depth (prevents circular reference infinite loops) */
#define PI_AF_MAX_DEPENDENCY_DEPTH 32

/** Maximum number of event frame triggers per analytic */
#define PI_AF_MAX_TRIGGERS        16

/** Maximum asset hierarchy depth for rollup */
#define PI_AF_MAX_HIERARCHY_DEPTH 64

/**
 * @brief Scheduling type for an analytic calculation.
 *
 * PI AF Analytics supports four scheduling modes:
 * - Periodic: Execute on a fixed interval (e.g., every 5 minutes)
 * - Event-Triggered: Execute when a named event frame starts/ends
 * - Natural: Execute whenever any input attribute value changes
 * - On-Demand: Execute only when explicitly requested via API/SDK
 */
typedef enum {
    PI_AF_SCHEDULE_PERIODIC       = 0,  /**< Time-interval based execution */
    PI_AF_SCHEDULE_EVENT_TRIGGERED = 1,  /**< Event-frame boundary execution */
    PI_AF_SCHEDULE_NATURAL        = 2,  /**< Input-change-driven execution */
    PI_AF_SCHEDULE_ON_DEMAND      = 3,  /**< Manual/API-triggered only */
    PI_AF_SCHEDULE_COUNT
} pi_af_schedule_type_t;

/**
 * @brief Execution status of an analytic calculation.
 *
 * Tracks the lifecycle of a single calculation instance from enqueue
 * through the execution pipeline to final disposition.
 */
typedef enum {
    PI_AF_CALC_STATUS_IDLE         = 0,  /**< Waiting for trigger condition */
    PI_AF_CALC_STATUS_QUEUED       = 1,  /**< Enqueued for execution */
    PI_AF_CALC_STATUS_RUNNING      = 2,  /**< Currently being evaluated */
    PI_AF_CALC_STATUS_COMPLETE     = 3,  /**< Successfully evaluated */
    PI_AF_CALC_STATUS_ERROR        = 4,  /**< Evaluation failed with error */
    PI_AF_CALC_STATUS_SKIPPED      = 5,  /**< Skipped due to dependency failure */
    PI_AF_CALC_STATUS_COUNT
} pi_af_calc_status_t;

/**
 * @brief Output destination types for analytic results.
 *
 * PI AF Analytics can write results to multiple destination types.
 */
typedef enum {
    PI_AF_OUTPUT_PI_POINT     = 0,  /**< PI Data Archive tag */
    PI_AF_OUTPUT_AF_ATTRIBUTE = 1,  /**< AF element attribute */
    PI_AF_OUTPUT_TABLE        = 2,  /**< AF table (relational storage) */
    PI_AF_OUTPUT_NONE         = 3,  /**< Intermediate/internal only */
} pi_af_output_type_t;

/**
 * @brief Timestamp mapping rule for calculated values.
 *
 * Determines how the timestamp for a calculated result is derived
 * from the input data timestamps.
 *
 * @see OPC UA Part 11 §6.4 — Timestamp semantics for processed data
 */
typedef enum {
    PI_AF_TS_MAP_SOURCE      = 0,  /**< Use the source data timestamp */
    PI_AF_TS_MAP_CALC_TIME   = 1,  /**< Use the moment of calculation */
    PI_AF_TS_MAP_MIN_INPUT   = 2,  /**< Minimum of all input timestamps */
    PI_AF_TS_MAP_MAX_INPUT   = 3,  /**< Maximum of all input timestamps */
    PI_AF_TS_MAP_INTERVAL_END = 4, /**< End of the summary interval */
} pi_af_ts_mapping_t;

/**
 * @brief Backfilling policy for historical data recalculation.
 *
 * Controls how the analytics engine handles recalculation of
 * historical time ranges.
 */
typedef enum {
    PI_AF_BACKFILL_NONE      = 0,  /**< No backfilling — forward only */
    PI_AF_BACKFILL_SINGLE    = 1,  /**< Single-pass backfill from specified start */
    PI_AF_BACKFILL_CONTINUOUS = 2, /**< Continuous backfill as new old data arrives */
    PI_AF_BACKFILL_REPROCESS = 3,  /**< Full reprocess — delete and recalculate */
} pi_af_backfill_policy_t;

/**
 * @brief Data quality rule for input data filtering.
 *
 * Zero = bad quality → include in calculation?
 * Good = calculation proceeds normally.
 * Questionable = calculation proceeds with warning.
 * Bad = skip or use substitute value.
 */
typedef enum {
    PI_AF_QUALITY_GOOD         = 0,  /**< Normal, valid data */
    PI_AF_QUALITY_QUESTIONABLE = 1,  /**< Suspect — proceed with caution */
    PI_AF_QUALITY_BAD          = 2,  /**< Invalid — use substitution policy */
    PI_AF_QUALITY_UNKNOWN      = 3,  /**< No quality information available */
} pi_af_data_quality_t;

/* --------------------------------------------------------------------------
 * L2 — Core Concepts: Analytic Definition & Scheduling
 * ------------------------------------------------------------------------*/

/**
 * @brief A single input attribute reference for an analytic.
 *
 * Maps to PI AF attribute references like:
 *   \Server\Database\Element|Attribute
 *   |Attribute  (relative to current element)
 *   ..\..\ParentElement|Attribute  (relative path)
 */
typedef struct {
    char   attribute_path[PI_AF_MAX_ANALYTIC_NAME];  /**< AF path to attribute */
    char   attribute_name[PI_AF_MAX_ANALYTIC_NAME];  /**< Short name for expression */
    double substitute_value;         /**< Value to use when data quality is bad */
    bool   is_required;              /**< If true, bad quality → skip calculation */
} pi_af_input_ref_t;

/**
 * @brief Complete definition of a single AF Analytic.
 *
 * This struct captures all metadata needed by the PI AF Analytics
 * service to schedule, execute, and store results for one analytic.
 *
 * @note PI AF SDK equivalent: AFNamedCollection<AFAnalytic>
 */
typedef struct {
    uint32_t  analytic_id;                              /**< Unique identifier */
    char      name[PI_AF_MAX_ANALYTIC_NAME];            /**< Human-readable name */
    char      description[PI_AF_MAX_ANALYTIC_NAME];     /**< Documentation string */
    char      expression[PI_AF_MAX_EXPRESSION_LEN];     /**< Calculation expression */

    pi_af_schedule_type_t schedule_type;                /**< When to execute */
    double    period_seconds;                           /**< Interval for periodic scheduling */
    char      trigger_event_frame[PI_AF_MAX_ANALYTIC_NAME]; /**< For event-triggered */

    pi_af_output_type_t output_type;                    /**< Where to write results */
    char      output_destination[PI_AF_MAX_ANALYTIC_NAME];  /**< Output path/tag name */

    pi_af_ts_mapping_t    ts_mapping;                   /**< Timestamp rule */
    pi_af_backfill_policy_t backfill;                   /**< Backfilling policy */

    uint32_t  input_count;                              /**< Number of input attributes */
    pi_af_input_ref_t inputs[16];                       /**< Input attribute references */

    uint32_t  dependency_count;                         /**< Upstream analytics this depends on */
    uint32_t  dependency_ids[PI_AF_MAX_DEPENDENCY_DEPTH]; /**< Dependency ID list */

    bool      enabled;                                  /**< Whether this analytic is active */
    time_t    last_execution;                           /**< Timestamp of last run */
    pi_af_calc_status_t last_status;                    /**< Result of last execution */
    char      last_error[512];                          /**< Error message from last run */

    double    last_result;                              /**< Most recent calculated value */
    uint32_t  execution_count;                          /**< Total number of executions */
    uint32_t  error_count;                              /**< Cumulative error count */
} pi_af_analytic_t;

/**
 * @brief Scheduling entry in the priority queue.
 *
 * The scheduler maintains a min-heap of pending calculations
 * ordered by next execution time.
 */
typedef struct {
    uint32_t  analytic_id;       /**< Which analytic to execute */
    time_t    scheduled_time;    /**< When it should be executed */
    time_t    enqueue_time;      /**< When it was placed in queue */
    int32_t   priority;          /**< Higher values = run sooner (for tie-breaking) */
    char      trigger_reason[128]; /**< Why this was scheduled */
} pi_af_schedule_entry_t;

/**
 * @brief A single time-value-quality data point.
 *
 * The fundamental unit of time-series data that flows through
 * the analytics pipeline.
 */
typedef struct {
    time_t   timestamp;          /**< POSIX timestamp */
    double   value;              /**< Numeric value (IEEE 754 double) */
    pi_af_data_quality_t quality; /**< Data quality indicator */
    bool     is_interpolated;    /**< Whether this point was interpolated */
} pi_af_datapoint_t;

/* --------------------------------------------------------------------------
 * L3 — Engineering Structures: Analytics Engine Context
 * ------------------------------------------------------------------------*/

/**
 * @brief Master analytics engine context.
 *
 * Holds all registered analytics, the schedule queue, and runtime
 * configuration. This is the top-level object for the entire
 * PI AF Analytics subsystem.
 *
 * Pattern: Singleton service context (similar to PI AF Server SDK).
 *
 * @see OSIsoft PI AF SDK — PISystem → AFDatabase → AFElement → AFAnalytic
 */
typedef struct {
    /* Registered analytics — indexed by analytic_id */
    pi_af_analytic_t analytics[PI_AF_MAX_ANALYTICS];
    uint32_t         analytics_count;
    bool             analytics_in_use[PI_AF_MAX_ANALYTICS]; /**< Slot occupancy */

    /* Schedule queue — min-heap ordered by scheduled_time */
    pi_af_schedule_entry_t *schedule_queue;
    uint32_t                 queue_capacity;
    uint32_t                 queue_size;

    /* Engine state */
    bool     engine_running;          /**< Whether the engine is active */
    time_t   engine_start_time;       /**< When the engine was started */
    time_t   engine_current_time;     /**< Current virtual time (for simulation) */
    uint64_t total_calculations;      /**< Total calculations performed */
    uint64_t total_errors;            /**< Total errors encountered */
    double   avg_calculation_ms;      /**< Running average calculation duration */

    /* Backfilling state */
    bool     backfill_active;
    time_t   backfill_start;
    time_t   backfill_end;

    /* Output statistics */
    uint64_t points_written;          /**< Total output data points written */
    uint64_t bytes_written;           /**< Total output data volume */

    /* Configuration */
    uint32_t max_concurrent;          /**< Max concurrent calculations */
    double   min_period_seconds;      /**< Minimum allowed period (anti-flood) */
    bool     strict_timing;           /**< Enforce exact schedule timing */
    bool     skip_on_error;           /**< Skip downstream on upstream error */

    /* Dependency tracking */
    uint32_t *dependency_graph;       /**< Adjacency matrix: [from][to] = 1 if from depends on to.
                                           Dynamically allocated as N×N where N = PI_AF_MAX_ANALYTICS */
    uint32_t *execution_order;        /**< Topologically sorted order */
    uint32_t  execution_order_count;
    bool     order_valid;             /**< Whether execution_order is up to date */
} pi_af_analytics_context_t;

/* --------------------------------------------------------------------------
 * L1 — Definition: Error Codes
 * ------------------------------------------------------------------------*/

/** PI AF Analytics error codes (mirrors PI AF SDK HRESULT patterns) */
typedef enum {
    PI_AF_OK                      =  0,   /**< Success */
    PI_AF_ERR_NOT_FOUND           = -1,   /**< Analytic or attribute not found */
    PI_AF_ERR_EXPRESSION_SYNTAX   = -2,   /**< Malformed expression */
    PI_AF_ERR_EXPRESSION_EVAL     = -3,   /**< Expression evaluation failure */
    PI_AF_ERR_DIVIDE_BY_ZERO      = -4,   /**< Division by zero in expression */
    PI_AF_ERR_CIRCULAR_DEPENDENCY = -5,   /**< Circular reference detected */
    PI_AF_ERR_SCHEDULE_FULL       = -6,   /**< Schedule queue capacity exceeded */
    PI_AF_ERR_TIMEOUT             = -7,   /**< Calculation exceeded time limit */
    PI_AF_ERR_DATA_QUALITY        = -8,   /**< Input data quality below threshold */
    PI_AF_ERR_BAD_TIMESTAMP       = -9,   /**< Invalid or out-of-order timestamp */
    PI_AF_ERR_OUTPUT_FAILED       = -10,  /**< Could not write output value */
    PI_AF_ERR_OUT_OF_MEMORY       = -11,  /**< Memory allocation failure */
    PI_AF_ERR_INVALID_ARGUMENT    = -12,  /**< Invalid function argument */
    PI_AF_ERR_OVERFLOW            = -13,  /**< Numeric overflow in calculation */
    PI_AF_ERR_NOT_INITIALIZED     = -14,  /**< Engine not initialized */
    PI_AF_ERR_DUPLICATE_ID        = -15,  /**< Analytic ID already registered */
    PI_AF_ERR_COUNT
} pi_af_error_t;

/* --------------------------------------------------------------------------
 * L5 — Algorithms: Core Engine Functions
 * ------------------------------------------------------------------------*/

/**
 * @brief Initialize the analytics engine context.
 *
 * Allocates and initializes all internal structures. Must be called
 * before any other function.
 *
 * @param ctx         Pointer to uninitialized context struct
 * @param queue_cap   Maximum size of the schedule priority queue
 * @return            PI_AF_OK on success
 *
 * Complexity: O(queue_cap) for allocation
 *
 * @see OSIsoft PI AF SDK — PISystems.Connect()
 */
pi_af_error_t pi_af_init(pi_af_analytics_context_t *ctx, uint32_t queue_cap);

/**
 * @brief Shut down and clean up the analytics engine.
 *
 * Frees all allocated memory and resets state. No further calls
 * are valid after this.
 *
 * @param ctx  Initialized context
 * @return     PI_AF_OK on success
 *
 * Complexity: O(1) for deallocation
 */
pi_af_error_t pi_af_shutdown(pi_af_analytics_context_t *ctx);

/**
 * @brief Register a new analytic in the engine.
 *
 * The analytic is assigned an ID if id==0, otherwise uses the given ID.
 * Expression is validated via the expression parser.
 *
 * @param ctx       Engine context
 * @param analytic  Analytic definition (copied into internal storage)
 * @param out_id    Output: assigned analytic ID
 * @return          PI_AF_OK on success
 *
 * Complexity: O(1) for registration + O(n) for expression validation
 *   where n = expression length
 */
pi_af_error_t pi_af_register_analytic(pi_af_analytics_context_t *ctx,
                                       const pi_af_analytic_t *analytic,
                                       uint32_t *out_id);

/**
 * @brief Unregister and remove an analytic from the engine.
 *
 * Also removes all pending schedule entries for this analytic
 * and invalidates the execution order.
 *
 * @param ctx         Engine context
 * @param analytic_id ID of analytic to remove
 * @return            PI_AF_OK on success
 *
 * Complexity: O(k) where k = queue size (for removing schedule entries)
 */
pi_af_error_t pi_af_unregister_analytic(pi_af_analytics_context_t *ctx,
                                         uint32_t analytic_id);

/**
 * @brief Enable or disable an analytic.
 *
 * Disabled analytics are not scheduled for execution.
 *
 * @param ctx         Engine context
 * @param analytic_id Analytic ID
 * @param enabled     true = activate, false = deactivate
 * @return            PI_AF_OK on success
 */
pi_af_error_t pi_af_set_analytic_enabled(pi_af_analytics_context_t *ctx,
                                          uint32_t analytic_id, bool enabled);

/**
 * @brief Look up an analytic by ID.
 *
 * @param ctx         Engine context
 * @param analytic_id Analytic ID to find
 * @return            Pointer to analytic, or NULL if not found
 *
 * Complexity: O(n) where n = registered analytics count
 */
pi_af_analytic_t *pi_af_get_analytic(pi_af_analytics_context_t *ctx,
                                      uint32_t analytic_id);

/**
 * @brief Schedule an analytic for execution.
 *
 * Adds the analytic to the priority queue based on its scheduling type.
 * For periodic: computes next scheduled_time from last_execution + period.
 * For event-triggered: uses the event timestamp.
 * For natural: marks for immediate execution.
 *
 * @param ctx         Engine context
 * @param analytic_id Analytic to schedule
 * @param trigger_ts  Trigger timestamp (0 = compute from schedule type)
 * @return            PI_AF_OK on success
 *
 * Complexity: O(log n) for heap insertion, n = queue size
 */
pi_af_error_t pi_af_schedule_analytic(pi_af_analytics_context_t *ctx,
                                       uint32_t analytic_id, time_t trigger_ts);

/**
 * @brief Process the next pending calculation from the schedule queue.
 *
 * This is the main execution loop entry point — equivalent to the
 * PI AF Analytics Service worker thread. Pops the next ready entry
 * from the priority queue, evaluates its expression, stores the result,
 * and potentially schedules dependent analytics.
 *
 * @param ctx  Engine context
 * @return     PI_AF_OK if a calculation was processed,
 *             0 if no pending entries, negative on error
 *
 * Complexity: O(log n) for heap pop + O(e) for expression evaluation
 *   where n = queue size, e = expression length
 *
 * @see MIT 6.302 — Discrete-event simulation loop
 */
pi_af_error_t pi_af_process_next(pi_af_analytics_context_t *ctx);

/**
 * @brief Run the engine for a specified number of calculations or time window.
 *
 * Convenience function that repeatedly calls pi_af_process_next().
 *
 * @param ctx           Engine context
 * @param max_calcs     Maximum calculations to process (0 = unlimited)
 * @param until_time    Stop time (0 = unlimited)
 * @param out_processed Output: number of calculations actually processed
 * @return              PI_AF_OK on success
 */
pi_af_error_t pi_af_run(pi_af_analytics_context_t *ctx,
                         uint32_t max_calcs, time_t until_time,
                         uint32_t *out_processed);

/**
 * @brief Build the topological execution order from dependency graph.
 *
 * Uses Kahn's algorithm (BFS-based topological sort) to determine a
 * valid calculation order that respects all dependencies.
 * Stores the result in ctx->execution_order and sets order_valid = true.
 *
 * @param ctx  Engine context
 * @return     PI_AF_OK on success, PI_AF_ERR_CIRCULAR_DEPENDENCY on cycle
 *
 * Complexity: O(V + E) where V = registered analytics, E = dependencies
 *
 * @see CMU 24-677 — Dependency resolution in real-time systems
 * @see Kahn, A.B. (1962) "Topological sorting of large networks"
 */
pi_af_error_t pi_af_build_execution_order(pi_af_analytics_context_t *ctx);

/**
 * @brief Check whether two time ranges overlap.
 *
 * Used for determining if an event-frame-based analytic's calculation
 * window overlaps with available data.
 *
 * @param start1, end1  First time range
 * @param start2, end2  Second time range
 * @return              true if ranges intersect
 *
 * Complexity: O(1)
 */
bool pi_af_time_range_overlaps(time_t start1, time_t end1,
                                 time_t start2, time_t end2);

/**
 * @brief Compute the intersection of two time ranges.
 *
 * @param start1, end1  First time range
 * @param start2, end2  Second time range
 * @param out_start, out_end  Output: intersection range
 * @return              true if intersection is non-empty
 *
 * Complexity: O(1)
 */
bool pi_af_time_range_intersection(time_t start1, time_t end1,
                                    time_t start2, time_t end2,
                                    time_t *out_start, time_t *out_end);

/**
 * @brief Get a human-readable string for an error code.
 *
 * @param err  Error code
 * @return     Static string description
 *
 * Complexity: O(1)
 */
const char *pi_af_error_string(pi_af_error_t err);

/**
 * @brief Get a human-readable string for a schedule type.
 *
 * Complexity: O(1)
 */
const char *pi_af_schedule_type_string(pi_af_schedule_type_t t);

/**
 * @brief Get a human-readable string for a calculation status.
 *
 * Complexity: O(1)
 */
const char *pi_af_calc_status_string(pi_af_calc_status_t s);

#endif /* PI_AF_ANALYTICS_CORE_H */
