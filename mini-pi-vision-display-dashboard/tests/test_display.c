#include "pv_display.h"
#include "pv_symbol.h"
#include "pv_trend.h"
#include "pv_dashboard.h"
#include "pv_render.h"
#include "pv_hmi_standard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

static int tr=0, tp=0;
#define T(n) do{tr++;printf("  TEST %s... ",n);}while(0)
#define P() do{printf("PASS\n");tp++;}while(0)

int main(void) {
    printf("=== PI Vision Display Tests ===\n");

    /* Display lifecycle tests */
    T("display_create");
    pv_display_t *d=pv_display_create("Test","Desc");
    assert(d); assert(!strcmp(d->name,"Test"));
    assert(d->state==PV_DISPLAY_LOADING);
    pv_display_destroy(d);
    assert(pv_display_create(NULL,NULL)==NULL);
    assert(pv_display_create("",NULL)==NULL);
    P();

    T("display_destroy_null");
    pv_display_destroy(NULL);
    P();

    T("element_create");
    pv_rect_t b={{10.0,10.0},30.0,20.0};
    pv_display_element_t *e=pv_element_create(PV_SYM_VALUE,b,"Temp");
    assert(e); assert(e->sym_type==PV_SYM_VALUE);
    assert(e->element_id>0); assert(!strcmp(e->label,"Temp"));
    assert(e->bounds.origin.x==10.0); assert(e->bounds.width==30.0);
    pv_element_destroy(e);
    P();

    T("display_add_find");
    d=pv_display_create("Test",NULL);
    e=pv_element_create(PV_SYM_TREND,b,"T1");
    uint32_t eid=e->element_id;
    pv_display_add_element(d,e);
    assert(d->element_count==1);
    pv_display_element_t *f=pv_display_find_element(d,eid);
    assert(f); assert(f->element_id==eid);
    assert(pv_display_find_element(d,99999)==NULL);
    pv_display_destroy(d);
    P();

    T("display_remove");
    d=pv_display_create("Test",NULL);
    pv_display_element_t *e1=pv_element_create(PV_SYM_VALUE,b,"E1");
    pv_display_element_t *e2=pv_element_create(PV_SYM_GAUGE,b,"E2");
    uint32_t id1=e1->element_id,id2=e2->element_id;
    pv_display_add_element(d,e1); pv_display_add_element(d,e2);
    assert(d->element_count==2);
    assert(pv_display_remove_element(d,id1)==1);
    assert(d->element_count==1);
    assert(!pv_display_find_element(d,id1));
    assert(pv_display_find_element(d,id2));
    assert(pv_display_remove_element(d,99999)==0);
    pv_display_destroy(d);
    P();

    T("child_hierarchy");
    pv_display_element_t *p=pv_element_create(PV_SYM_TREND,b,"Parent");
    pv_display_element_t *c1=pv_element_create(PV_SYM_VALUE,b,"C1");
    pv_display_element_t *c2=pv_element_create(PV_SYM_VALUE,b,"C2");
    pv_element_add_child(p,c1); pv_element_add_child(p,c2);
    assert(p->children==c1); assert(c1->next==c2);
    d=pv_display_create("Test",NULL);
    pv_display_add_element(d,p);
    assert(pv_display_get_element_count_recursive(d)==3);
    pv_display_destroy(d);
    P();

    T("data_binding");
    e=pv_element_create(PV_SYM_VALUE,b,"Pressure");
    pv_data_binding_t bind; memset(&bind,0,sizeof(bind));
    strcpy(bind.server_name,"PI-SRV"); strcpy(bind.point_name,"SINUSOID");
    strcpy(bind.uom,"MPa"); bind.span=100.0;
    pv_element_bind_data(e,&bind);
    assert(!strcmp(e->data_binding.server_name,"PI-SRV"));
    time_t now=time(NULL);
    pv_element_update_value(e,42.5,100,now);
    assert(e->last_value==42.5); assert(e->last_quality==100);
    pv_element_destroy(e);
    P();

    T("coord_transforms");
    int px,py; pv_coord_t c={50.0,50.0};
    pv_coord_to_pixel(c,1920,1080,&px,&py);
    assert(px==960); assert(py==540);
    pv_rect_t r={{0,0},100,100};
    int ox,oy,ow,oh;
    pv_rect_to_pixel(r,800,600,&ox,&oy,&ow,&oh);
    assert(ox==0); assert(oy==0); assert(ow==800);
    pv_coord_t in={30,40}, out={150,50};
    pv_rect_t rect={{10,10},50,50};
    assert(pv_rect_contains_point(rect,in)==1);
    assert(pv_rect_contains_point(rect,out)==0);
    pv_rect_t ra={{0,0},50,50}, rb={{25,25},50,50}, rc3={{60,60},10,10};
    assert(pv_rects_overlap(ra,rb)==1);
    assert(pv_rects_overlap(ra,rc3)==0);
    P();

    T("state_machine");
    d=pv_display_create("Test",NULL);
    assert(pv_display_set_state(d,PV_DISPLAY_ACTIVE)==1);
    assert(d->state==PV_DISPLAY_ACTIVE);
    assert(pv_display_set_state(d,PV_DISPLAY_PAUSED)==1);
    assert(pv_display_set_state(d,PV_DISPLAY_ACTIVE)==1);
    assert(pv_display_set_state(d,PV_DISPLAY_LOADING)==0);
    assert(d->state==PV_DISPLAY_ACTIVE);
    assert(pv_display_set_state(d,PV_DISPLAY_ERROR)==1);
    pv_display_destroy(d);
    P();

    T("version");
    d=pv_display_create("Test",NULL);
    assert(pv_display_increment_version(d)==1);
    assert(pv_display_increment_version(d)==2);
    pv_display_destroy(d);
    P();

    printf("=== Results: %d/%d tests passed ===\n",tp,tr);
    return (tp==tr)?0:1;
}
