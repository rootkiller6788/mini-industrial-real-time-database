/**
 * @file af_search.h
 * @brief PI Asset Framework - AF Search API
 *
 * Provides search and query capabilities across the AF hierarchy.
 * Supports searching by name patterns, template type, category,
 * attribute values, and path expressions.
 *
 * L5 Algorithms:
 *   - DFS search with predicate filtering
 *   - Wildcard pattern matching (fnmatch-like)
 *   - Compound search criteria with AND/OR logic
 */

#ifndef AF_SEARCH_H
#define AF_SEARCH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "af_element.h"
#include "af_template.h"

#define AF_MAX_SEARCH_RESULTS  1024

/* ─── Search Criteria ────────────────────────────────────────── */
typedef enum {
    AF_SEARCH_BY_NAME      = 0x01,
    AF_SEARCH_BY_TEMPLATE  = 0x02,
    AF_SEARCH_BY_CATEGORY  = 0x04,
    AF_SEARCH_BY_PATH      = 0x08,
    AF_SEARCH_BY_TYPE      = 0x10,
    AF_SEARCH_BY_ATTR_VALUE= 0x20
} af_search_field_t;

typedef enum {
    AF_SEARCH_MATCH_EXACT   = 0,
    AF_SEARCH_MATCH_PREFIX  = 1,
    AF_SEARCH_MATCH_SUFFIX  = 2,
    AF_SEARCH_MATCH_CONTAINS= 3,
    AF_SEARCH_MATCH_WILDCARD= 4  /* Supports * and ? wildcards */
} af_match_mode_t;

typedef struct {
    af_search_field_t  fields;        /* Bitmask of fields to search */
    af_match_mode_t    match_mode;    /* How to match text fields */

    char    name_pattern[AF_MAX_NAME_LEN];
    char    template_name[AF_MAX_TMPL_NAME_LEN];
    char    category_name[AF_MAX_CAT_NAME_LEN];
    char    path_pattern[AF_MAX_PATH_LEN];
    af_element_type_t  element_type;
    char    attr_name[AF_MAX_ATTR_NAME_LEN];
    bool    has_attr_filter;

    /** Limit on number of results (0 = no limit up to AF_MAX_SEARCH_RESULTS) */
    int     max_results;
} af_search_criteria_t;

/* ─── Search Result ──────────────────────────────────────────── */
typedef struct {
    AFElement *element;
    char       path[AF_MAX_PATH_LEN];
    double     relevance_score;  /* 0.0 - 1.0 relevance ranking */
} af_search_result_t;

typedef struct {
    af_search_result_t results[AF_MAX_SEARCH_RESULTS];
    int    count;
    int    total_matched;  /* May be > count if truncated */
} af_search_result_set_t;

/* ─── Search API ──────────────────────────────────────────────── */
/**
 * Search the AF hierarchy for elements matching criteria.
 * Performs a DFS traversal of the subtree, testing each element
 * against the search criteria.
 *
 * @param root Root element to start search from
 * @param criteria Search parameters
 * @param results Output result set
 * @return Number of results found (may be truncated)
 *
 * Complexity: O(N * F) where N = elements in subtree,
 *             F = cost of predicate evaluation
 */
int af_search_find(const AFElement *root,
                    const af_search_criteria_t *criteria,
                    af_search_result_set_t *results);

/**
 * Initialize a search criteria structure with default values.
 */
void af_search_criteria_init(af_search_criteria_t *crit);

/**
 * Set search by name pattern.
 */
void af_search_set_name(af_search_criteria_t *crit, const char *pattern);

/**
 * Set search by template name.
 */
void af_search_set_template(af_search_criteria_t *crit, const char *tmpl_name);

/**
 * Set search by category.
 */
void af_search_set_category(af_search_criteria_t *crit, const char *cat_name);

/**
 * Set search by element type.
 */
void af_search_set_type(af_search_criteria_t *crit, af_element_type_t type);

/**
 * Set search by attribute value range (attribute with numeric value).
 */
void af_search_set_attr_filter(af_search_criteria_t *crit,
                                const char *attr_name);

/* ─── Result Utilities ───────────────────────────────────────── */
/** Sort results by relevance score (descending). Stable sort. */
void af_search_sort_by_relevance(af_search_result_set_t *results);

/** Sort results by element path (ascending). */
void af_search_sort_by_path(af_search_result_set_t *results);

/** Get the Nth result element (NULL if out of bounds). */
AFElement* af_search_get_result(const af_search_result_set_t *results, int n);

/* ─── Path Matching ──────────────────────────────────────────── */
/**
 * Match a path string against a wildcard pattern.
 * Supports * (matches any characters) and ? (matches single character).
 * @param path Path string to test
 * @param pattern Pattern with wildcards
 * @return true if path matches pattern
 */
bool af_search_path_match(const char *path, const char *pattern);

/**
 * Match a name string against a pattern with configured match mode.
 */
bool af_search_name_match(const char *name, const char *pattern,
                           af_match_mode_t mode);

#endif /* AF_SEARCH_H */
