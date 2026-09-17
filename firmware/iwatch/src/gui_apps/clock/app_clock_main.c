/*
 * SPDX-FileCopyrightText: 2019-2026 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rtconfig.h"
#include "app_clock_main.h"
#include "app_clock_status_bar.h"
#include "iw_recovery.h"
#include "iw_gui_port.h"
#include "iw_service_runtime.h"
#include "iw_font.h"
/* 字体故障回收要求所有 LVGL 绘制回调在 GUI 线程内同步完成。 */
#if LV_USE_OS != LV_OS_NONE
    #error "iwatch font teardown requires synchronous LVGL rendering"
#endif
/* 固定 SDK 的 lv_lcd.c 提供此接口，但未在公共头文件中声明。 */
extern bool lv_refreshing_done(void);
// #include "lvsf.h"
#ifdef RT_USING_XIP_MODULE
    #include "dlmodule.h"
    #include "dlfcn.h"
    #include "dfs_posix.h"
#endif /* RT_USING_XIP_MODULE */

LV_IMG_DECLARE(img_clock);

#define APP_ID  "clock"
#define APP_CLOCK_ID_MAX_LEN 8
#define APP_CLOCK_MAX_COUNT 16
#define CLOCK_UPDATE_INTERVAL_IN_MS 100

typedef enum
{
    STATE_DEINIT = 0,
    STATE_PAUSED, /*clock be inited and paused*/
    STATE_ACTIVE,
} CLOCK_STATE;

/**
 *  description of one clock
 *
 */
typedef struct
{
    lv_obj_t *parent;               //!< clock's root parent obj
    char id[APP_CLOCK_ID_MAX_LEN + 1]; //!< clock's name
    const app_clock_ops_t   *ops;          //!< clock UI state cbk func
    uint8_t state;
    rt_list_t node;                  //!< list node for link all clocks
    void *mod;
} app_clock_desc_t;

/**
 *
 *
 */
typedef struct
{
    rt_uint32_t app_clock_list_len;
    rt_list_t list;
    lv_obj_t *tileview; /* 仅持有本页创建的对象，不持有框架屏幕。 */
    bool ready;
    bool stopping;
    bool registration_failed;
} app_clock_main_t;

#ifndef BSP_USING_LVGL_INPUT_AGENT
    static
#endif
app_clock_main_t *p_app_clock_main = NULL;
static uint16_t last_active_clock = 0;

static rt_uint16_t get_active_tile_col(lv_obj_t *tileview)
{
    if (!tileview) return 0;
    lv_obj_t *active_tile = lv_tileview_get_tile_active(tileview);
    int32_t w = lv_obj_get_content_width(tileview);
    if (!active_tile || w <= 0) return 0;
    int32_t x = lv_obj_get_x(active_tile);
    return x < 0 ? 0 : (rt_uint16_t)((x + w / 2) / w);
}

void app_clock_main_get_current_time(app_clock_time_t *t)
{
    iw_clock_snapshot_t clock;
    uint64_t day_ms;

    if (!t) return;
    memset(t, 0, sizeof(*t));
    if (!iw_clock_read(&clock)) return;

    if (clock.valid)
    {
        int64_t local_ms = clock.utc_ms + (int64_t)clock.offset_minutes * INT64_C(60000);
        int64_t wrapped = local_ms % INT64_C(86400000);
        if (wrapped < 0) wrapped += INT64_C(86400000);
        day_ms = (uint64_t)wrapped;
    }
    else
    {
        /* RTC 无效时只展示本次启动的单调时间，不能伪造一个已校准日期。 */
        day_ms = clock.mono_ms % UINT64_C(86400000);
    }
    t->ms = (uint16_t)(day_ms % 1000u);
    t->s = (uint8_t)((day_ms / 1000u) % 60u);
    t->m = (uint8_t)((day_ms / 60000u) % 60u);
    t->h = (uint8_t)((day_ms / 3600000u) % 24u);


#ifdef GRAPHIC_REFRESH_TIME_ANALYSIS
        if (refer_ana_enable)
        {
            /*
                clock analysis, fix clock time to 10:10:37

                and invalid screen
            */
            t->h = 10;
            t->m = 10;
            t->s = 37;


            {
                lv_disp_t *disp;
                lv_area_t scr_area;

                disp = lv_disp_get_default();

                scr_area.x1 = 0;
                scr_area.y1 = 0;
                scr_area.x2 = lv_disp_get_hor_res(disp) - 1;
                scr_area.y2 = lv_disp_get_ver_res(disp) - 1;

                _lv_inv_area(disp, &scr_area);
            }

        }
#endif

}

#if 0
void app_clock_main_tick(void *param)
{
    total_milliseconds += CLOCK_UPDATE_INTERVAL_IN_MS;
}
#endif

static const char *app_clock_state_to_name(uint8_t state)
{
#define STATE_TO_NAME_CASE(e) case e: return #e
    switch (state)
    {
        STATE_TO_NAME_CASE(STATE_DEINIT);
        STATE_TO_NAME_CASE(STATE_PAUSED);
        STATE_TO_NAME_CASE(STATE_ACTIVE);

    default:
        return "UNKNOW";


    }
}

static char *change_context;
static lv_obj_t *clk_parent;
char *app_clock_change_context(void)
{
    return change_context;
}

lv_obj_t *gui_app_get_clock_parent(void)
{
    return clk_parent;
}

static void app_clock_change_state(app_clock_desc_t *p_clock, uint8_t new_state)
{
    rt_int32_t result = RT_EOK;
    if (!p_clock || p_clock->state == new_state) return;
    if (!p_clock->parent || !p_clock->ops) return;
    change_context = p_clock->id;
    clk_parent = p_clock->parent;

    if (new_state == STATE_DEINIT)
    {
        if (p_clock->ops->pause) p_clock->ops->pause();
        if (p_clock->ops->deinit) p_clock->ops->deinit();
        lv_obj_clean(p_clock->parent);
    }
    else
    {
        if (p_clock->state == STATE_DEINIT && p_clock->ops->init)
            result = p_clock->ops->init(p_clock->parent);
        if (result == RT_EOK && new_state == STATE_ACTIVE && p_clock->ops->resume)
            result = p_clock->ops->resume();
        if (result == RT_EOK && new_state == STATE_PAUSED && p_clock->state == STATE_ACTIVE && p_clock->ops->pause)
            result = p_clock->ops->pause();
    }
    if (result != RT_EOK)
    {
        /* 部分创建也走同一回收路径，失败状态不能发布为活动表盘。 */
        if (p_clock->ops->pause) p_clock->ops->pause();
        if (p_clock->ops->deinit) p_clock->ops->deinit();
        lv_obj_clean(p_clock->parent);
        p_clock->state = STATE_DEINIT;
        rt_kprintf("[clock] %s create/resume failed: %d\n", p_clock->id, result);
        if (new_state == STATE_ACTIVE) iw_recovery_show(APP_ID);
    }
    else
    {
        p_clock->state = new_state;
        if (new_state == STATE_ACTIVE) iw_recovery_hide(APP_ID);
    }
    /* 插件退出后不向外暴露描述符内存及即将删除的父对象。 */
    change_context = NULL;
    clk_parent = NULL;
}


static void app_clock_change_state_by_id(uint16_t idx, uint8_t new_state)
{
    if (!p_app_clock_main || !p_app_clock_main->ready || p_app_clock_main->stopping || !p_app_clock_main->app_clock_list_len) return;
    rt_kprintf("Change state for clock at index %d to %s\n", idx, app_clock_state_to_name(new_state));
    uint16_t i = 0;
    rt_list_t *pos;
    rt_list_for_each(pos, (&p_app_clock_main->list))
    {
        app_clock_desc_t *clk_desc = rt_list_entry(pos, app_clock_desc_t, node);

        if (idx == i)
        {
            app_clock_change_state(clk_desc, new_state);
            break;
        }
        i++;
    }
}

static void app_clock_main_select(uint16_t clock_idx)
{
    if (!p_app_clock_main || !p_app_clock_main->ready || p_app_clock_main->stopping || !p_app_clock_main->app_clock_list_len) return;
    rt_uint16_t left_clock_idx, right_clock_idx, i;
    rt_list_t *pos;
    app_clock_desc_t *clk_desc;

    if (clock_idx >= p_app_clock_main->app_clock_list_len)
        clock_idx = p_app_clock_main->app_clock_list_len - 1;

    if (clock_idx > 0)
        left_clock_idx = clock_idx - 1;
    else
        left_clock_idx = p_app_clock_main->app_clock_list_len; //invalid left clock

    right_clock_idx = clock_idx + 1;

    if (right_clock_idx > p_app_clock_main->app_clock_list_len)
        right_clock_idx = p_app_clock_main->app_clock_list_len; //invalid right clock

    /*deinit all other clock , to free memory*/
    i = 0;
    rt_list_for_each(pos, (&p_app_clock_main->list))
    {
        clk_desc = rt_list_entry(pos, app_clock_desc_t, node);

        if ((i != clock_idx) && (i != left_clock_idx) && (i != right_clock_idx))
        {
            app_clock_change_state(clk_desc, STATE_DEINIT);
        }
        i++;
    }

    /*pause left&right clock , to free memory*/
    i = 0;
    rt_list_for_each(pos, (&p_app_clock_main->list))
    {
        clk_desc = rt_list_entry(pos, app_clock_desc_t, node);

        if ((i == left_clock_idx) || (i == right_clock_idx))
        {
            app_clock_change_state(clk_desc, STATE_PAUSED);
        }
        i++;
    }

    /* active selected clock */
    i = 0;
    rt_list_for_each(pos, (&p_app_clock_main->list))
    {
        clk_desc = rt_list_entry(pos, app_clock_desc_t, node);

        if (i == clock_idx)
        {
            app_clock_change_state(clk_desc, STATE_ACTIVE);
        }

        i++;
    }

    last_active_clock = clock_idx;
}

static void app_clock_main_drag_begin(uint16_t clock_idx)
{
    if (!p_app_clock_main || !p_app_clock_main->ready || p_app_clock_main->stopping || !p_app_clock_main->app_clock_list_len) return;
    rt_uint16_t left_clock_idx, right_clock_idx, i;
    rt_list_t *pos;
    app_clock_desc_t *clk_desc;

    if (clock_idx >= p_app_clock_main->app_clock_list_len)
        clock_idx = p_app_clock_main->app_clock_list_len - 1;

    if (clock_idx > 0)
        left_clock_idx = clock_idx - 1;
    else
        left_clock_idx = p_app_clock_main->app_clock_list_len; //invalid left clock

    right_clock_idx = clock_idx + 1;

    if (right_clock_idx > p_app_clock_main->app_clock_list_len)
        right_clock_idx = p_app_clock_main->app_clock_list_len; //invalid right clock


    /* active the left&right clock*/
    i = 0;
    rt_list_for_each(pos, (&p_app_clock_main->list))
    {
        clk_desc = rt_list_entry(pos, app_clock_desc_t, node);

        if ((i == left_clock_idx) || (i == right_clock_idx))
        {
            app_clock_change_state(clk_desc, STATE_ACTIVE);
        }

        i++;
    }
}


static void tileview_event_cb_t(lv_event_t *event)
{
    if (!p_app_clock_main || !p_app_clock_main->ready || p_app_clock_main->stopping) return;
    //if (event->code != LV_EVENT_PRESSING)
    //    rt_kprintf("tileview_event_cb_t %s\n", lv_event_to_name(event->code));

    switch (event->code)
    {
    case LV_EVENT_VALUE_CHANGED:
    {
        rt_uint16_t active_pos = get_active_tile_col(lv_event_get_current_target(event));
        rt_kprintf("tileview_event_cb_t LV_EVENT_VALUE_CHANGED, active_pos=%d\n", active_pos);

        if (gui_app_is_actived(APP_ID)) //value_changed could be sent after app paused
            app_clock_main_select(active_pos);
        else
            last_active_clock = active_pos;
    }
    break;

#if (LV_HOR_RES_MAX == 240) && (LV_HOR_RES_MAX == 240) //Active neighbor clock may cause malloc mem failure in high resolution
    case LV_EVENT_SCROLL_BEGIN:
    {
        app_clock_main_drag_begin(last_active_clock);
    }
    break;
#endif

    default:
        break;
    }
}


static void app_clock_main_init(void)
{
    uint16_t i = 0;
    rt_list_t *pos;
    lv_obj_t *tileview = lv_tileview_create(lv_scr_act());
    if (!tileview) return;
    p_app_clock_main->tileview = tileview;
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(tileview, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_COVER, LV_PART_MAIN);
    rt_list_for_each(pos, &p_app_clock_main->list)
    {
        app_clock_desc_t *desc = rt_list_entry(pos, app_clock_desc_t, node);
        desc->parent = lv_tileview_add_tile(tileview, i++, 0, LV_DIR_HOR);
        if (!desc->parent) return;
        lv_obj_set_scrollbar_mode(desc->parent, LV_SCROLLBAR_MODE_OFF);
    }
    if (last_active_clock >= i) last_active_clock = 0;
    lv_obj_set_tile_id(tileview, last_active_clock, 0, false);
    lv_obj_add_event_cb(tileview, tileview_event_cb_t, LV_EVENT_ALL, NULL);
    if (!app_clock_main_status_bar_init(lv_scr_act(), tileview)) return;
    p_app_clock_main->ready = true;
}

#ifdef RT_USING_XIP_MODULE
static void app_clock_load_one_dyn_wf(const char *module_name, const char *path)
{
    struct rt_dlmodule *mod;
    uint32_t old_len;
    app_clock_desc_t *clk_desc;

    old_len = p_app_clock_main->app_clock_list_len;

    mod = dlrun(module_name, path);

    if (mod && (old_len != p_app_clock_main->app_clock_list_len))
    {
        clk_desc = rt_list_tail_entry(&p_app_clock_main->list, app_clock_desc_t, node);
        clk_desc->mod = mod;
    }
}

static void app_clock_load_dyn_wf(void)
{
    DIR *dir;
    struct dirent *dir_entry;
    struct stat *ent_stat;
    char *full_path;
    const char *path = "watchface";
    uint32_t name_len;

    dir = opendir(path);
    if (!dir)
    {
        return;

    }
    ent_stat = rt_malloc(sizeof(*ent_stat));
    RT_ASSERT(stat);

    do
    {
        dir_entry = readdir(dir);
        if (!dir_entry)
        {
            break;
        }

        memset(ent_stat, 0, sizeof(*ent_stat));

        /* build full path for each file */
        full_path = dfs_normalize_path(path, dir_entry->d_name);
        if (full_path == NULL)
        {
            break;
        }

        if (stat(full_path, ent_stat) == 0)
        {
            if (!S_ISDIR(ent_stat->st_mode))
            {
                name_len = strlen(dir_entry->d_name);
                if (('m' == dir_entry->d_name[name_len - 1])
                        && ('.' == dir_entry->d_name[name_len - 2])) /* ending with .m */
                {
                    /* remove suffix .m */
                    dir_entry->d_name[name_len - 2] = 0;
                    app_clock_load_one_dyn_wf(dir_entry->d_name, path);
                }
            }
        }
        rt_free(full_path);
    }
    while (true);

    rt_free(ent_stat);
    closedir(dir);
}

#endif /* RT_USING_XIP_MODULE */

/**********************regist app clock to app manager****************************/
extern void app_clock_rotate_bg_register(void);
#if 1//!(defined(PKG_USING_MICROPYTHON)||defined(PKG_USING_QUICKJS))
    extern void app_clock_simple_register(void);
    extern void app_clock_dial_register(void);

#endif /* defined(PKG_USING_MICROPYTHON)||defined(PKG_USING_QUICKJS)*/

#ifdef PKG_USING_FFMPEG
    extern void app_clock_video_audio_register(void);
#endif /* PKG_USING_FFMPEG */
#if PKG_USING_STREAMING_MEDIA_DEMO_APP
    extern void app_clock_streamingmedia_register(void);
#endif
void app_clock_reset_time(void)
{
    iw_clock_snapshot_t snapshot;
    /* 保留 SDK 公开符号；实际时间由 iw_time owner 连续维护。 */
    (void)iw_clock_read(&snapshot);
}


static void on_stop(void);

bool app_clock_main_process_font_fault(void)
{
    if (!iw_font_fault_pending()) return false;
    /* 仅在 GUI 线程、LVGL 回调之外执行；GPU 和 LCD 排空前保持全部资源存活。 */
    if (!lv_refreshing_done()) return true;
    on_stop();
    if (!iw_font_ack_fault()) return true;
    iw_font_note_fallback();
    iw_recovery_show(APP_ID);
    return false;
}

static void on_start(void)
{
    if (p_app_clock_main) return;
    /* 先建立管理器，再逐个注册；任何一步失败都统一回滚。 */
    p_app_clock_main = (app_clock_main_t *) rt_malloc(sizeof(app_clock_main_t));
    if (!p_app_clock_main) { iw_recovery_show(APP_ID); return; }
    memset(p_app_clock_main, 0, sizeof(app_clock_main_t));
    rt_list_init(&p_app_clock_main->list);
#if (LV_HOR_RES_MAX < 512)&&(LV_VER_RES_MAX < 512)
    app_clock_rotate_bg_register();
#endif

#if 1//!(defined(PKG_USING_MICROPYTHON)||defined(PKG_USING_QUICKJS))
    app_clock_simple_register();
    app_clock_dial_register();

#endif /* defined(PKG_USING_MICROPYTHON)||defined(PKG_USING_QUICKJS) */
    gui_script_watch_face_register(SCRIPT_TYPE_QJS);
    gui_script_watch_face_register(SCRIPT_TYPE_MPY);

#ifdef RT_USING_XIP_MODULE
    app_clock_load_dyn_wf();
#endif /* RT_USING_XIP_MODULE */

    app_clock_reset_time();

    if (!p_app_clock_main->registration_failed && p_app_clock_main->app_clock_list_len)
        app_clock_main_init();
    if (!p_app_clock_main->ready)
    {
        on_stop();
        iw_recovery_show(APP_ID);
    }
}

static void on_resume(void)
{
    if (!p_app_clock_main) { iw_recovery_show(APP_ID); return; }

    app_clock_main_select(last_active_clock);
}

static void on_pause(void)
{
    iw_gui_cancel_input();
    iw_recovery_hide(APP_ID);
    if (!p_app_clock_main || p_app_clock_main->stopping) return;
    rt_list_t *pos;
    uint16_t i = 0;

    rt_list_for_each(pos, (&p_app_clock_main->list))
    {
        app_clock_desc_t *clk_desc;
        clk_desc = rt_list_entry(pos, app_clock_desc_t, node);
        app_clock_change_state(clk_desc, STATE_DEINIT);
    }
}

static void on_stop(void)
{
    app_clock_main_t *manager = p_app_clock_main;
    iw_recovery_hide(APP_ID);
    if (!manager || manager->stopping) return;
    manager->stopping = true;
    iw_gui_cancel_input();
    if (manager->tileview)
        lv_obj_remove_event_cb(manager->tileview, tileview_event_cb_t);
    /* 先摘链再释放，禁止让遍历宏读取已经释放的 node->next。 */
    while (!rt_list_isempty(&manager->list))
    {
        app_clock_desc_t *desc = rt_list_entry(manager->list.next, app_clock_desc_t, node);
        rt_list_remove(&desc->node);
        app_clock_change_state(desc, STATE_DEINIT);
#ifdef RT_USING_XIP_MODULE
        if (desc->mod) dlclose(desc->mod);
#endif
        rt_free(desc);
    }
    app_clock_main_status_bar_deinit();
    if (manager->tileview) lv_obj_delete(manager->tileview);
    change_context = NULL;
    clk_parent = NULL;
    p_app_clock_main = NULL;
    rt_free(manager);
}



static void msg_handler(gui_app_msg_type_t msg, void *param)
{
    switch (msg)
    {
    case GUI_APP_MSG_ONSTART:
        on_start();
        break;

    case GUI_APP_MSG_ONRESUME:
        on_resume();
        break;

    case GUI_APP_MSG_ONPAUSE:
        on_pause();
        break;

    case GUI_APP_MSG_ONSTOP:
        on_stop();
        break;
    default:
        break;
    }
}


static int app_main(intent_t i)
{
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    uint32_t active = intent_get_uint32(i, "active", 0xFFFF);
    if (active != 0xFFFF)
        last_active_clock = active;

    return 0;
}


BUILTIN_APP_EXPORT(LV_EXT_STR_ID(clock), LV_EXT_IMG_GET(img_clock), APP_ID, app_main, 1);


/**********************app clocks manager**************************/
int32_t app_clock_register(const char *id, const app_clock_ops_t *operations)
{
    if (!p_app_clock_main || p_app_clock_main->ready || p_app_clock_main->stopping || !id || !*id || !operations)
        return -RT_EINVAL;
    if (p_app_clock_main->app_clock_list_len >= APP_CLOCK_MAX_COUNT)
    {
        p_app_clock_main->registration_failed = true;
        return -RT_ENOMEM;
    }
    app_clock_desc_t *new_clock = rt_calloc(1, sizeof(*new_clock));
    if (!new_clock)
    {
        p_app_clock_main->registration_failed = true;
        return -RT_ENOMEM;
    }
    /* 保持 SDK 的八字节表盘标识兼容性。 */
    rt_strncpy(new_clock->id, id, APP_CLOCK_ID_MAX_LEN);
    new_clock->ops = operations;
    rt_list_init(&new_clock->node);
    rt_list_insert_before(&p_app_clock_main->list, &new_clock->node);
    p_app_clock_main->app_clock_list_len++;
    return RT_EOK;
}

#if 0
/**
 * duplicate an image to SRAM to improve drawn performance
 * \n
 *
 * @return
 * @param copy
 * \n
 * @see
 */
lv_img_dsc_t *app_clock_img_cache_malloc(const void *copy)
{
    lv_res_t res;
    lv_img_cache_entry_t *cache_entry;

    if (NULL == copy) return NULL;

    /* Allocate image descriptor */
    lv_img_dsc_t *dsc = lv_mem_alloc(sizeof(lv_img_dsc_t));
    if (dsc == NULL)
        return NULL;

    cache_entry = _lv_img_cache_open(copy, LV_COLOR_BLACK);
    RT_ASSERT(cache_entry);

    memcpy(&dsc->header, &cache_entry->dec_dsc.header, sizeof(dsc->header));

    /* Allocate raw buffer */
    dsc->data = lv_mem_alloc(cache_entry->dec_dsc.img_data_size);
    if (dsc->data == NULL)
    {
        lv_mem_free(dsc);
        return NULL;
    }

    memcpy((uint8_t *)dsc->data, (uint8_t *)cache_entry->dec_dsc.img_data, cache_entry->dec_dsc.img_data_size);

    return dsc;
}

void app_clock_img_cache_free(lv_img_dsc_t *p_img)
{
    if (NULL != p_img)
    {
        lv_img_cache_invalidate_src(p_img);

        if (NULL != p_img->data)
            lv_mem_free(p_img->data);

        lv_mem_free(p_img);
    }
}
#endif
