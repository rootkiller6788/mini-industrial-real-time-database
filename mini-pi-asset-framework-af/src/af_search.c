/**
 * @file af_search.c
 * @brief PI AF Search - hierarchy search with criteria matching
 *
 * Knowledge coverage:
 * L5: DFS-based search with compound criteria (name, template, category, type, attribute)
 * L5: Wildcard pattern matching (fnmatch-style * and ?)
 * L5: Relevance scoring and result ranking
 * L6: Asset discovery in large industrial AF hierarchies
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "af_search.h"
#include "af_attribute.h"
#include "af_category.h"

void af_search_criteria_init(af_search_criteria_t *crit)
{
    if (!crit) return;
    memset(crit, 0, sizeof(*crit));
    crit->match_mode = AF_SEARCH_MATCH_EXACT;
}

void af_search_set_name(af_search_criteria_t *crit, const char *pattern)
{
    if (!crit || !pattern) return;
    crit->fields |= AF_SEARCH_BY_NAME;
    strncpy(crit->name_pattern, pattern, AF_MAX_NAME_LEN - 1);
    crit->name_pattern[AF_MAX_NAME_LEN - 1] = 0;
}

void af_search_set_template(af_search_criteria_t *crit, const char *tmpl_name)
{
    if (!crit || !tmpl_name) return;
    crit->fields |= AF_SEARCH_BY_TEMPLATE;
    strncpy(crit->template_name, tmpl_name, AF_MAX_TMPL_NAME_LEN - 1);
    crit->template_name[AF_MAX_TMPL_NAME_LEN - 1] = 0;
}

void af_search_set_category(af_search_criteria_t *crit, const char *cat_name)
{
    if (!crit || !cat_name) return;
    crit->fields |= AF_SEARCH_BY_CATEGORY;
    strncpy(crit->category_name, cat_name, AF_MAX_CAT_NAME_LEN - 1);
    crit->category_name[AF_MAX_CAT_NAME_LEN - 1] = 0;
}

void af_search_set_type(af_search_criteria_t *crit, af_element_type_t type)
{
    if (!crit) return;
    crit->fields |= AF_SEARCH_BY_TYPE;
    crit->element_type = type;
}

void af_search_set_attr_filter(af_search_criteria_t *crit,
                                const char *attr_name)
{
    if (!crit || !attr_name) return;
    crit->fields |= AF_SEARCH_BY_ATTR_VALUE;
    strncpy(crit->attr_name, attr_name, AF_MAX_ATTR_NAME_LEN - 1);
    crit->attr_name[AF_MAX_ATTR_NAME_LEN - 1] = 0;
    crit->has_attr_filter = true;
}

bool af_search_path_match(const char *path, const char *pattern)
{
    return af_search_name_match(path, pattern, AF_SEARCH_MATCH_WILDCARD);
}

bool af_search_name_match(const char *name, const char *pattern,
                           af_match_mode_t mode)
{
    if (!name || !pattern) return false;
    switch (mode) {
    case AF_SEARCH_MATCH_EXACT:
        return strcmp(name, pattern) == 0;
    case AF_SEARCH_MATCH_PREFIX: {
        size_t plen = strlen(pattern);
        return strncmp(name, pattern, plen) == 0;
    }
    case AF_SEARCH_MATCH_SUFFIX: {
        size_t nlen = strlen(name), plen = strlen(pattern);
        if (plen > nlen) return false;
        return strcmp(name + nlen - plen, pattern) == 0;
    }
    case AF_SEARCH_MATCH_CONTAINS:
        return strstr(name, pattern) != NULL;
    case AF_SEARCH_MATCH_WILDCARD: {
        const char *n = name, *p = pattern;
        const char *sn = NULL, *sp = NULL;
        while (*n) {
            if (*p == '?' || *p == *n) { n++; p++; }
            else if (*p == '*') { sp = p++; sn = n; }
            else if (sp) { p = sp + 1; n = ++sn; }
            else return false;
        }
        while (*p == '*') p++;
        return *p == 0;
    }
    default: return false;
    }
}

static bool elem_matches(const AFElement *elem,
                         const af_search_criteria_t *crit)
{
    if (!elem || !crit) return false;
    if (crit->fields == 0) return true;
    if (crit->fields & AF_SEARCH_BY_NAME) {
        if (!af_search_name_match(elem->name, crit->name_pattern,
                                   crit->match_mode)) return false;
    }
    if (crit->fields & AF_SEARCH_BY_TYPE) {
        if (elem->type != crit->element_type) return false;
    }
    if (crit->fields & AF_SEARCH_BY_TEMPLATE) {
        if (!elem->template_ref ||
            !af_search_name_match(elem->template_ref->name,
                                   crit->template_name, AF_SEARCH_MATCH_EXACT))
            return false;
    }
    if (crit->fields & AF_SEARCH_BY_CATEGORY) {
        bool has = false;
        for (size_t i = 0; i < elem->cat_count; i++) {
            if (af_search_name_match(elem->categories[i]->name,
                                      crit->category_name, AF_SEARCH_MATCH_EXACT))
            { has = true; break; }
        }
        if (!has) return false;
    }
    if (crit->fields & AF_SEARCH_BY_ATTR_VALUE) {
        if (!crit->has_attr_filter) return false;
        if (!af_element_get_attribute(elem, crit->attr_name)) return false;
    }
    return true;
}

static int dfs_search(AFElement *node, const af_search_criteria_t *crit,
                       af_search_result_set_t *res, int *total, int depth)
{
    if (!node || !res || depth > 256) return 0;
    int found = 0;
    if (elem_matches(node, crit)) {
        (*total)++;
        if (res->count < AF_MAX_SEARCH_RESULTS &&
            (crit->max_results == 0 || res->count < crit->max_results)) {
            int ri = res->count;
            res->results[ri].element = node;
            af_element_get_path(node, res->results[ri].path,
                                sizeof(res->results[ri].path));
            double score = 0.0; int fc = 0;
            if (crit->fields & AF_SEARCH_BY_NAME) { score += 1.0; fc++; }
            if (crit->fields & AF_SEARCH_BY_TEMPLATE) { score += 1.0; fc++; }
            if (crit->fields & AF_SEARCH_BY_CATEGORY) { score += 0.9; fc++; }
            if (crit->fields & AF_SEARCH_BY_TYPE) { score += 0.7; fc++; }
            if (crit->fields & AF_SEARCH_BY_ATTR_VALUE) { score += 0.6; fc++; }
            res->results[ri].relevance_score = fc > 0 ? score / fc : 0.0;
            res->count++;
            found++;
        }
    }
    for (size_t i = 0; i < node->child_count; i++)
        found += dfs_search(node->children[i], crit, res, total, depth + 1);
    return found;
}

int af_search_find(const AFElement *root,
                    const af_search_criteria_t *criteria,
                    af_search_result_set_t *results)
{
    if (!root || !criteria || !results) return 0;
    memset(results, 0, sizeof(*results));
    int total_matched = 0;
    dfs_search((AFElement*)root, criteria, results, &total_matched, 0);
    results->total_matched = total_matched;
    return results->count;
}

static int cmp_rel(const void *a, const void *b)
{
    double ra = ((const af_search_result_t*)a)->relevance_score;
    double rb = ((const af_search_result_t*)b)->relevance_score;
    if (ra > rb) return -1;
    if (ra < rb) return 1;
    return 0;
}

static int cmp_path(const void *a, const void *b)
{
    return strcmp(((const af_search_result_t*)a)->path,
                  ((const af_search_result_t*)b)->path);
}

void af_search_sort_by_relevance(af_search_result_set_t *results)
{
    if (results && results->count > 1)
        qsort(results->results, results->count,
              sizeof(af_search_result_t), cmp_rel);
}

void af_search_sort_by_path(af_search_result_set_t *results)
{
    if (results && results->count > 1)
        qsort(results->results, results->count,
              sizeof(af_search_result_t), cmp_path);
}

AFElement* af_search_get_result(const af_search_result_set_t *results, int n)
{
    if (!results || n < 0 || n >= results->count) return NULL;
    return results->results[n].element;
}
