"""将生产生命周期函数原样编入主机替身测试，避免复制被测实现。"""
from pathlib import Path

BASE = Path(__file__).resolve().parents[1] / 'iwatch/src/gui_apps'
OUT = Path(__file__).resolve().parent / 'build'


def function(source, signature):
    start = source.index(signature+'\n{')
    end = source.index('\n}', start)+2
    return source[start:end]


clock = (BASE/'clock/app_clock_main.c').read_text(encoding='utf-8')
menu = (BASE/'main/app_mainmenu.c').read_text(encoding='utf-8')
bar = (BASE/'clock/app_clock_status_bar.c').read_text(encoding='utf-8')

# 替身仅模拟所有权与失败返回，完整 LVGL 行为留给目标构建和板测。
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
typedef int rt_int32_t;
typedef unsigned rt_uint32_t;
typedef struct rt_list { struct rt_list *next, *prev; } rt_list_t;
static void rt_list_init(rt_list_t *n) { n->next=n->prev=n; }
static void rt_list_insert_before(rt_list_t *h, rt_list_t *n) { n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n; }
static void rt_list_remove(rt_list_t *n) { n->prev->next=n->next; n->next->prev=n->prev; n->next=n->prev=n; }
#define rt_list_isempty(n) ((n)->next==(n))
#define rt_list_entry(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define rt_strncpy strncpy
#define rt_kprintf(...) ((void)0)
#define RT_EOK 0
#define RT_EINVAL 22
#define RT_ENOMEM 12
#define APP_ID "test"
static int live, alloc_at, fail_at, recoveries, cancellations, mock_callbacks;
static void *test_calloc(size_t n,size_t z) { if (++alloc_at==fail_at) return NULL; void *p=calloc(n,z); assert(p); live++; return p; }
static void test_free(void *p) { if (p) { live--; free(p); } }
#define rt_calloc test_calloc
#define rt_malloc(z) test_calloc(1,z)
#define rt_free test_free
#define lv_malloc(z) test_calloc(1,z)
#define lv_free test_free
static void iw_recovery_hide(const char *id) { (void)id; }
static void iw_recovery_show(const char *id) { (void)id; recoveries++; }
static void iw_gui_cancel_input(void) { cancellations++; }
typedef struct lv_obj { struct lv_obj *parent, *child, *next; void *user_data; const void *src; } lv_obj_t;
typedef struct { int x,y; } lv_point_t;
typedef struct { int dummy; } lv_indev_t;
typedef int lv_key_t;
typedef int lv_indev_state_t;
typedef struct lv_event { void *user_data; } lv_event_t;
typedef struct { void *user_data; void (*cb)(lv_event_t*); } lv_display_t;
static lv_obj_t screen;
static lv_display_t display;
static lv_obj_t *lv_scr_act(void) { return &screen; }
static lv_display_t *lv_display_get_default(void) { return &display; }
static lv_obj_t *lv_obj_create(lv_obj_t *parent) { lv_obj_t *o=test_calloc(1,sizeof(*o)); if(o) { assert(parent); o->parent=parent; o->next=parent->child;parent->child=o; } return o; }
static void lv_obj_delete(lv_obj_t *o) { assert(o); while(o->child) lv_obj_delete(o->child); if(o->parent){lv_obj_t **p=&o->parent->child;while(*p!=o){assert(*p);p=&(*p)->next;}*p=o->next;} test_free(o); }
static void lv_obj_clean(lv_obj_t *o) { assert(o); while(o->child)lv_obj_delete(o->child); }
static void lv_anim_delete(lv_obj_t *o, void *cb) { (void)o; (void)cb; }
static void lv_obj_remove_event_cb(lv_obj_t *o, void (*cb)(lv_event_t*)) { (void)o; (void)cb; }
static void lv_obj_add_event_cb(lv_obj_t *o, void (*cb)(lv_event_t*), int filter, void *data) { (void)o;(void)cb;(void)filter;(void)data; }
static void lv_display_add_event_cb(lv_display_t *d,void (*cb)(lv_event_t*),int filter,void *data) { (void)filter;mock_callbacks++;d->cb=cb;d->user_data=data; }
static void lv_display_remove_event_cb_with_user_data(lv_display_t *d,void (*cb)(lv_event_t*),void *data) { if(d->cb==cb && d->user_data==data){mock_callbacks--;d->cb=NULL;d->user_data=NULL;} }
static uint32_t lv_display_get_event_count(lv_display_t *d) { (void)d;return (uint32_t)mock_callbacks; }
#define LV_HOR_RES_MAX 390
#define LV_VER_RES_MAX 450
#define LV_FB_LINE_NUM 50
#define LV_PART_MAIN 0
#define LV_STATE_DEFAULT 0
#define LV_COLOR_BLACK 0
#define LV_ALIGN_CENTER 0
#define LV_SCROLL_SNAP_CENTER 0
#define LV_SCROLLBAR_MODE_OFF 0
#define LV_EVENT_ALL 0
#define LV_EVENT_REFR_START 0
#define lv_obj_set_size(...) ((void)0)
#define lv_obj_set_style_bg_color(...) ((void)0)
#define lv_obj_align(...) ((void)0)
#define lv_obj_set_style_border_width(...) ((void)0)
#define lv_obj_set_scroll_snap_x(...) ((void)0)
#define lv_obj_set_scroll_snap_y(...) ((void)0)
#define lv_obj_set_scrollbar_mode(...) ((void)0)
'''

clock_types = clock[clock.index('typedef enum'):clock.index('#ifndef BSP_USING_LVGL_INPUT_AGENT')]
clock_head = r'''
#define APP_CLOCK_ID_MAX_LEN 8
#define APP_CLOCK_MAX_COUNT 16
typedef struct { int (*init)(lv_obj_t*);int(*pause)(void);int(*resume)(void);int(*deinit)(void); } app_clock_ops_t;
'''+clock_types+r'''
static app_clock_main_t *p_app_clock_main;
static char *change_context;
static lv_obj_t *clk_parent;
static int initialized_plugins, init_result, resume_result;
static int plugin_init(lv_obj_t *p) { (void)p; initialized_plugins++;return init_result; }
static int plugin_pause(void) { return 0; }
static int plugin_resume(void) { return resume_result; }
static int plugin_deinit(void) { initialized_plugins--;return 0; }
static const app_clock_ops_t ops={plugin_init,plugin_pause,plugin_resume,plugin_deinit};
static void tileview_event_cb_t(lv_event_t *e) { (void)e; }
static void app_clock_main_status_bar_deinit(void) {}
'''
clock_functions = '\n'.join(function(clock, sig) for sig in [
    'static void app_clock_change_state(app_clock_desc_t *p_clock, uint8_t new_state)',
    'static void on_stop(void)',
    'int32_t app_clock_register(const char *id, const app_clock_ops_t *operations)'])
clock_test = r'''
int main(void) {
    for(int cycle=0;cycle<1000;cycle++) {
        p_app_clock_main=rt_calloc(1,sizeof(*p_app_clock_main)); assert(p_app_clock_main);
        rt_list_init(&p_app_clock_main->list);
        assert(app_clock_register("a",&ops)==0);assert(app_clock_register("b",&ops)==0);assert(app_clock_register("c",&ops)==0);
        for(rt_list_t *n=p_app_clock_main->list.next;n!=&p_app_clock_main->list;n=n->next) {
            app_clock_desc_t *d=rt_list_entry(n,app_clock_desc_t,node);d->parent=&screen;
            app_clock_change_state(d,STATE_ACTIVE);
        }
        assert(initialized_plugins==3);on_stop();on_stop();
        assert(live==0 && initialized_plugins==0 && !change_context && !clk_parent);
    }
    app_clock_desc_t d={0};d.parent=&screen;d.ops=&ops;
    init_result=-RT_ENOMEM;app_clock_change_state(&d,STATE_ACTIVE);
    assert(d.state==STATE_DEINIT && initialized_plugins==0 && recoveries==1);
    init_result=0;resume_result=-RT_ENOMEM;app_clock_change_state(&d,STATE_ACTIVE);
    assert(d.state==STATE_DEINIT && initialized_plugins==0 && recoveries==2);
    p_app_clock_main=rt_calloc(1,sizeof(*p_app_clock_main));rt_list_init(&p_app_clock_main->list);
    fail_at=alloc_at+1;assert(app_clock_register("fail",&ops)==-RT_ENOMEM);
    assert(p_app_clock_main->registration_failed);on_stop();assert(live==0);
    puts("Clock teardown/failure tests passed (1000 cycles)");return 0;
}
'''

start=menu.index('typedef struct\n{\n    lv_obj_t *pg_obj;')
menu_types=menu[start:menu.index('static mainmenu_cell_t *p_menu_cell',start)]
menu_head=r'''
#define MAX_APP_COL_NUM 8
#define MAX_APP_ROW_NUM 8
'''+menu_types+r'''
static mainmenu_cell_t *p_menu_cell;
static bool redraw_failed;
static void on_stop(void);
static void refr_start_cb(lv_event_t *e) { assert(p_menu_cell && e->user_data==p_menu_cell); }
static void mainmenu_cell_page_event_cb(lv_event_t *e) { (void)e; }
static void mainmenu_cell_redraw_app(lv_obj_t *parent) { (void)parent;p_menu_cell->build_failed=redraw_failed; }
'''
menu_functions='\n'.join(function(menu,sig) for sig in [
    'static void mainmenu_cell_clean_app(lv_obj_t *parent)',
    'static void mainmenu_cell_ui_init(void *param)',
    'static void on_start(void)', 'static void on_pause(void)', 'static void on_stop(void)'])
menu_test=r'''
int main(void) {
    for(int cycle=0;cycle<1000;cycle++) {
        on_start();assert(p_menu_cell && mock_callbacks==1);on_pause();on_pause();on_stop();on_stop();
        assert(!p_menu_cell && !display.cb && mock_callbacks==0 && !screen.child && live==0);
    }
    /* 管理器、两个数组、页面对象的每一个分配点依次失败。 */
    for(int point=1;point<=4;point++) {
        alloc_at=0;fail_at=point;on_start();assert(!p_menu_cell && live==0 && mock_callbacks==0);on_stop();
    }
    fail_at=0;redraw_failed=true;on_start();assert(!p_menu_cell && live==0 && mock_callbacks==0);
    assert(recoveries==5);puts("Menu teardown/failure tests passed (1000 cycles)");return 0;
}
'''

status_head=r'''
typedef struct { int marker; } lv_font_t;
typedef struct { uint8_t font_size;lv_font_t *font; } font_cache_t;
static font_cache_t font_cache[10];
static unsigned font_count;
static lv_font_t *chinese_font;
static lv_obj_t *app_clock_main_status_bar,*status_bar_area_up,*status_bar_area_down,*app_clock_tileview;
static void lv_tiny_ttf_destroy(lv_font_t *font) { assert(!app_clock_main_status_bar && !status_bar_area_up && !status_bar_area_down);test_free(font); }
'''
status_test=r'''
int main(void) {
    lv_obj_t roots[3]={0};
    for(int i=0;i<100;i++) {
        app_clock_main_status_bar=lv_obj_create(&roots[0]);status_bar_area_up=lv_obj_create(&roots[1]);status_bar_area_down=lv_obj_create(&roots[2]);
        font_count=10;for(int f=0;f<10;f++)font_cache[f].font=test_calloc(1,sizeof(lv_font_t));
        app_clock_main_status_bar_deinit();app_clock_main_status_bar_deinit();assert(live==0 && !font_count);
    }
    puts("Status font ownership tests passed");return 0;
}
'''

OUT.mkdir(exist_ok=True)
for name, body in [('clock',clock_head+clock_functions+clock_test),('menu',menu_head+menu_functions+menu_test),
                   ('status',status_head+function(bar,'void app_clock_main_status_bar_deinit(void)')+status_test)]:
    (OUT/f'test_{name}_lifecycle.c').write_text(prefix+body,encoding='utf-8')
print('Generated lifecycle tests from current production functions')

plugin_prefix = r'''
typedef struct { int value; } app_clock_time_t;
typedef struct { struct { int w,h; } header; } lv_image_dsc_t;
typedef struct { int marker; } lv_timer_t;
static lv_image_dsc_t fake_image={{40,40}};
#define CACHE_CLOCK_HANDS 1
#define CACHE_ROTATE_BG 1
#define ENABLE_MASKED_IMAGE 1
#define LV_EXT_IMG_GET(x) (&fake_image)
#define a8_mask_300x200 fake_image
#define clock_simple_hour_hand fake_image
#define clock_simple_minute_hand fake_image
#define clock_simple_second_hand fake_image
#define ROTATE_MEM 0
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_HIDDEN 2
#define LV_OBJ_FLAG_CLICKABLE 4
#define LV_ALIGN_TOP_MID 0
#define LV_IMG_ZOOM_NONE 256
#define lv_image_create lv_obj_create
static void lv_image_set_src(lv_obj_t *o,const void *src){assert(o && src);o->src=src;}
static void lv_img_set_angle(lv_obj_t *o,int a){(void)a;assert(o);}
#define lv_image_set_pivot(...) ((void)0)
#define lv_img_set_pivot(...) ((void)0)
#define lv_img_set_zoom(...) ((void)0)
#define lv_obj_clear_flag(...) ((void)0)
#define lv_obj_add_flag(...) ((void)0)
#define lv_obj_set_style_bitmap_mask_src(...) ((void)0)
#define lv_obj_get_width(...) 390
#define lv_obj_get_height(...) 450
#define lv_obj_get_self_width(...) 40
#define lv_obj_get_self_height(...) 40
#define lv_obj_img_png_set_zoom(...) ((void)0)
static void app_clock_simple_redraw(lv_timer_t *t){(void)t;}
static void app_clock_dial_redraw(lv_timer_t *t){(void)t;}
static void app_clock_rotate_bg_redraw(lv_timer_t *t){(void)t;}
static lv_timer_t *lv_timer_create(void(*cb)(lv_timer_t*),int ms,void *data){(void)cb;(void)ms;(void)data;return test_calloc(1,sizeof(lv_timer_t));}
static void lv_timer_del(lv_timer_t *t){assert(t);test_free(t);}
static lv_image_dsc_t *app_cache_copy_alloc(const void *src,int mem){(void)src;(void)mem;return test_calloc(1,sizeof(lv_image_dsc_t));}
static void assert_no_image_ref(lv_obj_t *o,const void *src){for(;o;o=o->next){assert(o->src!=src);if(o->child)assert_no_image_ref(o->child,src);}}
static void app_cache_copy_free(lv_image_dsc_t *cache){assert_no_image_ref(screen.child,cache);test_free(cache);}
static void add_mask_event_cb(lv_event_t *e){(void)e;}
static void bg_img_event_cb(lv_event_t *e){(void)e;}
'''
for name in ['simple', 'dial', 'rotate_bg']:
    source = (BASE/f'clock/app_clock_{name}.c').read_text(encoding='utf-8')
    start = source.index('typedef struct')
    end = source.index(f'}} app_clock_{name}_t;', start)+len(f'}} app_clock_{name}_t;')
    types = source[start:end] + f'\nstatic app_clock_{name}_t *p_clk_{name};\n'
    signatures = ['static rt_int32_t init(lv_obj_t *parent)']
    if name != 'rotate_bg':
        signatures += ['static bool init_clock_hands_img(void)', 'static void deinit_clock_hands_img(void)']
    signatures += ['static rt_int32_t resume_callback(void)', 'static rt_int32_t pause_callback(void)', 'static rt_int32_t deinit(void)']
    funcs = '\n'.join(function(source, signature) for signature in signatures)
    test = r'''
int main(void) {
    alloc_at=0;fail_at=0;assert(init(&screen)==0);assert(resume_callback()==0);
    int points=alloc_at;deinit();deinit();lv_obj_clean(&screen);assert(live==0);
    for(int point=1;point<=points;point++) {
        alloc_at=0;fail_at=point;int result=init(&screen);
        if(!result)result=resume_callback();assert(result!=0);
        pause_callback();deinit();deinit();lv_obj_clean(&screen);assert(live==0);
    }
    printf("Clock plugin allocation failure tests passed: %d points\n",points);return 0;
}
'''
    (OUT/f'test_{name}_lifecycle.c').write_text(prefix+plugin_prefix+types+funcs+test,encoding='utf-8')
