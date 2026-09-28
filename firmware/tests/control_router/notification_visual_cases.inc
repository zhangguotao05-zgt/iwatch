/* 实际 LVGL 指针读取触发生产 View，再进入真实控制器和路由；无直接动作替代。 */
extern void test_component_capture(lv_display_t *, lv_obj_t *, unsigned);
extern void dynamic_cache_fail_next(bool);
static lv_indev_data_t notice_pointer_data;
static void notice_pointer_read(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input;
    *data = notice_pointer_data;
}
static void notice_sample(lv_indev_t *input, int x, int y, bool down, unsigned elapsed)
{
    notice_pointer_data = (lv_indev_data_t){.point = {x, y},
        .state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED};
    test_tick_ms += elapsed;
    lv_indev_read(input);
}
static void notice_tap(lv_indev_t *input, int x, int y)
{
    notice_sample(input, x, y, true, 20);
    notice_sample(input, x, y, false, 20);
    (void)iw_product_process();
}
static void notice_capture(lv_display_t *display, iw_product_page_t *page, unsigned id)
{
    test_component_capture(display, iw_screen_frame_content(&page->view.frame), id);
}
static void notification_visual_case(lv_display_t *display)
{
    lv_indev_t *pointer = lv_indev_create();
    assert(pointer);
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, notice_pointer_read);
    iw_product_page_t *list = notification_open();
    notice_capture(display, list, 110);
    notice_tap(pointer, 195, 402);
    settle_dynamic("visual_empty_back");
    assert(depth == 1u);
    list = notification_open();
    for (unsigned i = 0; i < IW_NOTIFICATION_CAPACITY; ++i)
        assert(iw_product_notification_add(IW_NOTIFICATION_DIAGNOSTIC, "本机测试通知", strlen("本机测试通知")));
    (void)iw_product_process();
    const iw_notification_store_t *store = list->model.notifications;
    assert(store->count == 32u && list->view.scene.count <= IW_PRODUCT_NODES);
    printf("ASSERT notification node_bytes=%zu scene_bytes=%zu nodes=%u full_count=%u\n",
           sizeof(iw_product_node_t), sizeof(iw_product_scene_t), list->view.scene.count, store->count);
    notice_capture(display, list, 111);
    /* 同一阈值事件既移动又到时，必须先取消，回原点也不能重新计时。 */
    notice_sample(pointer, 190, 338, true, 20);
    notice_sample(pointer, 204, 338, true, 720);
    notice_sample(pointer, 190, 338, true, 720);
    notice_sample(pointer, 190, 338, false, 20);
    assert(!list->model.notification_clear_confirm && depth == 2u);
    /* 让释放点恰落在新确认页清除按钮上，验证长按不穿透。 */
    uint32_t revision = store->last_revision;
    notice_sample(pointer, 190, 338, true, 20);
    notice_sample(pointer, 190, 338, true, 720);
    assert(list->model.notification_clear_confirm);
    uint32_t token = list->notification_clear_revision;
    (void)iw_product_process();
    notice_sample(pointer, 190, 338, true, 720);
    assert(list->notification_clear_revision == token);
    notice_sample(pointer, 190, 338, false, 20);
    assert(store->last_revision == revision && store->count == 32u);
    notice_capture(display, list, 112);
    notice_tap(pointer, 195, 402);
    assert(!list->model.notification_clear_confirm && store->last_revision == revision);
    /* 顶部圆钮外接矩形角落不命中；普通区域以外不能长按清除。 */
    assert(iw_product_scene_hit(&list->view.scene, 24, 15, 0) == -1);
    notice_sample(pointer, 10, 180, true, 20);
    notice_sample(pointer, 10, 180, true, 720);
    notice_sample(pointer, 10, 180, false, 20);
    assert(!list->model.notification_clear_confirm);
    /* 暂停/重新激活清掉本次手势，不允许恢复后续按触发确认。 */
    notice_sample(pointer, 190, 150, true, 20);
    iw_product_view_activate(&list->view, false);
    iw_product_view_activate(&list->view, true);
    notice_sample(pointer, 190, 150, true, 720);
    notice_sample(pointer, 190, 150, false, 20);
    assert(!list->model.notification_clear_confirm && depth == 2u);
    /* 最末条滚到屏内后经实际命中打开，删除只作用该 id。 */
    iw_product_view_scroll(&list->view, INT16_MAX);
    uint32_t last_id = list->model.notification_ids[31];
    const iw_product_node_t *last = NULL;
    for (unsigned i = 0; i < list->view.scene.count; ++i)
        if (list->view.scene.nodes[i].action == IW_ACTION_NOTIFICATION_OPEN_BASE + 31u)
            last = &list->view.scene.nodes[i];
    assert(last);
    int y = last->y + last->height / 2 - list->view.scroll_y;
    assert(y >= 105 && y < 450);
    notice_capture(display, list, 113);
    notice_tap(pointer, 190, y);
    settle_dynamic("visual_last_detail");
    iw_product_page_t *detail = current_product()->product;
    assert(detail->page_id == IW_PAGE_NOTIFICATION_DETAIL && detail->argument == last_id);
    notice_capture(display, detail, 114);
    iw_product_view_scroll(&detail->view, INT16_MAX);
    notice_capture(display, detail, 115);
    int delete_y = -1;
    for (unsigned i = 0; i < detail->view.scene.count; ++i) {
        const iw_product_node_t *node = &detail->view.scene.nodes[i];
        if (node->action == IW_ACTION_NOTIFICATION_DELETE)
            delete_y = node->y + node->height / 2 - detail->view.scroll_y;
    }
    assert(delete_y >= 105 && delete_y < 450);
    notice_tap(pointer, 190, delete_y);
    settle_dynamic("visual_single_delete");
    assert(current_product()->product == list && !iw_notification_find(store, last_id) && store->count == 31u);
    iw_product_view_scroll(&list->view, -INT16_MAX);
    notice_sample(pointer, 190, 150, true, 20);
    notice_sample(pointer, 190, 150, true, 720);
    notice_sample(pointer, 190, 150, false, 20);
    (void)iw_product_process();
    assert(list->model.notification_clear_confirm);
    notice_tap(pointer, 53, 44);
    assert(!list->model.notification_clear_confirm && store->count == 31u);
    notice_sample(pointer, 190, 150, true, 20);
    notice_sample(pointer, 190, 150, true, 720);
    notice_sample(pointer, 190, 150, false, 20);
    (void)iw_product_process();
    notice_tap(pointer, 195, 338);
    assert(!store->count && !list->model.notification_clear_confirm);
    notice_capture(display, list, 116);
    notice_tap(pointer, 53, 44);
    settle_dynamic("visual_empty_close");
    assert(depth == 1u);
    lv_indev_delete(pointer);
    puts("ASSERT notification visual TARGET four_states=1 actual_pointer=1 full32=1 long_hold=1 drag_cancel=1 release_consumed=1 owner_cancel=1 single_delete=1 clear_cancel=1 clear_confirm=1");
}

static void notification_mixed_visual_case(lv_display_t *display)
{
    /* 真实闹钟到期后经账本 API 挂起：关键记录保留，但不抢占当前列表。 */
    (void)iw_service_advance(&service, test_tick_ms);
    iw_clock_snapshot_t clock;
    iw_calendar_fields_t local;
    assert(iw_time_read(&service_time, &clock));
    iw_clock_snapshot_t next = clock;
    next.utc_ms += 60000;
    assert(iw_clock_local_fields(&next, &local));
    iw_alarm_edit_t edit = {.hour=local.hour, .minute=local.minute, .enabled=1u, .label="alarm"};
    iw_alarm_t applied;
    assert(iw_alarms_apply(&service.alarms, &service.alerts, &edit, &clock, &applied) == IW_ALARM_OK);
    test_tick_ms += (uint32_t)(applied.next_due_utc_ms - (uint64_t)clock.utc_ms);
    iw_time_sample(&service_time, test_tick_ms);
    assert(iw_service_advance(&service, test_tick_ms));
    assert(iw_alerts_hold_alarm(&service.alerts, applied.alarm_id) == IW_ALERT_OK);
    iw_product_page_t *list = notification_open();
    assert(!list->model.notifications->count && list->model.selected_alert.entity_id);
    lv_indev_t *pointer = lv_indev_create();
    assert(pointer);
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, notice_pointer_read);
    notice_capture(display, list, 117);
    notice_sample(pointer, 190, 175, true, 20);
    notice_sample(pointer, 190, 175, true, 720);
    assert(!list->model.notification_clear_confirm);
    lv_indev_reset(pointer, NULL);
    notice_sample(pointer, 190, 175, false, 20);
    for (unsigned i = 0; i < IW_NOTIFICATION_CAPACITY; ++i)
        assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "普通通知", strlen("普通通知")));
    (void)iw_product_process();
    assert(list->view.scene.count <= IW_PRODUCT_NODES && list->model.selected_alert.entity_id);
    assert(list->model.notifications->count == 32u);
    notice_capture(display, list, 118);
    iw_product_view_scroll(&list->view, INT16_MAX);
    int hit = iw_product_scene_hit(&list->view.scene, 190, 377, list->view.scroll_y);
    assert(hit >= 0 && list->view.scene.nodes[hit].action == IW_ACTION_NOTIFICATION_OPEN_BASE + 31u);
    iw_product_view_scroll(&list->view, -INT16_MAX);
    iw_alerts_t alerts = service.alerts;
    iw_chronograph_t chronograph = service.chronograph;
    iw_alarms_t alarms = service.alarms;
    notice_sample(pointer, 190, 310, true, 20);
    notice_sample(pointer, 190, 310, true, 720);
    notice_sample(pointer, 190, 310, false, 20);
    (void)iw_product_process();
    assert(list->model.notification_clear_confirm);
    notice_tap(pointer, 195, 338);
    assert(!list->model.notifications->count && list->model.selected_alert.entity_id);
    assert(!memcmp(&alerts, &service.alerts, sizeof(alerts)));
    assert(!memcmp(&chronograph, &service.chronograph, sizeof(chronograph)));
    assert(!memcmp(&alarms, &service.alarms, sizeof(alarms)));
    notice_capture(display, list, 119);
    /* 混合中英文最大长度正文：固定分块不得丢字，清除始终在正文之后。 */
    char text[IW_NOTIFICATION_TEXT_BYTES];
    size_t length = 0;
    const char *unit = "本机W测试";
    while (length + strlen(unit) < sizeof(text)) {
        memcpy(text + length, unit, strlen(unit)); length += strlen(unit);
    }
    text[length] = 0;
    assert(iw_product_notification_add(IW_NOTIFICATION_DIAGNOSTIC, text, length));
    (void)iw_product_process();
    notice_tap(pointer, 190, 310);
    settle_dynamic("visual_long_detail");
    iw_product_page_t *detail = current_product()->product;
    assert(detail->page_id == IW_PAGE_NOTIFICATION_DETAIL);
    char restored[IW_NOTIFICATION_TEXT_BYTES] = {0};
    size_t used = 0;
    int bottom = 0, clear_y = 0;
    for (unsigned i = 0; i < detail->view.scene.count; ++i) {
        const iw_product_node_t *node = &detail->view.scene.nodes[i];
        if (node->multiline && !node->fixed) {
            size_t bytes = strlen(node->text);
            assert(used + bytes < sizeof(restored));
            memcpy(restored + used, node->text, bytes); used += bytes;
            assert(node->y >= bottom); bottom = node->y + node->height;
        }
        if (node->action == IW_ACTION_NOTIFICATION_DELETE) clear_y = node->y;
    }
    assert(!strcmp(restored, text) && clear_y > bottom);
    notice_capture(display, detail, 120);
    iw_product_view_scroll(&detail->view, INT16_MAX);
    notice_capture(display, detail, 121);
    assert(iw_product_scene_hit(&detail->view.scene, 190, clear_y - detail->view.scroll_y + 31,
                                detail->view.scroll_y) >= 0);
    assert(iw_router_back()); settle_dynamic("visual_long_back");
    notification_home();
    lv_indev_delete(pointer);
    puts("ASSERT notification mixed_full32=1 critical_hold_excluded=1 critical_not_acked=1 ordinary_empty_not_all_empty=1 long_body_complete=1");
}

static void notification_paint_failure_case(void)
{
    iw_product_page_t *list = notification_open();
    uint32_t published = list->notification_view_revision;
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "普通通知", strlen("普通通知")));
    const iw_notification_store_t *store = list->model.notifications;
    uint32_t revision = store->last_revision, id = store->last_id;
    /* 空态已有顶部材质，首条到达才分配新行材质；失败不得提交帧版本。 */
    dynamic_cache_fail_next(true);
    (void)iw_product_process();
    dynamic_cache_fail_next(false);
    assert(iw_gui_fault_pending() && list->dirty && list->notification_view_revision == published);
    assert(store->last_revision == revision && store->count == 1u && !iw_notification_find(store, id)->read);
    notification_action(list, IW_ACTION_NOTIFICATION_CLEAR);
    notification_action(list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(store->last_revision == revision);
    assert(!iw_gui_fault_process());
    settle_dynamic("notification_paint_fault_recovery");
    iw_gui_fault_dismiss();
    assert(depth == 1u);
    list = notification_open();
    assert(list->notification_view_revision == revision && list->model.notifications->count == 1u);
    notification_home();
    puts("ASSERT notification new_paint_allocation_failure=1 no_frame_publish=1 no_read_clear=1 recovery_retains_store=1");
}
