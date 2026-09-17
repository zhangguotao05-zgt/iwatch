#define _CRT_SECURE_NO_WARNINGS
#include "lvgl.h"
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_class_private.h"
#include "src/core/lv_obj_style_private.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern void test_font_arm_failure(size_t index);
extern size_t test_font_live_bytes(void);
extern size_t test_font_live_blocks(void);
extern unsigned test_font_assert_count(void);

/* 此处只提供 RTOS 链表、内存和邮箱边界；被审查的函数来自 SDK 源文件。 */
typedef int rt_err_t;
typedef uintptr_t rt_ubase_t;
enum { RT_EOK, RT_ENOMEM, RT_EFULL, RT_EEMPTY, RT_EINVAL, GUI_APP_MSG_MANUAL_GOBACK_ANIM };
typedef struct rt_list { struct rt_list *next, *prev; } rt_list_t;
static void rt_list_init(rt_list_t *list) { list->next = list->prev = list; }
static void rt_list_insert_before(rt_list_t *list, rt_list_t *item)
{ item->prev = list->prev; item->next = list; list->prev->next = item; list->prev = item; }
static void rt_list_remove(rt_list_t *item) { item->prev->next = item->next; item->next->prev = item->prev; }
static unsigned rt_list_len(const rt_list_t *list)
{ unsigned n = 0; for (const rt_list_t *p = list->next; p != list; p = p->next) n++; return n; }
#define rt_list_entry(pointer, type, member) ((type *)((char *)(pointer) - offsetof(type, member)))
#define rt_list_first_entry(head, type, member) rt_list_entry((head)->next, type, member)
#define rt_list_for_each(pointer, head) for ((pointer) = (head)->next; (pointer) != (head); (pointer) = (pointer)->next)
#define RT_ALIGN_SIZE 8u
#define RT_ALIGN(value, alignment) (((value) + (alignment) - 1u) & ~((alignment) - 1u))
#define rt_memcpy memcpy
#define rt_strcmp strcmp
#define rt_strncpy strncpy
#define rt_malloc lv_malloc
#define rt_free lv_free
#define app_sche_malloc lv_malloc
#define app_sche_free lv_free
#define app_sche_d(...) ((void)0)
#define TRANS_ANIMATION 1

typedef struct { uint32_t tick, msg_id; } gui_app_msg_t;
typedef struct { unsigned entry; gui_app_msg_t *message; } mailbox_t;
static mailbox_t gui_app_mbx, *task_msg_mbx;
static bool full_mailbox, input_enabled = true, pointer_enabled = true;
static uint32_t rt_tick_get(void) { return 42; }
static rt_err_t rt_mb_send(mailbox_t *box, rt_ubase_t value)
{
    if (full_mailbox || box->entry) return -RT_EFULL;
    box->message = (gui_app_msg_t *)value; box->entry = 1;
    return RT_EOK;
}
static void gui_app_enable_input_device(bool enabled) { input_enabled = pointer_enabled = enabled; }
static void gui_app_enable_input_device_except_tp(bool enabled) { input_enabled = enabled; }
#include "sdk_send_under_test.inc"

typedef void (*gui_page_msg_cb_t)(int, void *);
typedef enum { page_st_created, page_st_resumed, page_st_paused, page_st_stoped } page_state_enum;
typedef struct running_app { rt_list_t node, page_list; char id[16]; unsigned state, target_state; } gui_runing_app_t;
typedef struct _subpage_node {
    rt_list_t node;
    gui_runing_app_t *parent;
    lv_obj_t *scr;
    void *mem_ptr, *user_data;
    char name[16];
    gui_page_msg_cb_t msg_handler;
    page_state_enum state, target_state;
    unsigned a_exit, a_enter;
} subpage_node_t;
typedef struct {
    char app_id[16], page_id[16], back_page_id[16];
    void *user_data, *back_user_data;
    uint16_t running_apps, page_count;
    bool busy, back_valid, resumed, transitioning;
} gui_app_route_snapshot_t;
static rt_list_t running_app_list, *actived_app;
static unsigned scheduler_loop_st, suspend_task, running_root_stop_others;
static bool trans_anim_playing;
static unsigned state_changes, resumes;
static void gui_app_init_anim(unsigned *animation) { *animation = 0; }
static void app_set_all_page_state(gui_runing_app_t *app, page_state_enum state)
{
    rt_list_t *item;
    rt_list_for_each(item, &app->page_list) rt_list_entry(item, subpage_node_t, node)->target_state = state;
    state_changes++;
}
static void page_set_target_state(subpage_node_t *page, page_state_enum state) { page->target_state = state; }
static void app_resume(rt_list_t *node) { assert(node == actived_app); resumes++; }
#include "route_screen_under_test.inc"
static lv_obj_t *port_app_sche_create_scr(void) { return route_screen_create(); }
#include "sdk_create_under_test.inc"
#include "sdk_snapshot_under_test.inc"
static void page_callback(int event, void *context) { (void)event; (void)context; }

int test_sdk_navigation(void)
{
    size_t bytes = test_font_live_bytes(), blocks = test_font_live_blocks();
    gui_app_msg_t message = {0};
    test_font_arm_failure(1);
    assert(send_msg_to_gui_app_task(&message) == -RT_ENOMEM);
    test_font_arm_failure(0);
    assert(!gui_app_mbx.entry && input_enabled && pointer_enabled);
    full_mailbox = true;
    assert(send_msg_to_gui_app_task(&message) == -RT_EFULL);
    assert(!gui_app_mbx.entry && input_enabled && pointer_enabled);
    full_mailbox = false;
    for (unsigned manual = 0; manual < 2; manual++) {
        message.msg_id = manual ? GUI_APP_MSG_MANUAL_GOBACK_ANIM : 0;
        assert(send_msg_to_gui_app_task(&message) == RT_EOK);
        assert(gui_app_mbx.entry == 1 && gui_app_mbx.message != &message);
        assert(gui_app_mbx.message->tick == 42 && !input_enabled && pointer_enabled == (manual != 0));
        lv_free(gui_app_mbx.message); memset(&gui_app_mbx, 0, sizeof(gui_app_mbx));
        gui_app_enable_input_device(true);
    }

    gui_runing_app_t app = {0};
    subpage_node_t root = {.state = page_st_resumed, .target_state = page_st_resumed, .name = "root"};
    memcpy(app.id, "Main", 5);
    rt_list_init(&running_app_list); rt_list_init(&app.page_list);
    rt_list_insert_before(&running_app_list, &app.node);
    rt_list_insert_before(&app.page_list, &root.node);
    actived_app = &app.node;
    assert(app_create_page(&app.node, "test", page_callback, NULL, UINT32_MAX) == -RT_EINVAL);
    /* 节点、样式、屏幕及屏幕表四个真实分配点，失败均不暂停原页。 */
    for (unsigned point = 1; point <= 4; point++) {
        test_font_arm_failure(point);
        assert(app_create_page(&app.node, "test", page_callback, NULL, 0) != RT_EOK);
        test_font_arm_failure(0);
        assert(rt_list_len(&app.page_list) == 1 && !state_changes && !resumes);
        assert(root.state == page_st_resumed && root.target_state == page_st_resumed);
        assert(test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    }
    gui_app_route_snapshot_t snapshot;
    app_schedule_route_snapshot(&snapshot);
    assert(snapshot.running_apps == 1 && snapshot.page_count == 1 && snapshot.resumed && !snapshot.busy);
    scheduler_loop_st = 1; app_schedule_route_snapshot(&snapshot); assert(snapshot.busy); scheduler_loop_st = 0;
    suspend_task = 1; app_schedule_route_snapshot(&snapshot); assert(snapshot.busy); suspend_task = 0;
    trans_anim_playing = true; app_schedule_route_snapshot(&snapshot); assert(snapshot.busy && snapshot.transitioning); trans_anim_playing = false;
    task_msg_mbx = &gui_app_mbx; gui_app_mbx.entry = 1;
    app_schedule_route_snapshot(&snapshot); assert(snapshot.busy); gui_app_mbx.entry = 0;
    for (unsigned i = 1; i < 8; i++) assert(app_create_page(&app.node, "test", page_callback, NULL, 16) == RT_EOK);
    unsigned changes = state_changes;
    assert(app_create_page(&app.node, "ninth", page_callback, NULL, 0) == -RT_EFULL && state_changes == changes);
    app_schedule_route_snapshot(&snapshot);
    assert(snapshot.page_count == 8 && snapshot.back_valid && snapshot.busy && !strcmp(snapshot.back_page_id, "test"));
    while (rt_list_len(&app.page_list) > 1) {
        subpage_node_t *page = rt_list_entry(app.page_list.prev, subpage_node_t, node);
        assert(page->mem_ptr == (uint8_t *)page + RT_ALIGN(sizeof(*page), RT_ALIGN_SIZE));
        rt_list_remove(&page->node); lv_obj_delete(page->scr); lv_free(page);
    }
    root.target_state = root.state;
    app_schedule_route_snapshot(&snapshot); assert(!snapshot.busy && !snapshot.back_valid);
    assert(!test_font_assert_count() && test_font_live_bytes() == bytes && test_font_live_blocks() == blocks);
    puts("SDK navigation: queue ownership, 4 allocation failures, real stack depth and snapshots; asserts=0 result=ok");
    return 0;
}
