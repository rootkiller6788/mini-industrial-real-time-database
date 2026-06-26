/**
 * @file event_correlation.h
 * @brief Event Frame correlation analysis - causal and temporal relationships
 *
 * Algorithms to discover correlations between Event Frames: temporal proximity,
 * causal chains, common-cause analysis, and statistical correlation measures.
 *
 * Knowledge Levels:
 *   L1 Definitions:  Correlation matrix, causality chain, correlation type
 *   L2 Core Concepts: Causal discovery, temporal clustering, Granger causality
 *   L5 Algorithms:    DBSCAN temporal clustering, cross-correlation, Jaccard index
 *   L6 Canonical:     Cascade fault analysis, root cause identification
 *   L8 Advanced:      Transfer entropy, Bayesian network event modeling
 *
 * MIT 6.302 - Correlation analysis for fault diagnosis
 * CMU 24-677 - Causal reasoning in event systems
 */

#ifndef EVENT_CORRELATION_H
#define EVENT_CORRELATION_H

#include "event_frame.h"
#include <stdint.h>
#include <stddef.h>
#include <time.h>

typedef enum {
    CORR_TEMPORAL_PROXIMITY  = 0,
    CORR_CAUSAL_CHAIN        = 1,
    CORR_COMMON_CAUSE        = 2,
    CORR_ASSET_AFFINITY      = 3,
    CORR_SEVERITY_MATCH      = 4,
    CORR_TEMPLATE_FAMILY     = 5,
    CORR_ATTRIBUTE_SIMILARITY = 6,
    CORR_SPATIAL_PROXIMITY   = 7
} corr_type_t;

typedef struct {
    ef_guid_t    event_a;
    ef_guid_t    event_b;
    corr_type_t  corr_types[4];
    int          corr_type_count;
    double       strength;
    double       time_proximity_s;
    double       confidence;
    int          is_causal;
    int          causal_direction;
} corr_link_t;

#define CORR_MAX_EVENTS     256
#define CORR_MAX_LINKS      1024

typedef struct {
    event_frame_t  *events[CORR_MAX_EVENTS];
    int             event_count;
    corr_link_t     links[CORR_MAX_LINKS];
    int             link_count;
    time_t          analysis_time;
    double          time_window_s;
} corr_matrix_t;

#define CAUSAL_MAX_DEPTH    32

typedef struct {
    ef_guid_t    event_ids[CAUSAL_MAX_DEPTH];
    int          depth;
    double       total_duration_s;
    double       total_chain_duration_s;
    double       cascade_strength;
    char         root_cause_desc[512];
} causal_chain_t;

typedef struct {
    event_frame_t *events[CORR_MAX_EVENTS];
    int            event_count;
    time_t         cluster_start;
    time_t         cluster_end;
    double         density;
    ef_severity_t  max_severity;
    char           label[256];
} temporal_cluster_t;

double corr_temporal_proximity(const event_frame_t *a, const event_frame_t *b, double max_window_s);
double corr_attribute_jaccard(const event_frame_t *a, const event_frame_t *b);
int corr_build_matrix(corr_matrix_t *matrix, event_frame_t **events, int event_count, double time_window_s);
int corr_find_related(const corr_matrix_t *matrix, const ef_guid_t event_id,
                      const corr_link_t **results, int max_results);
int corr_discover_chains(const corr_matrix_t *matrix, causal_chain_t *chains, int max_chains);
int corr_dbscan_cluster(event_frame_t **events, int event_count,
                        double eps_s, int min_pts,
                        temporal_cluster_t *clusters, int max_clusters);
int corr_granger_proxy(event_frame_t **events, int count,
                       const char *tmpl_a, const char *tmpl_b,
                       double lag_window_s, double *p_value);
int corr_summary(const corr_matrix_t *matrix, char *buf, size_t buf_size);

#endif
