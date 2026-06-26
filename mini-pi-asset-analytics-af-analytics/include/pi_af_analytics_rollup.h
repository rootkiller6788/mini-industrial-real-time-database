/**
 * @file include/pi_af_analytics_rollup.h
 * @brief PI AF Analytics — Asset Hierarchy Rollup Engine
 *
 * Implements the hierarchical asset rollup framework that is central to
 * PI AF's value proposition. In industrial operations, assets are organized
 * in a tree hierarchy (Enterprise → Site → Area → Unit → Equipment).
 * Analytics roll up values from leaf nodes to root nodes.
 *
 * Rollup types:
 *   - Simple aggregation (sum, avg, min, max) of child values
 *   - Weighted rollup based on capacity/size/priority
 *   - Conditional rollup (only include children meeting criteria)
 *   - Time-windowed rollup (aggregate over matching time periods)
 *   - Formula-based rollup (custom expression combining children)
 *
 * Knowledge Coverage: L1 (Asset Hierarchy), L2 (Rollup Semantics),
 *                     L3 (Tree Traversal), L5 (Post-Order Aggregation),
 *                     L7 (Industrial Asset Modeling)
 *
 * Reference:
 *   - OSIsoft PI AF SDK — AFElement, AFElementTemplate, hierarchy traversal
 *   - ISA-95 Part 1 & 2 — Equipment hierarchy model
 *   - ISO 14224:2016 — Petroleum, petrochemical and natural gas industries —
 *     Collection and exchange of reliability and maintenance data for equipment
 *   - ISA-88 Part 1 — Physical model (Enterprise → Site → Area → Process Cell
 *     → Unit → Equipment Module → Control Module)
 *
 * Stanford ENGR205 — Hierarchical asset performance rollup
 * RWTH Aachen — Asset administration shell (AAS) hierarchy
 * Purdue ME 575 — Manufacturing asset performance management
 */

#ifndef PI_AF_ANALYTICS_ROLLUP_H
#define PI_AF_ANALYTICS_ROLLUP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "pi_af_analytics_core.h"

/* --------------------------------------------------------------------------
 * L1 — Definitions: Asset Hierarchy Model
 * ------------------------------------------------------------------------*/

/** Maximum nodes in an asset hierarchy */
#define PI_AF_MAX_ASSETS          64

/** Maximum children per asset node */
#define PI_AF_MAX_CHILDREN        32

/** Maximum depth of the asset tree */
#define PI_AF_MAX_ASSET_DEPTH     16

/** Maximum asset name length */
#define PI_AF_MAX_ASSET_NAME      256

/**
 * @brief Asset categories corresponding to ISA-95 equipment hierarchy.
 *
 * @see ISA-95 Part 2 §4.1 — Equipment Hierarchy Levels
 */
typedef enum {
    PI_AF_ASSET_CAT_ENTERPRISE      = 0,  /**< Top-level enterprise */
    PI_AF_ASSET_CAT_SITE            = 1,  /**< Physical location */
    PI_AF_ASSET_CAT_AREA            = 2,  /**< Production area */
    PI_AF_ASSET_CAT_PROCESS_CELL    = 3,  /**< Process cell (ISA-88) */
    PI_AF_ASSET_CAT_UNIT            = 4,  /**< Unit operation */
    PI_AF_ASSET_CAT_EQUIPMENT_MODULE = 5, /**< Equipment module */
    PI_AF_ASSET_CAT_CONTROL_MODULE  = 6,  /**< Control module */
    PI_AF_ASSET_CAT_SENSOR          = 7,  /**< Individual sensor/actuator */
} pi_af_asset_category_t;

/**
 * @brief Rollup aggregation methods.
 */
typedef enum {
    PI_AF_ROLLUP_SUM        = 0,  /**< Arithmetic sum of children */
    PI_AF_ROLLUP_AVERAGE    = 1,  /**< Unweighted mean */
    PI_AF_ROLLUP_MIN        = 2,  /**< Minimum child value */
    PI_AF_ROLLUP_MAX        = 3,  /**< Maximum child value */
    PI_AF_ROLLUP_WEIGHTED   = 4,  /**< Weighted by child capacity */
    PI_AF_ROLLUP_FORMULA    = 5,  /**< Custom expression over children */
    PI_AF_ROLLUP_COUNT      = 6,  /**< Number of children */
    PI_AF_ROLLUP_WORST      = 7,  /**< Worst-performing child (by score) */
} pi_af_rollup_method_t;

/**
 * @brief A single node in the asset hierarchy tree.
 *
 * Each node can have:
 *   - One parent (or 0 for root)
 *   - Multiple children
 *   - Associated analytics whose values are rolled up
 *   - Aggregation rule for combining children's values
 */
typedef struct {
    uint32_t  asset_id;                               /**< Unique ID */
    char      name[PI_AF_MAX_ASSET_NAME];             /**< Asset display name */
    pi_af_asset_category_t category;                  /**< ISA-95 category */
    char      description[PI_AF_MAX_ASSET_NAME];      /**< Description */

    /* Hierarchy */
    uint32_t  parent_id;                              /**< Parent asset ID (0 = root) */
    uint32_t  child_ids[PI_AF_MAX_CHILDREN];          /**< Child asset IDs */
    uint32_t  child_count;

    /* Direct instrumentation (leaf-level attributes) */
    uint32_t  attribute_analytic_id;                  /**< Direct measurement analytic */
    double    current_value;                          /**< Most recent native value */

    /* Rollup configuration */
    pi_af_rollup_method_t rollup_method;              /**< How to combine children */
    char      rollup_formula[PI_AF_MAX_EXPRESSION_LEN]; /**< For FORMULA method */
    double    rollup_weight;                          /**< Weight for WEIGHTED method */

    /* Rollup state */
    double    rolled_value;                           /**< Aggregated value from children */
    bool      rolled_valid;                           /**< Whether rolled_value is current */
    time_t    last_rolled;                            /**< When last rolled up */
    uint32_t  rollup_count;                           /**< How many times rolled up */

    /* Operational metadata */
    bool      is_active;                              /**< Whether asset is operational */
    double    design_capacity;                        /**< Nameplate capacity (for weighting) */
    char      uom[32];                                /**< Unit of measure */
} pi_af_asset_node_t;

/* --------------------------------------------------------------------------
 * L3 — Engineering Structure: Asset Tree
 * ------------------------------------------------------------------------*/

/**
 * @brief Asset hierarchy context — manages the entire asset tree.
 */
typedef struct {
    pi_af_asset_node_t nodes[PI_AF_MAX_ASSETS];
    uint32_t            node_count;
    bool                node_in_use[PI_AF_MAX_ASSETS];

    /* Tree root(s) */
    uint32_t            root_ids[PI_AF_MAX_CHILDREN];
    uint32_t            root_count;

    /* Traversal order (topological / BFS / DFS) */
    uint32_t            traversal_order[PI_AF_MAX_ASSETS];
    uint32_t            traversal_count;
    bool                traversal_valid;
} pi_af_asset_context_t;

/* --------------------------------------------------------------------------
 * L5 — Algorithms: Asset Hierarchy Operations
 * ------------------------------------------------------------------------*/

/**
 * @brief Initialize the asset hierarchy context.
 *
 * @param ctx  Uninitialized context
 * @return     PI_AF_OK on success
 */
pi_af_error_t pi_af_asset_init(pi_af_asset_context_t *ctx);

/**
 * @brief Add an asset node to the hierarchy.
 *
 * If parent_id is non-zero, the parent must already exist.
 * The node is automatically added to the parent's child list.
 *
 * @param ctx       Asset context
 * @param node      Asset node definition (ID is assigned if 0)
 * @param out_id    Output: assigned asset ID
 * @return          PI_AF_OK on success
 *
 * Complexity: O(1) for insertion, O(n) for ID lookup
 */
pi_af_error_t pi_af_asset_add(pi_af_asset_context_t *ctx,
                               const pi_af_asset_node_t *node,
                               uint32_t *out_id);

/**
 * @brief Remove an asset and all its descendants from the hierarchy.
 *
 * Cascading removal: all children are recursively removed.
 *
 * @param ctx       Asset context
 * @param asset_id  Asset to remove
 * @return          PI_AF_OK on success
 *
 * Complexity: O(n) where n = descendants count
 */
pi_af_error_t pi_af_asset_remove(pi_af_asset_context_t *ctx,
                                  uint32_t asset_id);

/**
 * @brief Look up an asset by ID.
 *
 * Complexity: O(n) where n = registered assets
 */
pi_af_asset_node_t *pi_af_asset_get(const pi_af_asset_context_t *ctx,
                                     uint32_t asset_id);

/**
 * @brief Set the current value of an asset's direct attribute.
 *
 * For leaf assets, this sets the native measurement.
 * Invalidates rolled values up the ancestral chain.
 *
 * @param ctx       Asset context
 * @param asset_id  Asset to update
 * @param value     New value
 * @param timestamp Measurement time
 * @return          PI_AF_OK on success
 *
 * Complexity: O(d) where d = depth to root (invalidation cascades up)
 */
pi_af_error_t pi_af_asset_set_value(pi_af_asset_context_t *ctx,
                                     uint32_t asset_id, double value,
                                     time_t timestamp);

/**
 * @brief Roll up values from children to a parent asset.
 *
 * Computes the parent's rolled_value by aggregating all children's
 * values using the parent's specified rollup method.
 *
 * If recursive is true, first ensures all children are rolled up
 * (bottom-up, post-order traversal).
 *
 * @param ctx        Asset context
 * @param asset_id   Asset to roll up
 * @param recursive  If true, recursively roll up children first
 * @param out_value  Output: rolled-up value
 * @return           PI_AF_OK on success
 *
 * Complexity: O(c) for non-recursive, O(n) for recursive (n = subtree size)
 *
 * @see "Tree accumulation" — Computer Science standard technique
 *      (post-order traversal with aggregation at each node)
 */
pi_af_error_t pi_af_asset_rollup(pi_af_asset_context_t *ctx,
                                  uint32_t asset_id, bool recursive,
                                  double *out_value);

/**
 * @brief Roll up all assets bottom-up.
 *
 * Ensures all rolled values in the tree are current.
 *
 * @param ctx  Asset context
 * @return     PI_AF_OK on success
 *
 * Complexity: O(n) where n = total assets (post-order traversal)
 */
pi_af_error_t pi_af_asset_rollup_all(pi_af_asset_context_t *ctx);

/**
 * @brief Depth-First Search (DFS) traversal of the asset tree.
 *
 * Visits nodes in DFS order (pre-order by default).
 *
 * @param ctx       Asset context
 * @param start_id  Starting asset (root of traversal)
 * @param order     Output: node IDs in traversal order
 * @param max_nodes Maximum output entries
 * @param out_count Output: actual count visited
 * @return          PI_AF_OK on success
 *
 * Complexity: O(n) where n = subtree size
 */
pi_af_error_t pi_af_asset_dfs(const pi_af_asset_context_t *ctx,
                               uint32_t start_id,
                               uint32_t *order, uint32_t max_nodes,
                               uint32_t *out_count);

/**
 * @brief Breadth-First Search (BFS) traversal of the asset tree.
 *
 * Visits nodes level by level — useful for same-generation comparisons.
 *
 * Complexity: O(n) where n = subtree size
 */
pi_af_error_t pi_af_asset_bfs(const pi_af_asset_context_t *ctx,
                               uint32_t start_id,
                               uint32_t *order, uint32_t max_nodes,
                               uint32_t *out_count);

/**
 * @brief Compute the depth of an asset in the hierarchy.
 *
 * Root nodes have depth 0. Each level down adds 1.
 *
 * @param ctx       Asset context
 * @param asset_id  Asset to measure
 * @param out_depth Output: depth (0 for root)
 * @return          PI_AF_OK on success
 *
 * Complexity: O(d) where d = depth
 */
pi_af_error_t pi_af_asset_depth(const pi_af_asset_context_t *ctx,
                                 uint32_t asset_id, uint32_t *out_depth);

/**
 * @brief Count total descendants (children, grandchildren, etc.) of an asset.
 *
 * @param ctx       Asset context
 * @param asset_id  Asset to count from
 * @param out_count Output: total descendant count
 * @return          PI_AF_OK on success
 *
 * Complexity: O(n) where n = subtree size
 */
pi_af_error_t pi_af_asset_descendant_count(const pi_af_asset_context_t *ctx,
                                            uint32_t asset_id,
                                            uint32_t *out_count);

/**
 * @brief Get the path from root to an asset.
 *
 * Returns the sequence of asset IDs from the root down to the given asset.
 *
 * @param ctx       Asset context
 * @param asset_id  Target asset
 * @param path      Output: array of asset IDs from root to target
 * @param max_path  Maximum path entries
 * @param out_len   Output: path length (0 if root itself)
 * @return          PI_AF_OK on success
 *
 * Complexity: O(d) where d = depth
 */
pi_af_error_t pi_af_asset_path(const pi_af_asset_context_t *ctx,
                                uint32_t asset_id,
                                uint32_t *path, uint32_t max_path,
                                uint32_t *out_len);

/**
 * @brief Find the lowest common ancestor of two assets.
 *
 * @param ctx       Asset context
 * @param id1, id2  Two asset IDs
 * @param out_lca   Output: lowest common ancestor ID (0 if none)
 * @return          PI_AF_OK on success
 *
 * Complexity: O(d1 + d2) where d1, d2 = depths
 */
pi_af_error_t pi_af_asset_lowest_common_ancestor(
    const pi_af_asset_context_t *ctx,
    uint32_t id1, uint32_t id2, uint32_t *out_lca);

/**
 * @brief Get all leaf assets (assets with no children).
 *
 * @param ctx        Asset context
 * @param leaf_ids   Output: array of leaf asset IDs
 * @param max_leaves Capacity of output array
 * @param out_count  Output: number of leaves found
 * @return           PI_AF_OK on success
 *
 * Complexity: O(n) where n = registered assets
 */
pi_af_error_t pi_af_asset_get_leaves(const pi_af_asset_context_t *ctx,
                                      uint32_t *leaf_ids, uint32_t max_leaves,
                                      uint32_t *out_count);

/**
 * @brief Get human-readable category name.
 *
 * Complexity: O(1)
 */
const char *pi_af_asset_category_name(pi_af_asset_category_t cat);

/**
 * @brief Get human-readable rollup method name.
 *
 * Complexity: O(1)
 */
const char *pi_af_rollup_method_name(pi_af_rollup_method_t m);

#endif /* PI_AF_ANALYTICS_ROLLUP_H */
