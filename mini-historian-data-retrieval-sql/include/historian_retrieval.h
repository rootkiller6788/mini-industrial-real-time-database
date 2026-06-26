#ifndef HISTORIAN_RETRIEVAL_H
#define HISTORIAN_RETRIEVAL_H

/**
 * @file    historian_retrieval.h
 * @brief   SQL-based data retrieval API for industrial historians.
 *
 * Provides programmatic SQL query construction and execution for
 * industrial time-series databases (OSIsoft PI OLEDB, Honeywell PHD SQL,
 * AspenTech SQLplus). Implements the historian equivalent of:
 *   SELECT value, timestamp FROM history WHERE tag='X' AND time BETWEEN A AND B
 *
 * Knowledge Coverage:
 *   L2: Snapshot vs. historical queries, raw vs. interpolated retrieval
 *   L3: SQL AST construction, parameterized queries, time-range filtering
 *   L4: ANSI SQL-92 compliance, SQL injection prevention
 *   L5: Various sampling modes (raw, interpolated, aggregated, snapshot)
 *   L7: PI OLEDB Enterprise, Honeywell PHD SQL, Aspen SQLplus patterns
 */

#include "historian_model.h"
#include <stdint.h>
#include <stddef.h>

/*---------------------------------------------------------------------------
 * L2: Query Mode - Core retrieval concepts
 *
 * Four fundamental retrieval modes exist in any industrial historian:
 *   RAW:           Every stored data point (archived values)
 *   INTERPOLATED:  Values at regular intervals, computed from stored data
 *   AGGREGATED:    Summary statistics over time buckets (hourly avg, daily max)
 *   SNAPSHOT:      Current (most recent) value only
 *
 * These are the four pillars of historian data access (OSIsoft PI SDK,
 * OPC HDA ReadRaw/ReadProcessed/ReadAtTime/ReadModified).
 *---------------------------------------------------------------------------*/

/** Data retrieval mode. */
typedef enum {
    HISTORIAN_QUERY_RAW          = 0,
    HISTORIAN_QUERY_INTERPOLATED = 1,
    HISTORIAN_QUERY_AGGREGATED   = 2,
    HISTORIAN_QUERY_SNAPSHOT     = 3
} historian_query_mode_t;

/*---------------------------------------------------------------------------
 * L3: SQL Dialect - Historian-specific SQL variants
 *
 * Industrial historians extend ANSI SQL with time-series functions:
 *   OSIsoft PI OLEDB:  WHERE time BETWEEN 't1' AND 't2'
 *   Honeywell PHD SQL: SELECT ... FROM ihRawData WHERE ...
 *   AspenTech SQLplus: SELECT ... FROM IP_History WHERE ...
 *---------------------------------------------------------------------------*/

typedef enum {
    HISTORIAN_SQL_STANDARD = 0,    /**< ANSI SQL-92 */
    HISTORIAN_SQL_PI_OLEDB = 1,    /**< OSIsoft PI OLEDB Enterprise */
    HISTORIAN_SQL_PHD      = 2,    /**< Honeywell PHD SQL */
    HISTORIAN_SQL_ASPEN    = 3,    /**< AspenTech SQLplus */
    HISTORIAN_SQL_CUSTOM   = 4     /**< Custom/embedded SQLite */
} historian_sql_dialect_t;

/*---------------------------------------------------------------------------
 * L3: Sampling Mode - controls how many points are returned
 *---------------------------------------------------------------------------*/

typedef enum {
    HISTORIAN_SAMPLING_ALL          = 0,   /**< All stored points */
    HISTORIAN_SAMPLING_INTERVAL     = 1,   /**< Fixed interval (every N seconds) */
    HISTORIAN_SAMPLING_COUNT        = 2,   /**< Exactly N evenly spaced points */
    HISTORIAN_SAMPLING_EVENT_SNAPS  = 3    /**< Snapshot at event times */
} historian_sampling_mode_t;

/*---------------------------------------------------------------------------
 * L5: Query Specification - Complete parameterization of a retrieval
 *
 * This struct embodies the full set of parameters that constitute a
 * historian data retrieval request. It is the programmatic equivalent
 * of the PI DataLink "Retrieval Options" dialog or the OPC HDA
 * ReadRaw/ReadProcessed parameter block.
 *---------------------------------------------------------------------------*/

/** Direction of time-series traversal for retrieval. */
typedef enum {
    HISTORIAN_DIR_FORWARD  = 0,   /**< From start to end (normal) */
    HISTORIAN_DIR_BACKWARD = 1,   /**< From end to start (reverse) */
    HISTORIAN_DIR_NEAREST  = 2    /**< Find the single point nearest to center */
} historian_direction_t;

/**
 * @struct historian_query_spec_t
 * @brief  Complete specification of a historian data retrieval query.
 *
 * This is the primary input structure for all data retrieval functions.
 * It maps directly to:
 *   - PI OLEDB: SELECT ... FROM [piarchive]..[picomp2] WHERE tag='...'
 *   - PHD SQL:   SELECT ... FROM ihRawData WHERE TagName='...'
 *   - SQLplus:   SELECT ... FROM IP_History WHERE Name='...'
 */
typedef struct {
    /* Target identification */
    int32_t      tag_id;               /**< Tag to query (-1 = use tag_filter) */
    const char   *tag_filter;          /**< SQL LIKE pattern for multi-tag query */
    int32_t      *tag_ids;             /**< Array of tag IDs for bulk query */
    size_t       tag_count;            /**< Number of tag IDs in array */

    /* Time specification */
    historian_time_range_t  time_range; /**< Query time boundaries */
    historian_direction_t   direction;  /**< Traversal direction */

    /* Retrieval mode */
    historian_query_mode_t    query_mode;
    historian_sampling_mode_t sampling_mode;
    int64_t   sample_interval_ms;       /**< Interval for INTERPOLATED/AGGREGATED */
    size_t    max_points;               /**< Maximum points to return (0 = unlimited) */

    /* Filtering */
    int       filter_good_only;         /**< 1 = exclude bad/uncertain quality */
    double    filter_value_min;         /**< Minimum value threshold */
    double    filter_value_max;         /**< Maximum value threshold */
    int       filter_apply_range;       /**< 1 = apply value range filter */

    /* SQL dialect */
    historian_sql_dialect_t sql_dialect;

    /* Pagination (for queries returning millions of rows) */
    size_t    page_offset;              /**< 0-based page offset */
    size_t    page_size;                /**< Rows per page (0 = all) */

    /* Timezone handling */
    int       output_tz_offset_min;     /**< Desired output timezone offset */
    int       convert_to_output_tz;     /**< 1 = convert to output timezone */
} historian_query_spec_t;

/*---------------------------------------------------------------------------
 * L3: SQL AST - Programmatic SQL construction
 *
 * Rather than string concatenation (vulnerable to injection), we construct
 * SQL queries using a typed AST. This enables:
 *   1. SQL injection prevention by design
 *   2. Cross-dialect query generation
 *   3. Query optimization at the AST level
 *
 * Reference:
 *   - Chamberlin & Boyce (1974), "SEQUEL: A Structured English Query Language"
 *   - Date, C.J. "SQL and Relational Theory" (2009)
 *---------------------------------------------------------------------------*/

/** SQL AST node types for historian queries. */
typedef enum {
    SQL_NODE_SELECT     = 0,
    SQL_NODE_FROM       = 1,
    SQL_NODE_WHERE      = 2,
    SQL_NODE_AND        = 3,
    SQL_NODE_OR         = 4,
    SQL_NODE_BETWEEN    = 5,
    SQL_NODE_COMPARE    = 6,
    SQL_NODE_ORDER_BY   = 7,
    SQL_NODE_GROUP_BY   = 8,
    SQL_NODE_LIMIT      = 9,
    SQL_NODE_CONDITION  = 10
} historian_sql_node_type_t;

/** Comparison operators for SQL WHERE clause construction. */
typedef enum {
    HISTORIAN_SQL_OP_EQ  = 0,
    HISTORIAN_SQL_OP_NE  = 1,
    HISTORIAN_SQL_OP_LT  = 2,
    HISTORIAN_SQL_OP_LE  = 3,
    HISTORIAN_SQL_OP_GT  = 4,
    HISTORIAN_SQL_OP_GE  = 5,
    HISTORIAN_SQL_OP_LIKE = 6,
    HISTORIAN_SQL_OP_IN   = 7
} historian_sql_operator_t;

/**
 * @brief Maximum length of a generated SQL statement.
 *
 * Industrial SQL queries can be long when filtering many tags.
 */
#define HISTORIAN_SQL_MAX_LEN    16384

/**
 * @brief Generate a SQL SELECT statement from a query specification.
 *
 * Produces a dialect-appropriate SQL query string for the given spec.
 * The generated SQL is ready for execution against the historian database.
 *
 * @param spec      Query specification (must be fully populated).
 * @param sql_out   Output buffer for the generated SQL string.
 * @param buf_size  Size of sql_out buffer (must be >= HISTORIAN_SQL_MAX_LEN).
 * @return Number of characters written (excluding null), or -1 on error.
 *
 * Knowledge: Generates dialect-specific SQL (PI OLEDB vs PHD vs Standard).
 */
int historian_generate_sql(const historian_query_spec_t *spec,
                            char *sql_out, size_t buf_size);

/**
 * @brief Generate a parameterized SQL query (no string concatenation).
 *
 * Instead of embedding values in the SQL string, this generates a
 * parameterized query with placeholders (? or :param), preventing
 * SQL injection.
 *
 * @param spec        Query specification.
 * @param sql_out     Output SQL with placeholders.
 * @param buf_size    Buffer size for sql_out.
 * @param params_out  Array to receive parameter values.
 * @param param_cap   Capacity of params_out array.
 * @param param_count Output: number of parameters set.
 * @return SQL string length, or -1 on error.
 *
 * Knowledge: SQL injection prevention (L4: security).
 */
int historian_generate_parameterized_sql(const historian_query_spec_t *spec,
                                          char *sql_out, size_t buf_size,
                                          double *params_out, size_t param_cap,
                                          size_t *param_count);

/*---------------------------------------------------------------------------
 * L2: Core Retrieval API
 *
 * These functions implement the four fundamental historian retrieval
 * modes. Each corresponds to a PI SDK function:
 *   - piar_retrieve_raw             <->  piar_read_raw()
 *   - piar_retrieve_interpolated    <->  piar_read_interpolated()
 *   - piar_retrieve_aggregated      <->  piar_read_summary()
 *   - piar_retrieve_snapshot        <->  piar_read_snapshot()
 *---------------------------------------------------------------------------*/

/**
 * Retrieve raw archived values for a given query specification.
 *
 * Corresponds to OPC HDA ReadRaw.
 * Returns 0 on success, negative error code on failure.
 */
int historian_retrieve_raw(const historian_query_spec_t *spec,
                            historian_result_set_t *result);

/**
 * Retrieve values at regularly-spaced intervals using interpolation.
 *
 * For each output timestamp, this function:
 *   1. Finds the two surrounding stored data points
 *   2. Applies the specified interpolation method
 *   3. Marks the result with HISTORIAN_QUAL_SUB_INTERPOLATED
 *
 * Corresponds to OPC HDA ReadProcessed (interpolated variant).
 * Returns 0 on success, negative error code on failure.
 */
int historian_retrieve_interpolated(const historian_query_spec_t *spec,
                                     historian_result_set_t *result);

/**
 * Retrieve aggregated (summarized) values over time buckets.
 *
 * For each bucket (e.g., hourly, daily), computes the requested aggregate
 * statistic from the raw data points within that bucket.
 *
 * Corresponds to OPC HDA ReadProcessed (aggregate variant)
 * and PI DataLink "Calculated Data" queries.
 * Returns 0 on success, negative error code on failure.
 */
int historian_retrieve_aggregated(const historian_query_spec_t *spec,
                                   historian_result_set_t *result);

/**
 * Retrieve the current snapshot (most recent value) for one or more tags.
 *
 * Corresponds to PI SDK piar_read_snapshot().
 * Returns 0 on success, negative error code on failure.
 */
int historian_retrieve_snapshot(const historian_query_spec_t *spec,
                                 historian_result_set_t *result);

/*---------------------------------------------------------------------------
 * L2: Multi-tag query - Retrieving data for multiple tags simultaneously
 *
 * In practice, dashboards and reports query hundreds of tags at once.
 * Multi-tag retrieval is more efficient than N single-tag queries.
 *---------------------------------------------------------------------------*/

/**
 * Retrieve historical data for multiple tags in a single query.
 * Uses SQL UNION ALL or bulk data retrieval depending on dialect.
 */
int historian_retrieve_multi_tag(const historian_query_spec_t *spec,
                                  historian_result_set_t *result);

/*---------------------------------------------------------------------------
 * L5: Query Optimization
 *---------------------------------------------------------------------------*/

/**
 * Analyze a query specification and estimate the number of data points
 * it will return. Used for:
 *   - Cost-based query planning
 *   - Progress bar estimation
 *   - Memory pre-allocation
 *
 * @return Estimated row count, or -1 if inestimable.
 */
int64_t historian_estimate_row_count(const historian_query_spec_t *spec);

/**
 * Suggest an optimal page size for paginated retrieval based on the
 * query characteristics and available memory. Returns bytes.
 */
size_t historian_suggest_page_size(const historian_query_spec_t *spec);

/*---------------------------------------------------------------------------
 * L7: PI OLEDB Enterprise-specific data structures
 *
 * OSIsoft PI OLEDB Enterprise exposes the PI Data Archive as a set of
 * SQL-accessible tables and views.
 *---------------------------------------------------------------------------*/

/** PI OLEDB-specific table references. */
#define HISTORIAN_PI_TABLE_ARCHIVE    "[piarchive]..[picomp2]"
#define HISTORIAN_PI_TABLE_SNAPSHOT   "[piarchive]..[pisnapshot]"
#define HISTORIAN_PI_TABLE_EVENTS     "[piarchive]..[pievent]"

/** PI-specific column names. */
#define HISTORIAN_PI_COL_TAG         "tag"
#define HISTORIAN_PI_COL_TIME        "time"
#define HISTORIAN_PI_COL_VALUE       "value"
#define HISTORIAN_PI_COL_QUALITY     "status"
#define HISTORIAN_PI_COL_ANNOTATION  "annotated"

/** Max number of tags in a single PI OLEDB IN clause. */
#define HISTORIAN_PI_MAX_IN_ITEMS    500

/*---------------------------------------------------------------------------
 * L7: Honeywell PHD SQL-specific constants
 *---------------------------------------------------------------------------*/

/** PHD table references. */
#define HISTORIAN_PHD_TABLE_RAW       "ihRawData"
#define HISTORIAN_PHD_TABLE_SNAPSHOT  "ihSnapShot"
#define HISTORIAN_PHD_TABLE_ALIAS     "ihAlias"

/** PHD-specific functions. */
#define HISTORIAN_PHD_FN_TIMEAVG      "hdTimeAvg"
#define HISTORIAN_PHD_FN_TIMEMIN      "hdTimeMin"
#define HISTORIAN_PHD_FN_TIMEMAX      "hdTimeMax"
#define HISTORIAN_PHD_FN_TIMESTD      "hdTimeStdDev"

/*---------------------------------------------------------------------------
 * Utility functions
 *---------------------------------------------------------------------------*/

/**
 * Initialize a query specification to safe defaults.
 */
void historian_query_spec_init(historian_query_spec_t *spec);

/**
 * Validate a query specification (time range, parameters sanity).
 * Returns 0 if valid, negative error code with reason otherwise.
 */
int historian_query_spec_validate(const historian_query_spec_t *spec);

#endif /* HISTORIAN_RETRIEVAL_H */
