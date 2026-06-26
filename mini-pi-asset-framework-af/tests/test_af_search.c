/**
 * test_af_search.c - Unit tests for AF Search
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/af_search.h"
#include "../include/af_element.h"
#include "../include/af_template.h"
#include "../include/af_category.h"

static int tr = 0, tp = 0;
#define T(n) do { tr++; printf("  TEST: %s... ", n); } while(0)
#define P() do { printf("PASS\n"); tp++; } while(0)

int main(void)
{
    printf("=== AF Search Tests ===\n");

    /* Build test hierarchy */
    AFElement *enterprise = af_element_create("ChemCorp", AF_ELEM_TYPE_ENTERPRISE);
    AFElement *site = af_element_create("Houston", AF_ELEM_TYPE_SITE);
    AFElement *area = af_element_create("PolymerArea", AF_ELEM_TYPE_AREA);
    AFElement *unit = af_element_create("ReactorUnit1", AF_ELEM_TYPE_UNIT);
    AFElement *eq1 = af_element_create("Reactor1A", AF_ELEM_TYPE_EQ_MODULE);
    AFElement *eq2 = af_element_create("Reactor1B", AF_ELEM_TYPE_EQ_MODULE);
    AFElement *pump = af_element_create("Pump101", AF_ELEM_TYPE_EQ_MODULE);

    af_element_add_child(enterprise, site);
    af_element_add_child(site, area);
    af_element_add_child(area, unit);
    af_element_add_child(unit, eq1);
    af_element_add_child(unit, eq2);
    af_element_add_child(unit, pump);

    /* Test name search - exact */
    T("name_exact");
    af_search_criteria_t crit;
    af_search_criteria_init(&crit);
    af_search_set_name(&crit, "Reactor1A");
    crit.match_mode = AF_SEARCH_MATCH_EXACT;
    af_search_result_set_t results;
    int n = af_search_find(enterprise, &crit, &results);
    assert(n == 1);
    assert(results.results[0].element == eq1);
    P();

    /* Test name search - wildcard */
    T("name_wildcard");
    af_search_criteria_init(&crit);
    af_search_set_name(&crit, "Reactor1*");
    crit.match_mode = AF_SEARCH_MATCH_WILDCARD;
    n = af_search_find(enterprise, &crit, &results);
    assert(n == 2); /* Reactor1A, Reactor1B */
    P();

    /* Test name search - contains */
    T("name_contains");
    af_search_criteria_init(&crit);
    af_search_set_name(&crit, "Pump");
    crit.match_mode = AF_SEARCH_MATCH_CONTAINS;
    n = af_search_find(enterprise, &crit, &results);
    assert(n == 1);
    assert(results.results[0].element == pump);
    P();

    /* Test type search */
    T("type_search");
    af_search_criteria_init(&crit);
    af_search_set_type(&crit, AF_ELEM_TYPE_EQ_MODULE);
    n = af_search_find(enterprise, &crit, &results);
    assert(n == 3);
    P();

    /* Test path match */
    T("path_match");
    assert(af_search_path_match("/hello/world", "*/world"));
    assert(!af_search_path_match("/hello/world", "*/foo"));
    P();

    /* Test name match modes */
    T("match_modes");
    assert(af_search_name_match("Reactor1A", "Reactor", AF_SEARCH_MATCH_PREFIX));
    assert(af_search_name_match("Reactor1A", "1A", AF_SEARCH_MATCH_SUFFIX));
    assert(af_search_name_match("Reactor1A", "tor", AF_SEARCH_MATCH_CONTAINS));
    assert(af_search_name_match("Reactor1A", "Reac*1A", AF_SEARCH_MATCH_WILDCARD));
    P();

    /* Test sort */
    T("sort");
    af_search_criteria_init(&crit);
    af_search_set_name(&crit, "Reactor*");
    crit.match_mode = AF_SEARCH_MATCH_WILDCARD;
    af_search_find(enterprise, &crit, &results);
    af_search_sort_by_path(&results);
    assert(results.count >= 2);
    P();

    /* Test get result */
    T("get_result");
    assert(af_search_get_result(&results, 0) != NULL);
    assert(af_search_get_result(&results, 999) == NULL);
    P();

    af_element_destroy(enterprise);
    printf("\n=== Results: %d/%d tests passed ===\n", tp, tr);
    return tp == tr ? 0 : 1;
}
