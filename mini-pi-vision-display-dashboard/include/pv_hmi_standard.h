/**
 * pv_hmi_standard.h - ISA-101 High Performance HMI Compliance (L4)
 *
 * Implements ISA-101 HMI design principles for industrial displays:
 * color usage rules, navigation limits, element density constraints,
 * and situational awareness guidelines.
 *
 * Knowledge Coverage:
 *   L4 Standards: ISA-101.01-2015, High Performance HMI principles
 *
 * Reference:
 *   ISA-101.01-2015 "Human Machine Interfaces for Process Automation"
 *   Hollifield, B. et al. "The High Performance HMI"
 *
 * Course Map: ISA/IEC ISA-101, RWTH Aachen Industrial Control
 */

#ifndef PV_HMI_STANDARD_H
#define PV_HMI_STANDARD_H

#include "pv_dashboard.h"
#include "pv_symbol.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ISA-101 color palette presets */
typedef enum {
    PV_PALETTE_GRAY       = 0,  /* Standard gray background */
    PV_PALETTE_HIGH_CONTRAST = 1,  /* Bright environment */
    PV_PALETTE_LOW_LIGHT  = 2   /* Night mode */
} pv_palette_preset_t;

/* HMI violation types */
typedef enum {
    PV_HMI_OK                    = 0,
    PV_HMI_VIOLATION_COLOR       = 1,   /* Non-standard color usage */
    PV_HMI_VIOLATION_DENSITY     = 2,   /* Too many elements on screen */
    PV_HMI_VIOLATION_NAV_DEPTH   = 3,   /* Navigation depth exceeded */
    PV_HMI_VIOLATION_ALARM_COLOR = 4,   /* Alarm color misused */
    PV_HMI_VIOLATION_BRIGHTNESS  = 5    /* Excessive brightness */
} pv_hmi_violation_t;

/* HMI audit result */
typedef struct {
    int  violation_count;
    pv_hmi_violation_t violations[16];
    int  element_density;
    int  max_allowed_density;
    int  nav_depth_actual;
    int  nav_depth_max;
    int  alarm_colors_used;
    int  non_standard_colors;
} pv_hmi_audit_t;

/* API */
void pv_hmi_get_palette(pv_palette_preset_t preset, pv_color_palette_t *palette);
int  pv_hmi_validate_color(const pv_color_t *color, const pv_color_palette_t *palette);
int  pv_hmi_check_density(int element_count, int canvas_width_px, int canvas_height_px);
int  pv_hmi_validate_alarm_color(const pv_color_t *color, pv_alarm_severity_t severity, const pv_color_palette_t *palette);
int  pv_hmi_audit_display(const pv_display_t *display, pv_hmi_audit_t *result);
int  pv_hmi_audit_dashboard(const pv_dashboard_t *dashboard, pv_hmi_audit_t *result);
void pv_hmi_audit_print(const pv_hmi_audit_t *result);

/* ISA-101 display density: max elements per level */
int  pv_hmi_max_elements_by_level(pv_navigation_level_t level);

/* Validate brightness (no excessively bright colors on gray backgrounds) */
int  pv_hmi_validate_brightness(const pv_color_t *fg, const pv_color_t *bg, double max_contrast_ratio);

#ifdef __cplusplus
}
#endif

#endif /* PV_HMI_STANDARD_H */

int pv_hmi_validate_spacing(const pv_display_t *display, double min_gap_pct);
int pv_hmi_validate_color_count(const pv_display_t *display, int max_colors);
int pv_hmi_validate_labels(const pv_display_t *display);
double pv_hmi_recommended_refresh_rate(pv_navigation_level_t level);
void pv_hmi_generate_report(const pv_dashboard_t *db, FILE *output);
