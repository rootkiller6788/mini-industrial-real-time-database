/**
 * pv_hmi_standard.c - ISA-101 High Performance HMI (L4)
 */
#include "pv_hmi_standard.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

void pv_hmi_get_palette(pv_palette_preset_t preset, pv_color_palette_t *p) {
    if (!p) return;
    /* ISA-101 standard gray background as default */
    p->background.r=0xD3; p->background.g=0xD3; p->background.b=0xD3; p->background.a=0xFF;
    p->canvas.r=0xE8; p->canvas.g=0xE8; p->canvas.b=0xE8; p->canvas.a=0xFF;
    p->text_primary.r=0x33; p->text_primary.g=0x33; p->text_primary.b=0x33; p->text_primary.a=0xFF;
    if (preset == PV_PALETTE_HIGH_CONTRAST) {
        p->background.r=0xFF; p->background.g=0xFF; p->background.b=0xFF; p->background.a=0xFF;
        p->canvas.r=0xF5; p->canvas.g=0xF5; p->canvas.b=0xF5; p->canvas.a=0xFF;
        p->text_primary.r=0x00; p->text_primary.g=0x00; p->text_primary.b=0x00; p->text_primary.a=0xFF;
    } else if (preset == PV_PALETTE_LOW_LIGHT) {
        p->background.r=0x1A; p->background.g=0x1A; p->background.b=0x1A; p->background.a=0xFF;
        p->canvas.r=0x2D; p->canvas.g=0x2D; p->canvas.b=0x2D; p->canvas.a=0xFF;
        p->text_primary.r=0xE0; p->text_primary.g=0xE0; p->text_primary.b=0xE0; p->text_primary.a=0xFF;
    }
    p->text_secondary.r=0x80; p->text_secondary.g=0x80; p->text_secondary.b=0x80; p->text_secondary.a=0xFF;
    /* ISA-101 standard alarm colors */
    p->alarm_critical.r=0xFF; p->alarm_critical.g=0x00; p->alarm_critical.b=0x00; p->alarm_critical.a=0xFF;
    p->alarm_high.r=0xFF; p->alarm_high.g=0xA5; p->alarm_high.b=0x00; p->alarm_high.a=0xFF;
    p->alarm_medium.r=0xFF; p->alarm_medium.g=0xFF; p->alarm_medium.b=0x00; p->alarm_medium.a=0xFF;
    p->alarm_low.r=0x00; p->alarm_low.g=0x80; p->alarm_low.b=0xFF; p->alarm_low.a=0xFF;
    p->status_normal.r=0x00; p->status_normal.g=0x80; p->status_normal.b=0x00; p->status_normal.a=0xFF;
    p->status_warning.r=0xFF; p->status_warning.g=0xA5; p->status_warning.b=0x00; p->status_warning.a=0xFF;
    p->grid_line.r=0xC0; p->grid_line.g=0xC0; p->grid_line.b=0xC0; p->grid_line.a=0xFF;
    p->accent.r=0x00; p->accent.g=0x70; p->accent.b=0xC0; p->accent.a=0xFF;
}

int pv_hmi_validate_color(const pv_color_t *c, const pv_color_palette_t *pal) {
    if (!c||!pal) return 0;
    pv_color_t pals[]={pal->background,pal->canvas,pal->text_primary,pal->text_secondary,
        pal->alarm_critical,pal->alarm_high,pal->alarm_medium,pal->alarm_low,
        pal->status_normal,pal->status_warning,pal->grid_line,pal->accent};
    int np=sizeof(pals)/sizeof(pals[0]);
    for(int i=0;i<np;i++)
        if(abs((int)c->r-(int)pals[i].r)<=10&&abs((int)c->g-(int)pals[i].g)<=10&&abs((int)c->b-(int)pals[i].b)<=10) return 1;
    return 0;
}

int pv_hmi_max_elements_by_level(pv_navigation_level_t level) {
    switch(level){
    case PV_NAV_LEVEL_1:return 12;
    case PV_NAV_LEVEL_2:return 20;
    case PV_NAV_LEVEL_3:return 30;
    case PV_NAV_LEVEL_4:return 40;
    default:return 20;
    }
}

int pv_hmi_check_density(int elem_count, int cw, int ch) {
    if(cw<=0||ch<=0)return 0;
    double area=(double)cw*(double)ch;
    double density=elem_count/(area/10000.0);
    return (density<=5.0)?1:0;
}

int pv_hmi_validate_alarm_color(const pv_color_t *c, pv_alarm_severity_t sev, const pv_color_palette_t *pal) {
    if(!c||!pal)return 1;
    pv_color_t exp;
    switch(sev){
    case PV_ALARM_CRITICAL:exp=pal->alarm_critical;break;
    case PV_ALARM_HIGH:exp=pal->alarm_high;break;
    case PV_ALARM_MEDIUM:exp=pal->alarm_medium;break;
    case PV_ALARM_LOW:exp=pal->alarm_low;break;
    default:return 1;
    }
    return (abs((int)c->r-(int)exp.r)<=15&&abs((int)c->g-(int)exp.g)<=15&&abs((int)c->b-(int)exp.b)<=15);
}

int pv_hmi_validate_brightness(const pv_color_t *fg, const pv_color_t *bg, double max_ratio) {
    if(!fg||!bg)return 1;
    double lf=0.2126*fg->r/255.0+0.7152*fg->g/255.0+0.0722*fg->b/255.0;
    double lb=0.2126*bg->r/255.0+0.7152*bg->g/255.0+0.0722*bg->b/255.0;
    double ratio=(lf>lb)?(lf+0.05)/(lb+0.05):(lb+0.05)/(lf+0.05);
    return (ratio<=max_ratio)?1:0;
}

int pv_hmi_audit_display(const pv_display_t *d, pv_hmi_audit_t *result) {
    if(!result)return 0;
    memset(result,0,sizeof(*result));
    if(!d)return 0;
    int n=pv_display_get_element_count_recursive(d);
    result->element_density=n;
    result->max_allowed_density=pv_hmi_max_elements_by_level(PV_NAV_LEVEL_2);
    if(n>result->max_allowed_density)
        result->violations[result->violation_count++]=PV_HMI_VIOLATION_DENSITY;
    return result->violation_count;
}

int pv_hmi_audit_dashboard(const pv_dashboard_t *db, pv_hmi_audit_t *result) {
    if(!result)return 0;
    memset(result,0,sizeof(*result));
    if(!db)return 0;
    result->nav_depth_actual=(int)db->max_depth;
    result->nav_depth_max=4;
    if(result->nav_depth_actual>result->nav_depth_max)
        result->violations[result->violation_count++]=PV_HMI_VIOLATION_NAV_DEPTH;
    if(db->active_display){
        pv_hmi_audit_t sub;
        pv_hmi_audit_display(db->active_display,&sub);
        for(int i=0;i<sub.violation_count&&result->violation_count<16;i++)
            result->violations[result->violation_count++]=sub.violations[i];
        result->element_density=sub.element_density;
        result->max_allowed_density=sub.max_allowed_density;
    }
    return result->violation_count;
}

void pv_hmi_audit_print(const pv_hmi_audit_t *r) {
    if(!r)return;
    printf("HMI Audit: %d violations, density=%d/%d, nav_depth=%d/%d\n",
        r->violation_count,r->element_density,r->max_allowed_density,
        r->nav_depth_actual,r->nav_depth_max);
    for(int i=0;i<r->violation_count;i++){
        const char *s;
        switch(r->violations[i]){
        case PV_HMI_VIOLATION_COLOR:s="Non-standard color";break;
        case PV_HMI_VIOLATION_DENSITY:s="Density exceeded";break;
        case PV_HMI_VIOLATION_NAV_DEPTH:s="Nav depth exceeded";break;
        case PV_HMI_VIOLATION_ALARM_COLOR:s="Alarm color misuse";break;
        case PV_HMI_VIOLATION_BRIGHTNESS:s="Brightness violation";break;
        default:s="Unknown";
        }
        printf("  [%d] %s\n",r->violations[i],s);
    }
}

/* =========================================================================
 * L4: ISA-101 Element Spacing Validation
 *
 * ISA-101 recommends minimum spacing between elements to prevent
 * visual clutter. Minimum gap: 2% of canvas dimension.
 * ========================================================================= */

int pv_hmi_validate_spacing(const pv_display_t *display, double min_gap_pct) {
    if (!display || !display->elements) return 1;
    /* Check pairwise spacing between top-level elements */
    int count = 0;
    pv_display_element_t *arr[64];
    for (pv_display_element_t *e = display->elements; e && count < 64; e = e->next) {
        arr[count++] = e;
    }
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            /* Compute gap between rectangles */
            double dx = arr[i]->bounds.origin.x - arr[j]->bounds.origin.x;
            double dy = arr[i]->bounds.origin.y - arr[j]->bounds.origin.y;
            double gap_x = (dx > 0) ? dx - arr[j]->bounds.width : -dx - arr[i]->bounds.width;
            double gap_y = (dy > 0) ? dy - arr[j]->bounds.height : -dy - arr[i]->bounds.height;
            if (gap_x < min_gap_pct && gap_y < min_gap_pct &&
                gap_x > -arr[i]->bounds.width && gap_y > -arr[i]->bounds.height) {
                return 0;  /* Overlap or insufficient spacing */
            }
        }
    }
    return 1;
}

/* =========================================================================
 * L4: ISA-101 Color Count Validation
 *
 * ISA-101 Principle: Use color sparingly. At most 6 distinct colors
 * (excluding grayscale) on a single display.
 * ========================================================================= */

int pv_hmi_validate_color_count(const pv_display_t *display, int max_colors) {
    if (!display) return 1;
    /* Simplified: count unique non-gray colors among elements */
    pv_color_t unique[32];
    int unique_count = 0;
    /* Traverse elements and count unique colors */
    pv_display_element_t *stack[128];
    int ss = 0;
    pv_display_element_t *e = display->elements;
    while (e || ss > 0) {
        while (e) {
            /* Check if foreground is non-gray */
            int is_gray = (e->foreground.r == e->foreground.g &&
                          e->foreground.g == e->foreground.b);
            if (!is_gray) {
                int found = 0;
                for (int i = 0; i < unique_count; i++) {
                    if (unique[i].r == e->foreground.r &&
                        unique[i].g == e->foreground.g &&
                        unique[i].b == e->foreground.b) {
                        found = 1; break;
                    }
                }
                if (!found && unique_count < 32) {
                    unique[unique_count++] = e->foreground;
                }
            }
            if (e->children && ss < 128) {
                stack[ss++] = e->next;
                e = e->children;
            } else {
                e = e->next;
            }
        }
        if (ss > 0) e = stack[--ss];
    }
    return (unique_count <= max_colors) ? 1 : 0;
}

/* =========================================================================
 * L4: ISA-101 Text Size Validation
 *
 * ISA-101 requires text to be readable at typical viewing distance.
 * Labels must not be empty and must fit within element bounds.
 * ========================================================================= */

int pv_hmi_validate_labels(const pv_display_t *display) {
    if (!display) return 1;
    pv_display_element_t *stack[128];
    int ss = 0;
    pv_display_element_t *e = display->elements;
    while (e || ss > 0) {
        while (e) {
            if (e->visible && e->label[0] == '\0') {
                return 0;  /* Visible element without label */
            }
            if (e->children && ss < 128) {
                stack[ss++] = e->next;
                e = e->children;
            } else {
                e = e->next;
            }
        }
        if (ss > 0) e = stack[--ss];
    }
    return 1;
}

/* =========================================================================
 * L4: ISA-101 Refresh Rate Recommendation
 *
 * For Level 1 and 2 displays (overview/unit), refresh every 5-10 seconds.
 * For Level 3 and 4 displays (detail), refresh every 1-2 seconds.
 * ========================================================================= */

double pv_hmi_recommended_refresh_rate(pv_navigation_level_t level) {
    switch (level) {
    case PV_NAV_LEVEL_1: return 10.0;
    case PV_NAV_LEVEL_2: return 5.0;
    case PV_NAV_LEVEL_3: return 2.0;
    case PV_NAV_LEVEL_4: return 1.0;
    default: return 5.0;
    }
}

/* =========================================================================
 * L4: Generate ISA-101 Compliance Report
 *
 * Produces a text report summarizing all ISA-101 compliance checks
 * for a dashboard and its displays.
 * ========================================================================= */

void pv_hmi_generate_report(const pv_dashboard_t *db, FILE *output) {
    if (!db || !output) return;
    fprintf(output, "=== ISA-101 HMI Compliance Report ===\n");
    fprintf(output, "Dashboard: %s\n", db->name);
    fprintf(output, "Layout: %dx%d grid\n", db->layout.columns, db->layout.rows);
    fprintf(output, "Navigation depth: %d (max 4)\n", (int)db->max_depth);

    if (db->active_display) {
        int elem_count = pv_display_get_element_count_recursive(db->active_display);
        fprintf(output, "Active display: %s\n", db->active_display->name);
        fprintf(output, "  Elements: %d\n", elem_count);
        fprintf(output, "  State: %d\n", (int)db->active_display->state);

        /* Density check */
        int density_ok = pv_hmi_check_density(elem_count, 1920, 1080);
        fprintf(output, "  Density: %s\n", density_ok ? "PASS" : "FAIL");

        /* Label check */
        int labels_ok = pv_hmi_validate_labels(db->active_display);
        fprintf(output, "  Labels: %s\n", labels_ok ? "PASS" : "FAIL");

        /* Color count check */
        int colors_ok = pv_hmi_validate_color_count(db->active_display, 6);
        fprintf(output, "  Color count: %s\n", colors_ok ? "PASS" : "FAIL");
    }
    fprintf(output, "=== End Report ===\n");
}
