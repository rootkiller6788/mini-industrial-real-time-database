#include "pv_symbol.h"
#include "pv_render.h"
#include "pv_hmi_standard.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>

static int tr=0,tp=0;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{printf("PASS\n");tp++;}while(0)

int main(void) {
    printf("=== Symbol & Render & HMI Tests ===\n");

    T("symbol_create_value");
    pv_value_symbol_t vcfg; memset(&vcfg,0,sizeof(vcfg));
    vcfg.precision=2; vcfg.range_min=0.0; vcfg.range_max=100.0;
    vcfg.alarm_high=95.0; vcfg.alarm_low=5.0;
    void *vs = pv_symbol_create_value(&vcfg);
    assert(vs != NULL);
    pv_symbol_destroy(PV_SYM_VALUE, vs);
    P();

    T("symbol_create_trend");
    pv_trend_symbol_t ts; memset(&ts,0,sizeof(ts));
    ts.num_pens=2; ts.max_pens=4;
    pv_trend_pen_t pens[2]; memset(pens,0,sizeof(pens));
    pens[0].line_width=2.0; ts.pens=pens;
    void *tp_sym = pv_symbol_create_trend(&ts);
    assert(tp_sym != NULL);
    pv_trend_symbol_t *tsp = (pv_trend_symbol_t*)tp_sym;
    assert(tsp->num_pens == 2);
    pv_symbol_destroy(PV_SYM_TREND, tp_sym);
    P();

    T("symbol_create_gauge");
    pv_gauge_symbol_t gs; memset(&gs,0,sizeof(gs));
    gs.type=PV_GAUGE_RADIAL; gs.gauge_min=0.0; gs.gauge_max=100.0;
    void *gv = pv_symbol_create_gauge(&gs);
    assert(gv != NULL);
    pv_symbol_destroy(PV_SYM_GAUGE, gv);
    P();

    T("symbol_create_state_indicator");
    pv_state_indicator_t si; memset(&si,0,sizeof(si));
    si.num_states=3; si.default_state=0;
    pv_state_def_t states[3];
    states[0].state_value=0; strcpy(states[0].state_label,"Off");
    states[1].state_value=1; strcpy(states[1].state_label,"On");
    states[2].state_value=2; strcpy(states[2].state_label,"Fault");
    si.states=states;
    void *sip = pv_symbol_create_state_indicator(&si);
    assert(sip != NULL);
    pv_symbol_destroy(PV_SYM_STATE_INDICATOR, sip);
    P();

    T("symbol_create_kpi");
    pv_kpi_indicator_t kpi; memset(&kpi,0,sizeof(kpi));
    kpi.target_value=100.0; kpi.change_percent=5.0;
    double sd[5]={90,92,95,98,100};
    kpi.sparkline_points=5; kpi.sparkline_data=sd;
    void *kp = pv_symbol_create_kpi(&kpi);
    assert(kp != NULL);
    pv_symbol_destroy(PV_SYM_KPI_INDICATOR, kp);
    P();

    T("symbol_create_alarm_list");
    pv_alarm_list_symbol_t al; memset(&al,0,sizeof(al));
    al.max_alarms=10; al.show_acked=1;
    void *alp = pv_symbol_create_alarm_list(&al);
    assert(alp != NULL);
    pv_symbol_destroy(PV_SYM_ALARM_LIST, alp);
    P();

    T("symbol_create_bar_graph");
    pv_bar_graph_symbol_t bg; memset(&bg,0,sizeof(bg));
    bg.num_bars=3; bg.bar_min=0.0; bg.bar_max=100.0;
    double vals[3]={30,60,90};
    pv_color_t cols[3]={{255,0,0,255},{0,255,0,255},{0,0,255,255}};
    bg.bar_values=vals; bg.bar_colors=cols;
    void *bgp = pv_symbol_create_bar_graph(&bg);
    assert(bgp != NULL);
    pv_symbol_destroy(PV_SYM_BAR_GRAPH, bgp);
    P();

    T("gauge_angle_value");
    pv_gauge_symbol_t rg; memset(&rg,0,sizeof(rg));
    rg.type=PV_GAUGE_RADIAL; rg.gauge_min=0.0; rg.gauge_max=100.0;
    double angle = pv_gauge_value_to_angle(&rg, 50.0);
    double val = pv_gauge_angle_to_value(&rg, angle);
    assert(fabs(val - 50.0) < 1.0);
    double sa = pv_gauge_value_to_angle(&rg, 0.0);
    assert(fabs(sa - 225.0) < 1.0);
    P();

    T("value_get_display_color");
    pv_value_symbol_t vs2; memset(&vs2,0,sizeof(vs2));
    vs2.alarm_high=90.0; vs2.alarm_low=10.0;
    vs2.warn_high=70.0; vs2.warn_low=30.0;
    pv_color_t norm={0,255,0,255}, warn={255,255,0,255}, alarm={255,0,0,255};
    pv_color_t c = pv_value_get_display_color(&vs2, 50.0, norm, warn, alarm);
    assert(c.r==norm.r);
    c = pv_value_get_display_color(&vs2, 95.0, norm, warn, alarm);
    assert(c.r==alarm.r);
    c = pv_value_get_display_color(&vs2, 75.0, norm, warn, alarm);
    assert(c.r==warn.r);
    P();

    T("render_queue");
    pv_render_queue_t *q = pv_render_queue_create(100, 1920, 1080);
    assert(q != NULL);
    pv_render_command_t cmd; memset(&cmd,0,sizeof(cmd));
    cmd.type=PV_RCMD_FILL_RECT; cmd.z_order=5;
    assert(pv_render_queue_add_command(q, &cmd) == 1);
    pv_render_queue_sort(q);
    pv_render_queue_destroy(q);
    P();

    T("display_cache");
    pv_display_cache_t *cache = pv_display_cache_create(10);
    assert(cache != NULL);
    pv_render_command_t out;
    assert(pv_display_cache_lookup(cache, 1, &out) == 0);
    pv_display_cache_invalidate(cache);
    double hr = pv_display_cache_hit_rate(cache);
    assert(hr >= 0.0 && hr <= 1.0);
    pv_display_cache_destroy(cache);
    P();

    T("adaptive_resolution");
    pv_time_range_t tr_short={1,0,0,30,""};
    assert(pv_render_adaptive_resolution(&tr_short, 800) == PV_RES_RAW);
    /* 10-day range with 400 displayable points -> day resolution */
    pv_time_range_t tr_10day={1,0,0,86400*10,""};
    assert(pv_render_adaptive_resolution(&tr_10day, 800) == PV_RES_DAY);
    P();

    T("hmi_palette_and_validate");
    pv_color_palette_t pal;
    pv_hmi_get_palette(PV_PALETTE_GRAY, &pal);
    assert(pal.alarm_critical.r == 0xFF && pal.alarm_critical.g == 0x00);
    pv_color_t ok={0xD3,0xD3,0xD3,0xFF}, bad={0xFF,0x00,0xFF,0xFF};
    assert(pv_hmi_validate_color(&ok, &pal) == 1);
    assert(pv_hmi_validate_color(&bad, &pal) == 0);
    P();

    T("hmi_density");
    assert(pv_hmi_check_density(10, 1920, 1080) == 1);
    assert(pv_hmi_check_density(1000, 100, 100) == 0);
    P();

    T("hmi_brightness");
    pv_color_t fg_c={0xFF,0xFF,0xFF,0xFF}, bg_c={0x00,0x00,0x00,0xFF};
    /* White-on-black contrast is 21:1, should fail at limit 20 */
    assert(pv_hmi_validate_brightness(&fg_c, &bg_c, 20.0) == 0);
    assert(pv_hmi_validate_brightness(&bg_c, &bg_c, 10.0) == 1);
    P();

    printf("=== Results: %d/%d tests passed ===\n",tp,tr);
    return (tp==tr)?0:1;
}
