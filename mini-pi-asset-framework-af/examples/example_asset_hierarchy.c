/**
 * example_asset_hierarchy.c - ISA-95 equipment hierarchy demo
 * L6: Industrial asset modeling in PI AF
 */
#include <stdio.h>
#include <string.h>
#include "../include/af_element.h"
#include "../include/af_attribute.h"
#include "../include/af_category.h"

int main(void)
{
    printf("=== PI AF Asset Hierarchy (ISA-95) ===\n\n");
    AFElement *ent = af_element_create("GlobalChem", AF_ELEM_TYPE_ENTERPRISE);
    af_element_set_description(ent, "Global Chemicals Corporation");
    AFElement *site = af_element_create("TexasPlant", AF_ELEM_TYPE_SITE);
    AFElement *area = af_element_create("OlefinsArea", AF_ELEM_TYPE_AREA);
    AFElement *unit = af_element_create("CrackerUnit1", AF_ELEM_TYPE_UNIT);
    af_element_set_description(unit, "Ethylene Cracker 500KTA");
    AFElement *eq1 = af_element_create("Furnace1A", AF_ELEM_TYPE_EQ_MODULE);
    AFElement *eq2 = af_element_create("QuenchTower1", AF_ELEM_TYPE_EQ_MODULE);
    af_element_add_child(ent, site);
    af_element_add_child(site, area);
    af_element_add_child(area, unit);
    af_element_add_child(unit, eq1);
    af_element_add_child(unit, eq2);

    AFAttribute *temp = af_attribute_create("Temperature", AF_VAL_FLOAT64);
    af_attribute_set_uom(temp, "degC");
    af_value_t tv = { .type = AF_VAL_FLOAT64, .value.v_float64 = 850.0, .is_good = true };
    af_attribute_set_value(temp, &tv);
    af_element_add_attribute(eq1, temp);

    printf("Hierarchy Tree:\n");
    printf("  Enterprise: %s\n", ent->name);
    printf("    Site:     %s\n", site->name);
    printf("      Area:   %s\n", area->name);
    printf("        Unit: %s (%s)\n", unit->name, unit->description);
    for (size_t i = 0; i < unit->child_count; i++) {
        printf("          - %s\n", unit->children[i]->name);
        for (size_t j = 0; j < unit->children[i]->attr_count; j++) {
            AFAttribute *a = unit->children[i]->attributes[j];
            char vs[64];
            af_attribute_value_to_string(a, vs, sizeof(vs));
            printf("            %s: %s %s\n", a->name, vs, a->uom);
        }
    }

    char path[1024];
    af_element_get_path(eq1, path, sizeof(path));
    printf("\nPath of Furnace1A: %s\n", path);
    printf("Subtree under Enterprise: %zu elements\n", af_element_subtree_count(ent));
    printf("Depth of Furnace1A: %d\n", af_element_get_depth(eq1));

    AFCategory *cat = af_category_create("FiredEquipment");
    af_element_add_category(eq1, cat);
    printf("Has FiredEquipment category: %s\n",
           af_element_has_category(eq1, cat) ? "yes" : "no");

    af_element_destroy(ent);
    af_category_destroy(cat);
    printf("\nHierarchy demo complete.\n");
    return 0;
}
