/**
 * test_af_element.c - Unit tests for AFElement
 * Tests: create, destroy, hierarchy, path, traversal
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/af_element.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  TEST: %s... ", name); } while(0)
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

int main(void)
{
    printf("=== AFElement Tests ===\n");

    /* Create element */
    TEST("create");
    AFElement *e = af_element_create("Reactor1", AF_ELEM_TYPE_UNIT);
    assert(e != NULL);
    assert(strcmp(e->name, "Reactor1") == 0);
    assert(e->type == AF_ELEM_TYPE_UNIT);
    assert(e->parent == NULL);
    assert(e->child_count == 0);
    PASS();

    /* Set description */
    TEST("set_description");
    assert(af_element_set_description(e, "Main reactor vessel"));
    assert(strcmp(e->description, "Main reactor vessel") == 0);
    PASS();

    /* Depth of root element */
    TEST("depth_root");
    assert(af_element_get_depth(e) == 0);
    PASS();

    /* Root traversal */
    TEST("get_root");
    assert(af_element_get_root(e) == e);
    PASS();

    /* Path computation */
    TEST("get_path");
    char path[1024];
    int plen = af_element_get_path(e, path, sizeof(path));
    assert(plen > 0);
    printf("(%s) ", path);
    PASS();

    /* Create child */
    TEST("add_child");
    AFElement *child = af_element_create("Pump1", AF_ELEM_TYPE_EQ_MODULE);
    assert(child != NULL);
    assert(af_element_add_child(e, child));
    assert(e->child_count == 1);
    assert(child->parent == e);
    PASS();

    /* Find child */
    TEST("find_child");
    AFElement *found = af_element_find_child(e, "Pump1");
    assert(found == child);
    assert(af_element_find_child(e, "Nonexistent") == NULL);
    PASS();

    /* Child depth */
    TEST("child_depth");
    assert(af_element_get_depth(child) == 1);
    PASS();

    /* Child path */
    TEST("child_path");
    plen = af_element_get_path(child, path, sizeof(path));
    assert(plen > 0);
    printf("(%s) ", path);
    PASS();

    /* Is descendant */
    TEST("is_descendant");
    assert(af_element_is_descendant(e, child));
    assert(!af_element_is_descendant(child, e));
    PASS();

    /* Find by path */
    TEST("find_by_path");
    AFElement *by_path = af_element_find_by_path(e, path);
    assert(by_path == child);
    PASS();

    /* Remove child */
    TEST("remove_child");
    assert(af_element_remove_child(child));
    assert(child->parent == NULL);
    assert(e->child_count == 0);
    PASS();

    /* Re-add and create grandchild */
    TEST("grandchild");
    af_element_add_child(e, child);
    AFElement *grandchild = af_element_create("Sensor1", AF_ELEM_TYPE_CTRL_MODULE);
    af_element_add_child(child, grandchild);
    assert(af_element_get_depth(grandchild) == 2);
    assert(af_element_is_descendant(e, grandchild));
    assert(af_element_subtree_count(e) == 3);
    PASS();

    /* Relative path */
    TEST("relative_path");
    char rel[1024];
    int rlen = af_element_get_relative_path(e, grandchild, rel, sizeof(rel));
    assert(rlen > 0);
    printf("(%s) ", rel);
    PASS();

    /* Destroy cleans up subtree */
    TEST("destroy");
    af_element_destroy(e);
    /* No assert needed - valgrind would catch leaks */
    PASS();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
