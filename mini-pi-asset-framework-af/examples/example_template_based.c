/**
 * example_template_based.c - Template-based asset creation demo
 * L6: Repetitive equipment modeling with PI AF templates
 */
#include <stdio.h>
#include <string.h>
#include "../include/af_template.h"
#include "../include/af_element.h"
#include "../include/af_attribute.h"

int main(void)
{
    printf("=== PI AF Template-Based Asset Creation ===\n\n");

    AFTemplate *base_eq = af_template_create("BaseEquipment");
    af_template_set_description(base_eq, "Base template for all equipment");
    af_template_add_attr(base_eq, "AssetID", AF_VAL_STRING, "");
    af_template_add_attr(base_eq, "Manufacturer", AF_VAL_STRING, "");
    printf("Base template: %s (%zu attrs)\n", base_eq->name, base_eq->attr_tmpl_count);

    AFTemplate *reactor = af_template_create("ReactorTemplate");
    af_template_set_base(reactor, base_eq);
    af_template_add_attr(reactor, "Temperature", AF_VAL_FLOAT64, "degC");
    af_template_add_attr(reactor, "Pressure", AF_VAL_FLOAT64, "bar");
    af_value_t def_t = { .type = AF_VAL_FLOAT64, .value.v_float64 = 25.0 };
    af_template_set_attr_default(reactor, "Temperature", &def_t);

    printf("Reactor template: %s (inherits from %s)\n",
           reactor->name, reactor->base_template->name);

    char names[256][256];
    size_t count = 0;
    af_template_list_effective_attrs(reactor, names, &count);
    printf("Effective attributes: ");
    for (size_t i = 0; i < count; i++) printf("%s ", names[i]);
    printf("\n\n");

    AFElement *r101 = af_template_instantiate(reactor, "Reactor_R101");
    AFElement *r102 = af_template_instantiate(reactor, "Reactor_R102");
    printf("Created: %s (attrs=%zu), %s (attrs=%zu)\n",
           r101->name, r101->attr_count, r102->name, r102->attr_count);

    AFAttribute *t = af_element_get_attribute(r101, "Temperature");
    if (t) {
        char v[64];
        af_attribute_value_to_string(t, v, sizeof(v));
        printf("R101 Temperature: %s degC\n", v);
    }

    printf("Template version: %d\n", af_template_get_version(reactor));
    printf("Inheritance valid: %s\n",
           af_template_is_descendant(reactor, base_eq) ? "yes" : "no");

    af_element_destroy(r101);
    af_element_destroy(r102);
    af_template_destroy(reactor);
    af_template_destroy(base_eq);
    printf("\nTemplate demo complete.\n");
    return 0;
}
