#include "pv_trend.h"
#include "pv_symbol.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <time.h>

static int tr=0, tp=0;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{printf("PASS\n");tp++;}while(0)

int main(void) {
    printf("=== PI Vision Trend Tests ===\n");

    T("trend_buffer_create");
    pv_trend_buffer_t *buf = pv_trend_buffer_create(100);
    assert(buf != NULL);
    assert(buf->capacity == 100);
    assert(buf->count == 0);
    pv_trend_buffer_destroy(buf);
    assert(pv_trend_buffer_create(0) == NULL);
    assert(pv_trend_buffer_create(-1) == NULL);
    P();

    T("trend_buffer_push_get");
    buf = pv_trend_buffer_create(10);
    for (int i = 0; i < 5; i++) {
        pv_trend_point_t pt = {time(NULL) + i, (double)i * 10.0, 100, 1};
        pv_trend_buffer_push(buf, &pt);
    }
    assert(buf->count == 5);
    pv_trend_point_t pt;
    assert(pv_trend_buffer_get(buf, 0, &pt) == 1);
    assert(pt.value == 0.0);
    assert(pv_trend_buffer_get(buf, 4, &pt) == 1);
    assert(pt.value == 40.0);
    assert(pv_trend_buffer_get(buf, 5, &pt) == 0);
    assert(pv_trend_buffer_get(buf, -1, &pt) == 0);
    pv_trend_buffer_destroy(buf);
    P();

    T("trend_buffer_circular_overflow");
    buf = pv_trend_buffer_create(3);
    for (int i = 0; i < 5; i++) {
        pv_trend_point_t pt = {100 + i, (double)i, 100, 1};
        pv_trend_buffer_push(buf, &pt);
    }
    assert(buf->count == 3);
    /* Oldest should be index 2 (pushed at i=2) */
    assert(pv_trend_buffer_get(buf, 0, &pt) == 1);
    assert(pt.value == 2.0);
    /* Newest should be index 2 (pushed at i=4) */
    assert(pv_trend_buffer_get(buf, 2, &pt) == 1);
    assert(pt.value == 4.0);
    pv_trend_buffer_destroy(buf);
    P();

    T("trend_buffer_statistics");
    buf = pv_trend_buffer_create(10);
    for (int i = 1; i <= 5; i++) {
        pv_trend_point_t pt = {time(NULL), (double)i * 10.0, 100, 1};
        pv_trend_buffer_push(buf, &pt);
    }
    double min, max, avg; int count;
    pv_trend_buffer_get_statistics(buf, &min, &max, &avg, &count);
    assert(count == 5);
    assert(fabs(min - 10.0) < 0.01);
    assert(fabs(max - 50.0) < 0.01);
    assert(fabs(avg - 30.0) < 0.01);
    pv_trend_buffer_destroy(buf);
    P();

    T("trend_buffer_get_range");
    buf = pv_trend_buffer_create(10);
    time_t base = 1000;
    for (int i = 0; i < 6; i++) {
        pv_trend_point_t pt = {base + i * 10, (double)i, 100, 1};
        pv_trend_buffer_push(buf, &pt);
    }
    pv_trend_point_t out[10];
    int n = pv_trend_buffer_get_range(buf, 1010, 1030, out, 10);
    assert(n == 3);  /* timestamps 1010, 1020, 1030 */
    assert(out[0].timestamp == 1010);
    assert(out[2].timestamp == 1030);
    pv_trend_buffer_destroy(buf);
    P();

    T("douglas_peucker_decimate");
    pv_trend_point_t points[10];
    for (int i = 0; i < 10; i++) {
        points[i].timestamp = i;
        points[i].value = (i == 5) ? 10.0 : 0.0;  /* Spike at i=5 */
        points[i].quality = 100;
        points[i].is_good = 1;
    }
    pv_trend_point_t result[10];
    int kept = pv_trend_decimate_douglas_peucker(points, 10, 5, 1.0, result);
    assert(kept > 0 && kept <= 5);
    /* First and last should always be kept */
    assert(result[0].timestamp == 0);
    assert(result[kept-1].timestamp == 9);
    P();

    T("douglas_peucker_trivial");
    pv_trend_point_t pts2[2] = {{0,0.0,100,1},{1,1.0,100,1}};
    pv_trend_point_t res2[2];
    int k2 = pv_trend_decimate_douglas_peucker(pts2, 2, 10, 1.0, res2);
    assert(k2 == 2);  /* Already within limit */
    P();

    T("wu_line_rendering");
    pv_trend_line_segment_t segs[200];
    int nsegs = pv_trend_render_line_wu(0, 0, 10, 5,
        (pv_color_t){0xFF,0x00,0x00,0xFF}, segs, 200);
    assert(nsegs > 0);
    assert(nsegs <= 200);
    /* Each segment should have intensity between 0 and 1 */
    int valid = 1;
    for (int i = 0; i < nsegs; i++) {
        if (segs[i].intensity < 0.0 || segs[i].intensity > 1.0) {
            valid = 0; break;
        }
    }
    assert(valid == 1);
    P();

    T("value_to_pixel_y");
    int py = pv_trend_value_to_pixel_y(50.0, 0.0, 100.0, 0, 200);
    assert(py == 100);
    py = pv_trend_value_to_pixel_y(0.0, 0.0, 100.0, 0, 200);
    assert(py == 200);
    py = pv_trend_value_to_pixel_y(100.0, 0.0, 100.0, 0, 200);
    assert(py == 0);
    P();

    T("time_to_pixel_x");
    int px = pv_trend_time_to_pixel_x(500, 0, 1000, 0, 1000);
    assert(px == 500);
    px = pv_trend_time_to_pixel_x(0, 0, 1000, 0, 1000);
    assert(px == 0);
    P();

    T("auto_scale");
    buf = pv_trend_buffer_create(10);
    for (int i = 0; i < 3; i++) {
        pv_trend_point_t pt = {time(NULL), (double)i * 50.0, 100, 1};
        pv_trend_buffer_push(buf, &pt);
    }
    double ymin, ymax;
    pv_trend_compute_auto_scale(buf, &ymin, &ymax);
    assert(ymin < 0.0);
    assert(ymax > 100.0);
    assert((ymax - ymin) > 90.0);  /* Range with padding */
    pv_trend_buffer_destroy(buf);
    P();

    T("render_config");
    pv_trend_symbol_t ts; memset(&ts,0,sizeof(ts));
    ts.y_left_min=0.0; ts.y_left_max=100.0;
    ts.y_right_min=0.0; ts.y_right_max=50.0;
    pv_rect_t bounds={{5,5},80,70};
    pv_time_range_t trange={1,0,0,8*3600,""};
    pv_trend_render_config_t cfg;
    pv_trend_build_render_config(&ts,bounds,1920,1080,&trange,&cfg);
    assert(cfg.plot_width > 0);
    assert(cfg.plot_height > 0);
    assert(cfg.x_scale > 0.0);
    P();

    T("display_points");
    int dp = pv_trend_get_display_points(800);
    assert(dp == 400);
    dp = pv_trend_get_display_points(0);
    assert(dp == 0);
    P();

    printf("=== Results: %d/%d tests passed ===\n",tp,tr);
    return (tp==tr)?0:1;
}
