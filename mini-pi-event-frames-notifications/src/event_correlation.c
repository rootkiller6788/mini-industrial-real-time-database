/**
 * @file event_correlation.c
 * @brief Event Frame correlation analysis implementation
 *
 * Implements algorithms for discovering relationships between Event Frames:
 * temporal proximity scoring, Jaccard attribute similarity, correlation matrix
 * construction, causal chain discovery via graph DFS, DBSCAN temporal
 * clustering, and a Granger-causality proxy test for event streams.
 *
 * Knowledge mapping:
 *   L1: corr_link_t, corr_matrix_t, causal_chain_t, temporal_cluster_t
 *   L2: Causal discovery, event correlation, common-cause analysis
 *   L5: Jaccard index (Jaccard 1901), DBSCAN clustering (Ester et al. 1996),
 *       Granger causality proxy, DFS chain traversal
 *   L6: Cascade fault analysis, root cause identification
 *   L8: Transfer entropy approximation, event stream causality
 *
 * MIT 6.302 - Correlation analysis for fault diagnosis
 * CMU 24-677 - Causal reasoning in event systems
 * Georgia Tech ECE 6550 - Time-series event correlation
 */

#include "event_correlation.h"
#include "event_frame.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ─── L5: Temporal Proximity Correlation ────────────────────────────────────
 *
 * Measures how close two events are in time. Uses exponential decay:
 *
 *   strength = max(0.0, 1.0 - |delta_t| / max_window)
 *
 * where delta_t is the minimum time distance between any point in event A
 * and any point in event B.
 *
 * For events that are open (ACTIVE), the current time is used as end time.
 * For inactive events, returns 0.
 *
 * Complexity: O(1)
 */

double corr_temporal_proximity(const event_frame_t *a, const event_frame_t *b,
                               double max_window_s) {
    if (!a || !b || max_window_s <= 0.0) return 0.0;
    if (a->status == EF_STATUS_INACTIVE || b->status == EF_STATUS_INACTIVE)
        return 0.0;

    time_t a_end = (a->status == EF_STATUS_ACTIVE) ? time(NULL) : a->end_time;
    time_t b_end = (b->status == EF_STATUS_ACTIVE) ? time(NULL) : b->end_time;

    /* Find minimum distance between the two intervals */
    double dist;
    if (a_end < b->start_time) {
        dist = difftime(b->start_time, a_end);
    } else if (b_end < a->start_time) {
        dist = difftime(a->start_time, b_end);
    } else {
        /* Intervals overlap */
        dist = 0.0;
    }

    if (dist >= max_window_s) return 0.0;
    return 1.0 - (dist / max_window_s);
}

/* ─── L5: Jaccard Attribute Similarity ──────────────────────────────────────
 *
 * Jaccard index on attribute names that both events have set (is_set == 1):
 *
 *   J(A, B) = |attrs_A ∩ attrs_B| / |attrs_A ∪ attrs_B|
 *
 * This measures how much structured data the two events share, which
 * is a proxy for whether they describe the same type of process situation.
 *
 * Reference: Jaccard, P. (1901) "Distribution de la flore alpine"
 *            Bulletin de la Societe Vaudoise des Sciences Naturelles
 *
 * Complexity: O(min(m, n)) where m = A.attr_count, n = B.attr_count
 */

double corr_attribute_jaccard(const event_frame_t *a, const event_frame_t *b) {
    if (!a || !b) return 0.0;

    int intersection = 0;
    int union_count = 0;

    /* Count union: all unique attribute names set in either event */
    /* For each attribute in A */
    for (int i = 0; i < a->attr_count; i++) {
        if (!a->attributes[i].is_set) continue;
        
        for (int j = 0; j < b->attr_count; j++) {
            if (!b->attributes[j].is_set) continue;
            if (strcmp(a->attributes[i].name, b->attributes[j].name) == 0) {
                intersection++; break;
                intersection++;
                break;
            }
        }
        union_count++;
    }

    /* Add attributes in B that are NOT in A to the union count */
    for (int j = 0; j < b->attr_count; j++) {
        if (!b->attributes[j].is_set) continue;
        int found_in_a = 0;
        for (int i = 0; i < a->attr_count; i++) {
            if (!a->attributes[i].is_set) continue;
            if (strcmp(b->attributes[j].name, a->attributes[i].name) == 0) {
                found_in_a = 1;
                break;
            }
        }
        if (!found_in_a) union_count++;
    }

    if (union_count == 0) return 0.0;
    return (double)intersection / (double)union_count;
}

/* ─── L5: Correlation Matrix Construction ───────────────────────────────────
 *
 * Builds all pairwise correlations between events in the input set.
 * For each pair (i, j) with i < j, computes:
 *   1. Temporal proximity
 *   2. Attribute Jaccard similarity
 *   3. Template match (same template → strong correlation)
 *   4. Severity match
 *
 * Links are stored with aggregate strength = weighted average of
 * the individual correlation scores.
 *
 * Complexity: O(n^2 * k) where n = event_count, k = avg attrs per event
 */

int corr_build_matrix(corr_matrix_t *matrix, event_frame_t **events,
                      int event_count, double time_window_s) {
    if (!matrix || !events || event_count <= 0) return 0;
    if (event_count > CORR_MAX_EVENTS) event_count = CORR_MAX_EVENTS;

    memset(matrix, 0, sizeof(corr_matrix_t));
    matrix->event_count = event_count;
    matrix->time_window_s = time_window_s;
    matrix->analysis_time = time(NULL);

    /* Copy event pointers */
    for (int i = 0; i < event_count; i++) {
        matrix->events[i] = events[i];
    }

    /* Build pairwise correlations */
    for (int i = 0; i < event_count; i++) {
        for (int j = i + 1; j < event_count; j++) {
            if (matrix->link_count >= CORR_MAX_LINKS) break;

            event_frame_t *a = events[i];
            event_frame_t *b = events[j];

            double temp_score = corr_temporal_proximity(a, b, time_window_s);
            double jac_score = corr_attribute_jaccard(a, b);
            double tmpl_score = (strcmp(a->template_name, b->template_name) == 0) ? 1.0 : 0.0;
            double sev_score = (a->severity == b->severity) ? 0.5 : 0.0;

            /* Aggregate score: weighted combination */
            double aggregate = 0.35 * temp_score + 0.25 * jac_score +
                               0.25 * tmpl_score + 0.15 * sev_score;

            if (aggregate < 0.1) continue;  /* Skip weak correlations */

            corr_link_t *link = &matrix->links[matrix->link_count];
            memset(link, 0, sizeof(corr_link_t));

            strncpy(link->event_a, a->id, 36);
            link->event_a[36] = '\0';
            strncpy(link->event_b, b->id, 36);
            link->event_b[36] = '\0';

            link->strength = aggregate;
            link->time_proximity_s = fabs(difftime(
                (a->start_time < b->start_time) ? b->start_time : a->start_time,
                (a->start_time < b->start_time) ? a->start_time : b->start_time));

            /* Determine correlation types */
            int ct = 0;
            if (temp_score > 0.5) {
                link->corr_types[ct++] = CORR_TEMPORAL_PROXIMITY;
            }
            if (jac_score > 0.3) {
                link->corr_types[ct++] = CORR_ATTRIBUTE_SIMILARITY;
            }
            if (tmpl_score > 0.5) {
                link->corr_types[ct++] = CORR_TEMPLATE_FAMILY;
            }
            if (sev_score > 0.0) {
                link->corr_types[ct++] = CORR_SEVERITY_MATCH;
            }
            link->corr_type_count = ct;

            /* Temporal ordering suggests causal direction */
            if (temp_score > 0.3) {
                if (a->start_time < b->start_time) {
                    link->causal_direction = 1;   /* A precedes B */
                    link->is_causal = (temp_score > 0.7) ? 1 : 0;
                } else {
                    link->causal_direction = -1;  /* B precedes A */
                    link->is_causal = (temp_score > 0.7) ? 1 : 0;
                }
            }

            link->confidence = 0.5 + 0.5 * aggregate;  /* Heuristic */
            matrix->link_count++;
        }
    }

    return matrix->link_count;
}

/* ─── L5: Find Related Events ────────────────────────────────────────────── */

int corr_find_related(const corr_matrix_t *matrix, const ef_guid_t event_id,
                      const corr_link_t **results, int max_results) {
    if (!matrix || !results || max_results <= 0) return 0;

    int found = 0;

    /* Simple linear scan: collect all links involving event_id, sorted by strength */
    for (int i = 0; i < matrix->link_count && found < max_results; i++) {
        if (strcmp(matrix->links[i].event_a, event_id) == 0 ||
            strcmp(matrix->links[i].event_b, event_id) == 0) {
            results[found++] = &matrix->links[i];
        }
    }

    /* Bubble sort by strength descending (acceptable for small result sets) */
    for (int i = 0; i < found - 1; i++) {
        for (int j = i + 1; j < found; j++) {
            if (results[j]->strength > results[i]->strength) {
                corr_link_t *tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }
        }
    }

    return found;
}

/* ─── L5: Causal Chain Discovery (DFS) ─────────────────────────────────────
 *
 * Performs depth-first search on the correlation graph to find chains
 * of causally-linked events. Prioritizes links with:
 *   - is_causal == 1 (high confidence in causation)
 *   - causal_direction != 0 (directional information)
 *   - high strength and confidence
 *
 * Uses iterative DFS with explicit stack to avoid recursion limits.
 *
 * Complexity: O(V + E) per DFS traversal
 */

int corr_discover_chains(const corr_matrix_t *matrix,
                         causal_chain_t *chains, int max_chains) {
    if (!matrix || !chains || max_chains <= 0) return 0;

    int chain_count = 0;
    int visited[CORR_MAX_EVENTS] = {0};

    /* For each unvisited event, start a DFS to discover chains */
    for (int start = 0; start < matrix->event_count && chain_count < max_chains; start++) {
        if (visited[start]) continue;

        /* Check if this event has any strong causal incoming links */
        int has_incoming = 0;
        for (int l = 0; l < matrix->link_count; l++) {
            corr_link_t *link = &matrix->links[l];
            if (!link->is_causal) continue;
            /* Check if this event is event_b with causal_direction == 1 */
            if (strcmp(link->event_b, matrix->events[start]->id) == 0
                && link->causal_direction == 1) {
                has_incoming = 1;
                break;
            }
            if (strcmp(link->event_a, matrix->events[start]->id) == 0
                && link->causal_direction == -1) {
                has_incoming = 1;
                break;
            }
        }

        if (has_incoming) continue;  /* Not a root cause - skip for now */

        /* DFS from this root */
        causal_chain_t *chain = &chains[chain_count];
        memset(chain, 0, sizeof(causal_chain_t));

        int current = start;
        chain->event_ids[0][0] = '\0';
        strncpy(chain->event_ids[0], matrix->events[current]->id, 36);
        chain->event_ids[0][36] = '\0';
        chain->depth = 1;
        chain->total_duration_s = ef_duration_seconds(matrix->events[current]);

        /* Follow causal links forward */
        int extended = 1;
        while (extended && chain->depth < CAUSAL_MAX_DEPTH) {
            extended = 0;

            /* Find best outgoing causal link from current */
            double best_strength = 0.0;
            int best_target = -1;

            for (int l = 0; l < matrix->link_count; l++) {
                corr_link_t *link = &matrix->links[l];
                if (!link->is_causal) continue;

                int target_idx = -1;
                /* Current is event_a in the link */
                if (strcmp(link->event_a, matrix->events[current]->id) == 0
                    && link->causal_direction == 1) {
                    /* Find target index */
                    for (int t = 0; t < matrix->event_count; t++) {
                        if (strcmp(matrix->events[t]->id, link->event_b) == 0) {
                            target_idx = t;
                            break;
                        }
                    }
                }
                /* Current is event_b in the link */
                else if (strcmp(link->event_b, matrix->events[current]->id) == 0
                         && link->causal_direction == -1) {
                    for (int t = 0; t < matrix->event_count; t++) {
                        if (strcmp(matrix->events[t]->id, link->event_a) == 0) {
                            target_idx = t;
                            break;
                        }
                    }
                }

                if (target_idx >= 0 && !visited[target_idx]
                    && link->strength > best_strength) {
                    best_strength = link->strength;
                    best_target = target_idx;
                }
            }

            if (best_target >= 0) {
                visited[current] = 1;
                current = best_target;
                strncpy(chain->event_ids[chain->depth],
                        matrix->events[current]->id, 36);
                chain->event_ids[chain->depth][36] = '\0';
                chain->depth++;
                chain->cascade_strength += best_strength;
                chain->total_chain_duration_s =
                    ef_duration_seconds(matrix->events[current]);
                extended = 1;
            }
        }

        visited[current] = 1;

        if (chain->depth >= 2) {
            /* Only count as a chain if at least 2 events */
            chain->cascade_strength /= (chain->depth - 1);  /* Average link strength */
            chain_count++;
        }
    }

    return chain_count;
}

/* ─── L5: DBSCAN Temporal Clustering ────────────────────────────────────────
 *
 * Density-Based Spatial Clustering of Applications with Noise (DBSCAN)
 * adapted for 1D temporal data (event start times).
 *
 * Core concept:
 *   - eps (epsilon): maximum time distance for two events to be neighbors
 *   - min_pts: minimum events to form a dense cluster
 *   - Core point: has >= min_pts neighbors within eps
 *   - Border point: within eps of a core point, but not core itself
 *   - Noise point: neither core nor border
 *
 * Reference: Ester, M., Kriegel, H.P., Sander, J., Xu, X. (1996)
 *   "A Density-Based Algorithm for Discovering Clusters in Large Spatial
 *    Databases with Noise", KDD-96 Proceedings.
 *
 * Complexity: O(n^2) for naive implementation (acceptable for n <= CORR_MAX_EVENTS)
 */

typedef enum {
    DBSCAN_UNVISITED = 0,
    DBSCAN_NOISE     = 1,
    DBSCAN_CLUSTERED = 2
} dbscan_label_t;

int corr_dbscan_cluster(event_frame_t **events, int event_count,
                        double eps_s, int min_pts,
                        temporal_cluster_t *clusters, int max_clusters) {
    if (!events || event_count <= 0 || !clusters || max_clusters <= 0) return 0;
    if (event_count > CORR_MAX_EVENTS) event_count = CORR_MAX_EVENTS;

    int labels[CORR_MAX_EVENTS];
    for (int i = 0; i < event_count; i++) labels[i] = DBSCAN_UNVISITED;

    int cluster_count = 0;

    for (int i = 0; i < event_count && cluster_count < max_clusters; i++) {
        if (labels[i] != DBSCAN_UNVISITED) continue;

        /* Find neighbors (events within eps_s of event i) */
        int neighbors[CORR_MAX_EVENTS];
        int neighbor_count = 0;

        for (int j = 0; j < event_count; j++) {
            if (events[j]->status == EF_STATUS_INACTIVE) continue;
            double dist = fabs(difftime(events[i]->start_time, events[j]->start_time));
            if (dist <= eps_s) {
                neighbors[neighbor_count++] = j;
            }
        }

        if (neighbor_count < min_pts) {
            labels[i] = DBSCAN_NOISE;
            continue;
        }

        /* Start a new cluster */
        temporal_cluster_t *cluster = &clusters[cluster_count];
        memset(cluster, 0, sizeof(temporal_cluster_t));
        cluster->cluster_start = events[i]->start_time;
        cluster->cluster_end = events[i]->start_time;
        cluster->max_severity = events[i]->severity;

        /* Expand cluster */
        for (int n = 0; n < neighbor_count; n++) {
            int idx = neighbors[n];
            if (labels[idx] == DBSCAN_NOISE) {
                labels[idx] = DBSCAN_CLUSTERED;
                cluster->events[cluster->event_count++] = events[idx];
                if (events[idx]->start_time < cluster->cluster_start)
                    cluster->cluster_start = events[idx]->start_time;
                time_t ef_end = (events[idx]->status == EF_STATUS_ACTIVE)
                                ? time(NULL) : events[idx]->end_time;
                if (ef_end > cluster->cluster_end)
                    cluster->cluster_end = ef_end;
                if (events[idx]->severity > cluster->max_severity)
                    cluster->max_severity = events[idx]->severity;
            }
            if (labels[idx] != DBSCAN_UNVISITED) continue;

            labels[idx] = DBSCAN_CLUSTERED;
            cluster->events[cluster->event_count++] = events[idx];
            if (events[idx]->start_time < cluster->cluster_start)
                cluster->cluster_start = events[idx]->start_time;
            time_t ef_end = (events[idx]->status == EF_STATUS_ACTIVE)
                            ? time(NULL) : events[idx]->end_time;
            if (ef_end > cluster->cluster_end)
                cluster->cluster_end = ef_end;
            if (events[idx]->severity > cluster->max_severity)
                cluster->max_severity = events[idx]->severity;

            /* Expand: check neighbors of this neighbor (density-reachable) */
            for (int k = 0; k < event_count; k++) {
                if (labels[k] != DBSCAN_UNVISITED) continue;
                if (events[k]->status == EF_STATUS_INACTIVE) continue;
                double dist = fabs(difftime(events[idx]->start_time, events[k]->start_time));
                if (dist <= eps_s) {
                    /* Check if k is a core point (has >= min_pts neighbors) */
                    int kn_count = 0;
                    for (int m = 0; m < event_count && kn_count < min_pts; m++) {
                        if (events[m]->status == EF_STATUS_INACTIVE) continue;
                        double kd = fabs(difftime(events[k]->start_time, events[m]->start_time));
                        if (kd <= eps_s) kn_count++;
                    }
                    if (kn_count >= min_pts) {
                        /* Add k to neighbor expansion */
                        if (neighbor_count < CORR_MAX_EVENTS) {
                            neighbors[neighbor_count++] = k;
                        }
                    }
                }
            }
        }

        /* Finalize cluster */
        double span_s = difftime(cluster->cluster_end, cluster->cluster_start);
        cluster->density = (span_s > 0) ? cluster->event_count / span_s : 0.0;
        snprintf(cluster->label, 255, "Cluster-%d (%d events, %.0fs span)",
                 cluster_count + 1, cluster->event_count, span_s);

        cluster_count++;
    }

    return cluster_count;
}

/* ─── L5: Granger Causality Proxy ──────────────────────────────────────────
 *
 * Tests whether events of template A tend to precede events of template B
 * within a lag window, more often than would be expected by chance.
 *
 * Method:
 *   1. For each event of type A, count how many type B events occur within
 *      the look-ahead window [t_A_start, t_A_start + lag_window_s].
 *   2. Compute the ratio: (A→B count) / (total B count).
 *   3. Compare against null hypothesis (uniform random distribution).
 *   4. Approximate p-value using binomial test.
 *
 * This is a practical proxy for Granger causality, not the full
 * autoregressive test, but it works on discrete event streams where
 * the full Granger test would require continuous time series.
 *
 * Reference: Granger, C.W.J. (1969) "Investigating Causal Relations by
 *   Econometric Models and Cross-spectral Methods", Econometrica
 *
 * Complexity: O(n) where n = total event count
 */

int corr_granger_proxy(event_frame_t **events, int count,
                       const char *tmpl_a, const char *tmpl_b,
                       double lag_window_s, double *p_value) {
    if (!events || count <= 0 || !tmpl_a || !tmpl_b || !p_value) return 0;

    /* Collect events of each type */
    int idx_a[CORR_MAX_EVENTS], idx_b[CORR_MAX_EVENTS];
    int count_a = 0, count_b = 0;

    for (int i = 0; i < count; i++) {
        if (events[i]->status == EF_STATUS_INACTIVE) continue;
        if (strcmp(events[i]->template_name, tmpl_a) == 0 && count_a < CORR_MAX_EVENTS) {
            idx_a[count_a++] = i;
        }
        if (strcmp(events[i]->template_name, tmpl_b) == 0 && count_b < CORR_MAX_EVENTS) {
            idx_b[count_b++] = i;
        }
    }

    if (count_a == 0 || count_b == 0) {
        *p_value = 1.0;
        return 0;
    }

    /* Count B events that follow A events within lag window */
    int a_causes_b = 0;
    for (int i = 0; i < count_a; i++) {
        time_t t_a = events[idx_a[i]]->start_time;
        for (int j = 0; j < count_b; j++) {
            time_t t_b = events[idx_b[j]]->start_time;
            double dt = difftime(t_b, t_a);
            if (dt >= 0.0 && dt <= lag_window_s) {
                a_causes_b++;
            }
        }
    }

    /* Null hypothesis: B events randomly distributed in time.
     * Expected probability of B falling in window = lag_window_s / total_span */
    time_t t_min = events[0]->start_time, t_max = events[0]->start_time;
    for (int i = 0; i < count; i++) {
        if (events[i]->status == EF_STATUS_INACTIVE) continue;
        if (events[i]->start_time < t_min) t_min = events[i]->start_time;
        if (events[i]->start_time > t_max) t_max = events[i]->start_time;
    }
    double total_span_s = difftime(t_max, t_min);
    if (total_span_s <= 0.0) total_span_s = 1.0;

    double expected_prob = lag_window_s / total_span_s;
    if (expected_prob > 1.0) expected_prob = 1.0;

    /* Expected count = count_a * count_b * expected_prob */
    double expected_count = count_a * count_b * expected_prob;

    /* Simple significance test: is observed >> expected? */
    double ratio = (expected_count > 0) ? (double)a_causes_b / expected_count : 0.0;

    /* Approximate p-value using Poisson: P(X >= observed | lambda = expected) */
    double p = 1.0;
    if (expected_count > 0.0) {
        double lambda = expected_count;
        double poisson_cdf = exp(-lambda);
        double term = 1.0;
        for (int k = 1; k <= a_causes_b; k++) {
            term *= lambda / k;
            poisson_cdf += term * exp(-lambda);
        }
        p = 1.0 - poisson_cdf + term * exp(-lambda);
        if (p < 0.0) p = 0.0;
        if (p > 1.0) p = 1.0;
    }

    *p_value = p;
    return (ratio > 2.0 && p < 0.05) ? 1 : 0;
}

/* ─── L5: Correlation Summary ────────────────────────────────────────────── */

int corr_summary(const corr_matrix_t *matrix, char *buf, size_t buf_size) {
    if (!matrix || !buf || buf_size == 0) return 0;

    double avg_strength = 0.0;
    int causal_count = 0;
    for (int i = 0; i < matrix->link_count; i++) {
        avg_strength += matrix->links[i].strength;
        if (matrix->links[i].is_causal) causal_count++;
    }
    if (matrix->link_count > 0) avg_strength /= matrix->link_count;

    return snprintf(buf, buf_size,
                    "Correlation Matrix: %d events, %d links, "
                    "avg strength=%.3f, %d causal links, window=%.0fs",
                    matrix->event_count, matrix->link_count,
                    avg_strength, causal_count, matrix->time_window_s);
}
