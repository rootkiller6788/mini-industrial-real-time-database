/**
 * @file src/pi_af_analytics_rollup.c
 * @brief PI AF Analytics — Asset Hierarchy Rollup Engine
 *
 * Implements hierarchical asset rollup — the mechanism by which
 * PI AF aggregates values from child assets up through the
 * enterprise hierarchy (Sensor → Equipment → Unit → Area → Site).
 *
 * This is the core differentiator of asset-centric analytics:
 * rather than looking at tags in isolation, you roll up performance
 * metrics through the physical asset model.
 *
 * Key algorithms:
 *   - Post-order tree traversal for bottom-up rollup
 *   - BFS for level-order display/reporting
 *   - DFS for parent-child dependency analysis
 *   - Lowest Common Ancestor (LCA) for asset relationship queries
 *   - Leaf enumeration for performance measurement at the edge
 *
 * Knowledge Coverage: L1 (Asset Hierarchy), L2 (Rollup Semantics),
 *                     L3 (Tree Data Structure), L5 (DFS/BFS/LCA),
 *                     L7 (ISA-95 Equipment Hierarchy)
 *
 * References:
 *   - ISA-95 Part 2 §4.1 — Equipment hierarchy model
 *   - ISA-88 Part 1 §4 — Physical model
 *   - OSIsoft PI AF SDK — AFElement and hierarchy traversal
 *   - Cormen, Leiserson, Rivest, Stein (2009) "Introduction to Algorithms" §10.4,
 *     §22.2 (BFS), §22.3 (DFS), §21.3 (LCA via Tarjan's algorithm)
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "pi_af_analytics_rollup.h"

/* --------------------------------------------------------------------------
 * L1: String Tables
 * ------------------------------------------------------------------------*/

const char *pi_af_asset_category_name(pi_af_asset_category_t cat) {
    switch (cat) {
        case PI_AF_ASSET_CAT_ENTERPRISE:       return "Enterprise";
        case PI_AF_ASSET_CAT_SITE:             return "Site";
        case PI_AF_ASSET_CAT_AREA:             return "Area";
        case PI_AF_ASSET_CAT_PROCESS_CELL:     return "Process Cell";
        case PI_AF_ASSET_CAT_UNIT:             return "Unit";
        case PI_AF_ASSET_CAT_EQUIPMENT_MODULE: return "Equipment Module";
        case PI_AF_ASSET_CAT_CONTROL_MODULE:   return "Control Module";
        case PI_AF_ASSET_CAT_SENSOR:           return "Sensor";
        default:                               return "Unknown";
    }
}

const char *pi_af_rollup_method_name(pi_af_rollup_method_t m) {
    switch (m) {
        case PI_AF_ROLLUP_SUM:      return "Sum";
        case PI_AF_ROLLUP_AVERAGE:  return "Average";
        case PI_AF_ROLLUP_MIN:      return "Min";
        case PI_AF_ROLLUP_MAX:      return "Max";
        case PI_AF_ROLLUP_WEIGHTED: return "Weighted";
        case PI_AF_ROLLUP_FORMULA:  return "Formula";
        case PI_AF_ROLLUP_COUNT:    return "Count";
        case PI_AF_ROLLUP_WORST:    return "Worst";
        default:                    return "Unknown";
    }
}

/* --------------------------------------------------------------------------
 * Engine Lifecycle
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_asset_init(pi_af_asset_context_t *ctx) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;
    memset(ctx, 0, sizeof(*ctx));
    ctx->traversal_valid = false;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Asset Registration
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_asset_add(pi_af_asset_context_t *ctx,
                               const pi_af_asset_node_t *node,
                               uint32_t *out_id) {
    if (!ctx || !node) return PI_AF_ERR_INVALID_ARGUMENT;

    /* Validate parent if specified */
    if (node->parent_id != 0) {
        bool parent_exists = false;
        for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
            if (ctx->node_in_use[i] &&
                ctx->nodes[i].asset_id == node->parent_id) {
                parent_exists = true;
                break;
            }
        }
        if (!parent_exists) return PI_AF_ERR_NOT_FOUND;
    }

    /* Find free slot */
    uint32_t slot = (uint32_t)(-1);
    for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
        if (!ctx->node_in_use[i]) { slot = i; break; }
    }
    if (slot == (uint32_t)(-1)) return PI_AF_ERR_OUT_OF_MEMORY;

    /* Copy node */
    memcpy(&ctx->nodes[slot], node, sizeof(pi_af_asset_node_t));

    /* Assign ID */
    if (ctx->nodes[slot].asset_id == 0) {
        uint32_t max_id = 0;
        for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
            if (ctx->node_in_use[i] &&
                ctx->nodes[i].asset_id > max_id) {
                max_id = ctx->nodes[i].asset_id;
            }
        }
        ctx->nodes[slot].asset_id = max_id + 1;
    }

    ctx->nodes[slot].rolled_valid = false;
    ctx->nodes[slot].rolled_value = 0.0;
    ctx->nodes[slot].rollup_count = 0;
    ctx->nodes[slot].last_rolled = 0;

    /* Register with parent */
    if (ctx->nodes[slot].parent_id != 0) {
        pi_af_asset_node_t *parent = pi_af_asset_get(
            ctx, ctx->nodes[slot].parent_id);
        if (parent && parent->child_count < PI_AF_MAX_CHILDREN) {
            parent->child_ids[parent->child_count++] = ctx->nodes[slot].asset_id;
        }
    } else {
        /* Root node */
        if (ctx->root_count < PI_AF_MAX_CHILDREN) {
            ctx->root_ids[ctx->root_count++] = ctx->nodes[slot].asset_id;
        }
    }

    ctx->node_in_use[slot] = true;
    ctx->node_count++;
    ctx->traversal_valid = false;

    if (out_id) *out_id = ctx->nodes[slot].asset_id;
    return PI_AF_OK;
}

/**
 * @brief Recursively remove a subtree.
 */
static void pi_af_asset_remove_recursive(pi_af_asset_context_t *ctx,
                                          uint32_t slot) {
    if (!ctx || slot >= PI_AF_MAX_ASSETS) return;

    pi_af_asset_node_t *node = &ctx->nodes[slot];

    /* Remove all children first */
    while (node->child_count > 0) {
        uint32_t child_id = node->child_ids[0];
        for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
            if (ctx->node_in_use[i] && ctx->nodes[i].asset_id == child_id) {
                pi_af_asset_remove_recursive(ctx, i);
                break;
            }
        }
        /* Shift remaining children */
        for (uint32_t c = 0; c < node->child_count - 1; c++) {
            node->child_ids[c] = node->child_ids[c + 1];
        }
        node->child_count--;
    }

    /* Remove from parent's child list */
    if (node->parent_id != 0) {
        pi_af_asset_node_t *parent = pi_af_asset_get(ctx, node->parent_id);
        if (parent) {
            for (uint32_t c = 0; c < parent->child_count; c++) {
                if (parent->child_ids[c] == node->asset_id) {
                    /* Shift */
                    for (uint32_t j = c; j < parent->child_count - 1; j++) {
                        parent->child_ids[j] = parent->child_ids[j + 1];
                    }
                    parent->child_count--;
                    break;
                }
            }
        }
    }

    /* Remove from root list if applicable */
    if (node->parent_id == 0) {
        for (uint32_t r = 0; r < ctx->root_count; r++) {
            if (ctx->root_ids[r] == node->asset_id) {
                for (uint32_t j = r; j < ctx->root_count - 1; j++) {
                    ctx->root_ids[j] = ctx->root_ids[j + 1];
                }
                ctx->root_count--;
                break;
            }
        }
    }

    ctx->node_in_use[slot] = false;
    ctx->node_count--;
    ctx->traversal_valid = false;
    memset(&ctx->nodes[slot], 0, sizeof(pi_af_asset_node_t));
}

pi_af_error_t pi_af_asset_remove(pi_af_asset_context_t *ctx,
                                  uint32_t asset_id) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
        if (ctx->node_in_use[i] && ctx->nodes[i].asset_id == asset_id) {
            pi_af_asset_remove_recursive(ctx, i);
            return PI_AF_OK;
        }
    }
    return PI_AF_ERR_NOT_FOUND;
}

pi_af_asset_node_t *pi_af_asset_get(const pi_af_asset_context_t *ctx,
                                     uint32_t asset_id) {
    if (!ctx) return NULL;
    for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
        if (ctx->node_in_use[i] && ctx->nodes[i].asset_id == asset_id) {
            return (pi_af_asset_node_t *)&ctx->nodes[i];
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * L5: Tree Traversal — DFS (Post-Order for Bottom-Up Rollup)
 * ------------------------------------------------------------------------*/

/**
 * @brief Recursive DFS helper for subtree traversal.
 *
 * @param ctx       Asset context
 * @param slot      Current node slot
 * @param order     Output array (filled in post-order)
 * @param max_nodes Maximum output entries
 * @param p_count   Pointer to current count
 */
static void dfs_recursive(const pi_af_asset_context_t *ctx, uint32_t slot,
                           uint32_t *order, uint32_t max_nodes,
                           uint32_t *p_count) {
    if (!ctx || slot >= PI_AF_MAX_ASSETS) return;
    if (*p_count >= max_nodes) return;

    const pi_af_asset_node_t *node = &ctx->nodes[slot];

    /* Visit children first (post-order) */
    for (uint32_t i = 0; i < node->child_count; i++) {
        /* Find slot for child */
        for (uint32_t j = 0; j < PI_AF_MAX_ASSETS; j++) {
            if (ctx->node_in_use[j] &&
                ctx->nodes[j].asset_id == node->child_ids[i]) {
                dfs_recursive(ctx, j, order, max_nodes, p_count);
                break;
            }
        }
        if (*p_count >= max_nodes) return;
    }

    /* Visit current node */
    if (*p_count < max_nodes) {
        order[*p_count] = node->asset_id;
        (*p_count)++;
    }
}

pi_af_error_t pi_af_asset_dfs(const pi_af_asset_context_t *ctx,
                               uint32_t start_id,
                               uint32_t *order, uint32_t max_nodes,
                               uint32_t *out_count) {
    if (!ctx || !order || !out_count) return PI_AF_ERR_INVALID_ARGUMENT;

    *out_count = 0;

    /* Find starting slot */
    uint32_t slot = (uint32_t)(-1);
    for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
        if (ctx->node_in_use[i] && ctx->nodes[i].asset_id == start_id) {
            slot = i; break;
        }
    }
    if (slot == (uint32_t)(-1)) return PI_AF_ERR_NOT_FOUND;

    dfs_recursive(ctx, slot, order, max_nodes, out_count);
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Tree Traversal — BFS (Level-Order)
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_asset_bfs(const pi_af_asset_context_t *ctx,
                               uint32_t start_id,
                               uint32_t *order, uint32_t max_nodes,
                               uint32_t *out_count) {
    if (!ctx || !order || !out_count) return PI_AF_ERR_INVALID_ARGUMENT;

    *out_count = 0;

    /* Find starting slot */
    uint32_t start_slot = (uint32_t)(-1);
    for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
        if (ctx->node_in_use[i] && ctx->nodes[i].asset_id == start_id) {
            start_slot = i; break;
        }
    }
    if (start_slot == (uint32_t)(-1)) return PI_AF_ERR_NOT_FOUND;

    /* BFS queue */
    uint32_t queue[PI_AF_MAX_ASSETS];
    uint32_t qh = 0, qt = 0;
    queue[qt++] = start_slot;

    while (qh < qt && *out_count < max_nodes) {
        uint32_t cur_slot = queue[qh++];
        const pi_af_asset_node_t *cur = &ctx->nodes[cur_slot];

        order[*out_count] = cur->asset_id;
        (*out_count)++;

        /* Enqueue all children */
        for (uint32_t i = 0; i < cur->child_count && qt < PI_AF_MAX_ASSETS; i++) {
            for (uint32_t j = 0; j < PI_AF_MAX_ASSETS; j++) {
                if (ctx->node_in_use[j] &&
                    ctx->nodes[j].asset_id == cur->child_ids[i]) {
                    queue[qt++] = j;
                    break;
                }
            }
        }
    }

    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Bottom-Up Rollup (Post-Order Aggregation)
 * ------------------------------------------------------------------------*/

/**
 * @brief Roll up values for a single asset from its children.
 *
 * If recursive is true, recursively ensures children are rolled up first
 * (using depth-first post-order traversal).
 */
pi_af_error_t pi_af_asset_rollup(pi_af_asset_context_t *ctx,
                                  uint32_t asset_id, bool recursive,
                                  double *out_value) {
    if (!ctx || !out_value) return PI_AF_ERR_INVALID_ARGUMENT;

    pi_af_asset_node_t *node = pi_af_asset_get(ctx, asset_id);
    if (!node) return PI_AF_ERR_NOT_FOUND;

    /* Leaf nodes: rolled_value = current_value */
    if (node->child_count == 0) {
        node->rolled_value = node->current_value;
        node->rolled_valid = true;
        *out_value = node->rolled_value;
        return PI_AF_OK;
    }

    /* Recursively roll up children first */
    if (recursive) {
        for (uint32_t i = 0; i < node->child_count; i++) {
            double child_val;
            pi_af_asset_rollup(ctx, node->child_ids[i], true, &child_val);
        }
    }

    /* Collect child values */
    double child_values[PI_AF_MAX_CHILDREN];
    /* double child_weights[PI_AF_MAX_CHILDREN]; */
    for (uint32_t i = 0; i < node->child_count; i++) {
        pi_af_asset_node_t *child = pi_af_asset_get(ctx, node->child_ids[i]);
        child_values[i] = child ? child->rolled_value : 0.0;
    }

    double result = 0.0;

    switch (node->rollup_method) {
        case PI_AF_ROLLUP_SUM: {
            double sum = 0.0;
            for (uint32_t i = 0; i < node->child_count; i++) sum += child_values[i];
            result = sum;
            break;
        }
        case PI_AF_ROLLUP_AVERAGE: {
            double sum = 0.0;
            for (uint32_t i = 0; i < node->child_count; i++) sum += child_values[i];
            result = (node->child_count > 0) ? sum / (double)node->child_count : 0.0;
            break;
        }
        case PI_AF_ROLLUP_MIN: {
            double min_val = (node->child_count > 0) ? child_values[0] : 0.0;
            for (uint32_t i = 1; i < node->child_count; i++) {
                if (child_values[i] < min_val) min_val = child_values[i];
            }
            result = min_val;
            break;
        }
        case PI_AF_ROLLUP_MAX: {
            double max_val = (node->child_count > 0) ? child_values[0] : 0.0;
            for (uint32_t i = 1; i < node->child_count; i++) {
                if (child_values[i] > max_val) max_val = child_values[i];
            }
            result = max_val;
            break;
        }
        case PI_AF_ROLLUP_WEIGHTED: {
            double sum_wv = 0.0, sum_w = 0.0;
            for (uint32_t i = 0; i < node->child_count; i++) {
                pi_af_asset_node_t *child = pi_af_asset_get(ctx, node->child_ids[i]);
                double w = child ? (child->design_capacity > 0.0 ?
                                    child->design_capacity : 1.0) : 1.0;
                sum_wv += w * child_values[i];
                sum_w  += w;
            }
            result = (sum_w > 0.0) ? sum_wv / sum_w : 0.0;
            break;
        }
        case PI_AF_ROLLUP_COUNT:
            result = (double)node->child_count;
            break;
        case PI_AF_ROLLUP_WORST: {
            /* "Worst" = lowest performance score among children */
            double worst = 100.0;
            for (uint32_t i = 0; i < node->child_count; i++) {
                pi_af_asset_node_t *child = pi_af_asset_get(ctx, node->child_ids[i]);
                /* Use child_value as a proxy for the score */
                double score = child ? child->rolled_value : 100.0;
                if (score < worst) worst = score;
            }
            result = worst;
            break;
        }
        case PI_AF_ROLLUP_FORMULA:
            /* Formula evaluation would use the expression engine.
             * For now, fall back to average. */
            {
                double sum = 0.0;
                for (uint32_t i = 0; i < node->child_count; i++) {
                    sum += child_values[i];
                }
                result = (node->child_count > 0) ?
                    sum / (double)node->child_count : 0.0;
            }
            break;
        default:
            return PI_AF_ERR_INVALID_ARGUMENT;
    }

    node->rolled_value = result;
    node->rolled_valid = true;
    node->rollup_count++;
    node->last_rolled = time(NULL);

    *out_value = result;
    return PI_AF_OK;
}

/**
 * @brief Roll up all nodes in the tree from leaves to roots.
 *
 * Uses post-order DFS to ensure each node's children are
 * rolled up before the parent.
 */
pi_af_error_t pi_af_asset_rollup_all(pi_af_asset_context_t *ctx) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    /* For each root, do a DFS rollup */
    for (uint32_t r = 0; r < ctx->root_count; r++) {
        uint32_t dfs_order[PI_AF_MAX_ASSETS];
        uint32_t dfs_count = 0;
        pi_af_error_t ret = pi_af_asset_dfs(ctx, ctx->root_ids[r],
                                              dfs_order, PI_AF_MAX_ASSETS,
                                              &dfs_count);
        if (ret != PI_AF_OK) return ret;

        /* DFS post-order: children before parents, so we can roll up in order */
        for (uint32_t i = 0; i < dfs_count; i++) {
            double val;
            pi_af_asset_rollup(ctx, dfs_order[i], false, &val);
        }
    }

    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * Asset Value Management
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_asset_set_value(pi_af_asset_context_t *ctx,
                                     uint32_t asset_id, double value,
                                     time_t timestamp) {
    if (!ctx) return PI_AF_ERR_INVALID_ARGUMENT;

    pi_af_asset_node_t *node = pi_af_asset_get(ctx, asset_id);
    if (!node) return PI_AF_ERR_NOT_FOUND;

    node->current_value = value;
    node->rolled_valid = false; /* Invalidate rollup cache up the chain */

    /* Invalidate ancestors */
    uint32_t cur_id = node->parent_id;
    while (cur_id != 0) {
        pi_af_asset_node_t *anc = pi_af_asset_get(ctx, cur_id);
        if (anc) {
            anc->rolled_valid = false;
            cur_id = anc->parent_id;
        } else {
            break;
        }
    }

    (void)timestamp; /* Timestamp for future persistence */
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Tree Properties
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_asset_depth(const pi_af_asset_context_t *ctx,
                                 uint32_t asset_id, uint32_t *out_depth) {
    if (!ctx || !out_depth) return PI_AF_ERR_INVALID_ARGUMENT;

    uint32_t depth = 0;
    uint32_t cur_id = asset_id;

    while (cur_id != 0) {
        pi_af_asset_node_t *node = pi_af_asset_get(ctx, cur_id);
        if (!node) return PI_AF_ERR_NOT_FOUND;
        cur_id = node->parent_id;
        depth++;
    }

    *out_depth = depth;
    return PI_AF_OK;
}

/**
 * @brief Recursively count descendants.
 */
static uint32_t count_descendants_recursive(const pi_af_asset_context_t *ctx,
                                              uint32_t asset_id) {
    pi_af_asset_node_t *node = pi_af_asset_get(ctx, asset_id);
    if (!node) return 0;

    uint32_t total = node->child_count;
    for (uint32_t i = 0; i < node->child_count; i++) {
        total += count_descendants_recursive(ctx, node->child_ids[i]);
    }
    return total;
}

pi_af_error_t pi_af_asset_descendant_count(const pi_af_asset_context_t *ctx,
                                            uint32_t asset_id,
                                            uint32_t *out_count) {
    if (!ctx || !out_count) return PI_AF_ERR_INVALID_ARGUMENT;

    if (!pi_af_asset_get(ctx, asset_id)) return PI_AF_ERR_NOT_FOUND;

    *out_count = count_descendants_recursive(ctx, asset_id);
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Path to Root & Lowest Common Ancestor
 * ------------------------------------------------------------------------*/

/**
 * @brief Build the path from root to a given asset.
 *
 * Walks up from the asset to the root, then reverses for root-to-asset order.
 */
pi_af_error_t pi_af_asset_path(const pi_af_asset_context_t *ctx,
                                uint32_t asset_id,
                                uint32_t *path, uint32_t max_path,
                                uint32_t *out_len) {
    if (!ctx || !path || !out_len) return PI_AF_ERR_INVALID_ARGUMENT;

    /* Walk up to root, storing in reverse */
    uint32_t temp[PI_AF_MAX_ASSET_DEPTH + 1];
    uint32_t len = 0;
    uint32_t cur_id = asset_id;

    while (cur_id != 0 && len < PI_AF_MAX_ASSET_DEPTH) {
        pi_af_asset_node_t *node = pi_af_asset_get(ctx, cur_id);
        if (!node) return PI_AF_ERR_NOT_FOUND;
        temp[len++] = cur_id;
        cur_id = node->parent_id;
    }

    /* Reverse into output */
    uint32_t out_len_val = (len < max_path) ? len : max_path;
    for (uint32_t i = 0; i < out_len_val; i++) {
        path[i] = temp[len - 1 - i];
    }

    *out_len = out_len_val;
    return PI_AF_OK;
}

/**
 * @brief Find the lowest common ancestor of two assets.
 *
 * Algorithm:
 *   1. Build path from root to asset1 (A-path).
 *   2. Build path from root to asset2 (B-path).
 *   3. Traverse both paths from root; the last common node is the LCA.
 *
 * @see Bender, M.A. & Farach-Colton, M. (2000) "The LCA Problem Revisited"
 *      LATIN 2000. The O(1) RMQ-based approach is optimal, but the
 *      O(depth) path method is sufficient for typical industrial hierarchies.
 */
pi_af_error_t pi_af_asset_lowest_common_ancestor(
    const pi_af_asset_context_t *ctx,
    uint32_t id1, uint32_t id2, uint32_t *out_lca) {
    if (!ctx || !out_lca) return PI_AF_ERR_INVALID_ARGUMENT;

    uint32_t path1[PI_AF_MAX_ASSET_DEPTH + 1];
    uint32_t path2[PI_AF_MAX_ASSET_DEPTH + 1];
    uint32_t len1 = 0, len2 = 0;

    pi_af_error_t ret = pi_af_asset_path(ctx, id1, path1,
                                          PI_AF_MAX_ASSET_DEPTH, &len1);
    if (ret != PI_AF_OK) return ret;
    ret = pi_af_asset_path(ctx, id2, path2, PI_AF_MAX_ASSET_DEPTH, &len2);
    if (ret != PI_AF_OK) return ret;

    /* Find last common node from root */
    uint32_t lca = 0;
    uint32_t min_len = (len1 < len2) ? len1 : len2;
    for (uint32_t i = 0; i < min_len; i++) {
        if (path1[i] == path2[i]) {
            lca = path1[i];
        } else {
            break;
        }
    }

    *out_lca = lca;
    return PI_AF_OK;
}

/* --------------------------------------------------------------------------
 * L5: Leaf Enumeration
 * ------------------------------------------------------------------------*/

pi_af_error_t pi_af_asset_get_leaves(const pi_af_asset_context_t *ctx,
                                      uint32_t *leaf_ids, uint32_t max_leaves,
                                      uint32_t *out_count) {
    if (!ctx || !leaf_ids || !out_count) return PI_AF_ERR_INVALID_ARGUMENT;

    *out_count = 0;
    for (uint32_t i = 0; i < PI_AF_MAX_ASSETS; i++) {
        if (!ctx->node_in_use[i]) continue;
        if (ctx->nodes[i].child_count == 0) {
            if (*out_count < max_leaves) {
                leaf_ids[*out_count] = ctx->nodes[i].asset_id;
            }
            (*out_count)++;
        }
    }

    return PI_AF_OK;
}
