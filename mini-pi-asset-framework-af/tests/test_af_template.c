/**
 * test_af_template.c - Unit tests for AFTemplate
 * Tests: create, attribute templates, inheritance, instantiation
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/af_template.h"
#include "../include/af_element.h"

static int tests_run = 0, tests_passed = 0;
#define TEST(n) do { tests_run++; printf("  TEST: %s... ", n); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)

int main(void)
{
    printf("=== AFTemplate Tests ===\n");

    TEST("create");
    AFTemplate *tmpl = af_template_create("ReactorTemplate");
    assert(tmpl != NULL);
    assert(strcmp(tmpl->name, "ReactorTemplate") == 0);
    assert(tmpl->version == 1);
    PASS();

    TEST("add_attr");
    assert(af_template_add_attr(tmpl, "Temperature", AF_VAL_FLOAT64, "degC"));
    assert(af_template_add_attr(tmpl, "Pressure", AF_VAL_FLOAT64, "bar"));
    assert(af_template_add_attr(tmpl, "Status", AF_VAL_INT32, ""));
    assert(tmpl->attr_tmpl_count == 3);
    PASS();

    TEST("find_attr");
    const af_attr_template_t *at = af_template_find_attr(tmpl, "Temperature");
    assert(at != NULL);
    assert(at->value_type == AF_VAL_FLOAT64);
    assert(strcmp(at->uom, "degC") == 0);
    assert(af_template_find_attr(tmpl, "Nonexistent") == NULL);
    PASS();

    TEST("set_attr_default");
    af_value_t def_val = { .type = AF_VAL_FLOAT64, .value.v_float64 = 25.0 };
    assert(af_template_set_attr_default(tmpl, "Temperature", &def_val));
    at = af_template_find_attr(tmpl, "Temperature");
    assert(at->has_default);
    assert(at->default_value.value.v_float64 == 25.0);
    PASS();

    TEST("remove_attr");
    assert(af_template_remove_attr(tmpl, "Status"));
    assert(tmpl->attr_tmpl_count == 2);
    assert(af_template_find_attr(tmpl, "Status") == NULL);
    PASS();

    TEST("base_template");
    AFTemplate *base = af_template_create("BaseTemplate");
    af_template_add_attr(base, "AssetID", AF_VAL_STRING, "");
    af_template_add_attr(base, "Location", AF_VAL_STRING, "");
    assert(af_template_set_base(tmpl, base));
    assert(tmpl->base_template == base);
    PASS();

    TEST("resolve_inherited_attr");
    const af_attr_template_t *inh = af_template_resolve_attr(tmpl, "AssetID");
    assert(inh != NULL);
    assert(strcmp(inh->name, "AssetID") == 0);
    /* Local attribute overrides inherited */
    inh = af_template_resolve_attr(tmpl, "Temperature");
    assert(inh != NULL);
    PASS();

    TEST("is_descendant");
    assert(af_template_is_descendant(tmpl, base));
    assert(!af_template_is_descendant(base, tmpl));
    PASS();

    TEST("cycle_detection");
    assert(!af_template_set_base(base, tmpl));
    PASS();

    TEST("list_effective_attrs");
    char names[256][256];
    size_t count = 0;
    size_t total = af_template_list_effective_attrs(tmpl, names, &count);
    assert(total >= 3);
    PASS();

    TEST("instantiate");
    AFElement *elem = af_template_instantiate(tmpl, "Reactor_R101");
    assert(elem != NULL);
    assert(elem->is_template_derived);
    assert(elem->template_ref == tmpl);
    /* Check that attributes were created */
    AFAttribute *temp = af_element_get_attribute(elem, "Temperature");
    assert(temp != NULL);
    af_element_destroy(elem);
    PASS();

    TEST("version");
    assert(af_template_get_version(tmpl) == 1);
    af_template_bump_version(tmpl);
    assert(af_template_get_version(tmpl) == 2);
    PASS();

    TEST("validate");
    assert(af_template_validate(tmpl) == 0);
    PASS();

    af_template_destroy(tmpl);
    af_template_destroy(base);
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
