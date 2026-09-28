/* 通知包的路由增量；只用现有场景命中，清除仍明确停在控制器动作边界。 */
static void notification_action(iw_product_page_t *page, uint16_t action_id)
{
    page->view.action(action_id, 0u, true, page->view.context);
}

static void notification_hit(iw_product_page_t *page, uint16_t action_id)
{
    iw_product_view_t *view = &page->view;
    for (unsigned i = 0; i < view->scene.count; ++i) {
        const iw_product_node_t *node = &view->scene.nodes[i];
        if (node->action != action_id) continue;
        if (!node->fixed) iw_product_view_scroll(view, node->y - 160 - view->scroll_y);
        int x = node->x + node->width / 2;
        int y = node->y + node->height / 2 - (node->fixed ? 0 : view->scroll_y);
        int hit = iw_product_scene_hit(&view->scene, x, y, view->scroll_y);
        assert(hit >= 0 && view->scene.nodes[hit].action == action_id);
        notification_action(page, action_id);
        return;
    }
    assert(!"Expected existing notification/control scene action");
}

static iw_product_page_t *notification_open(void)
{
    assert(iw_router_open(IW_PAGE_NOTIFICATION_LIST));
    settle_dynamic("notification_open");
    assert(current_product()->route.page_id == IW_PAGE_NOTIFICATION_LIST);
    return current_product()->product;
}

static void notification_home(void)
{
    assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
    settle_dynamic("notification_home");
    assert(depth == 1u && router_test_input_context == IW_INPUT_NORMAL);
}

static void notification_clear_begin(iw_product_page_t *page)
{
    (void)iw_product_process();
    notification_action(page, IW_ACTION_NOTIFICATION_CLEAR);
    assert(page->model.notification_clear_confirm);
}

static void notification_lock_cases(void)
{
    const uint16_t locks[] = {IW_PAGE_LOCK, IW_PAGE_WATER_LOCK};
    for (unsigned i = 0; i < 2u; ++i) {
        assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
        settle_dynamic("notification_lock_control");
        notification_hit(current_product()->product, locks[i]);
        settle_dynamic("notification_locked");
        iw_input_context_t context = i ? IW_INPUT_WATER : IW_INPUT_LOCKED;
        assert(router_test_input_context == context && current_product()->route.page_id == locks[i]);
        const iw_notification_store_t *store = current_product()->product->model.notifications;
        assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "locked arrival", 14u));
        uint32_t id = store->last_id, revision = store->last_revision;
        unsigned locked_depth = depth;
        /* 实际 input_signal -> iw_keys_intent 决定锁行为，不直接改锁上下文。 */
        assert(!input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
        assert(!input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_SINGLE}));
        assert(!input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_DOUBLE}));
        unlock_steps(4u);
        assert(depth == locked_depth && router_test_input_context == context);
        assert(store->last_revision == revision && !iw_notification_find(store, id)->read);
        assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_UNLOCK}));
        settle_dynamic("notification_unlocked");
        assert(depth == 1u && router_test_input_context == IW_INPUT_NORMAL);
        iw_product_page_t *list = notification_open();
        notification_hit(list, IW_ACTION_NOTIFICATION_OPEN_BASE);
        settle_dynamic("notification_detail");
        assert(current_product()->route.page_id == IW_PAGE_NOTIFICATION_DETAIL);
        assert(current_product()->route.argument == id && iw_notification_find(store, id)->read);
        assert(iw_router_back());
        settle_dynamic("notification_detail_back");
        assert(current_product()->product == list);
        notification_clear_begin(list);
        revision = store->last_revision;
        assert(iw_router_back());
        settle_dynamic("notification_confirm_back");
        assert(current_product()->product == list && !list->model.notification_clear_confirm);
        assert(store->last_revision == revision);
        notification_clear_begin(list);
        notification_action(list, IW_ACTION_NOTIFICATION_CLEAR_CANCEL);
        assert(store->last_revision == revision);
        notification_clear_begin(list);
        iw_input_context_t unlocked_context = router_test_input_context;
        notification_action(list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
        /* 覆盖态由 input_signal 临时提升；key port 只持有锁上下文。 */
        assert(!store->count && router_test_input_context == unlocked_context);
        notification_home();
        printf("ASSERT notification lock=%u arrival_unread=1 input_blocked=1 detail_back=1 confirm_cancel_back=1\n", i);
    }
}

static void notification_alert_case(bool alarm)
{
    iw_product_page_t *list = notification_open();
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "before alert", 12u));
    notification_clear_begin(list);
    uint32_t token = list->notification_clear_revision;
    (void)iw_service_advance(&service, test_tick_ms);
    uint32_t due_ms = 1000u;
    if (alarm) {
        iw_clock_snapshot_t clock;
        iw_calendar_fields_t local;
        assert(iw_time_read(&service_time, &clock));
        iw_clock_snapshot_t next = clock;
        next.utc_ms += 60000;
        assert(iw_clock_local_fields(&next, &local));
        iw_alarm_edit_t edit = {.hour=local.hour, .minute=local.minute, .enabled=1u, .label="alarm"};
        iw_alarm_t applied;
        assert(iw_alarms_apply(&service.alarms, &service.alerts, &edit, &clock, &applied) == IW_ALARM_OK);
        assert(applied.next_due_utc_ms > (uint64_t)clock.utc_ms);
        due_ms = (uint32_t)(applied.next_due_utc_ms - (uint64_t)clock.utc_ms);
    } else {
        iw_timer_view_t timer;
        assert(iw_timer_create(&service.chronograph, test_tick_ms, due_ms, &timer) == IW_CHRONO_OK);
    }
    test_tick_ms += due_ms;
    iw_time_sample(&service_time, test_tick_ms);
    assert(iw_service_advance(&service, test_tick_ms));
    settle_dynamic("notification_covered_by_alert");
    assert(iw_router_alert_visible() && !list->visible);
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "during alert", 12u));
    uint32_t revision = list->model.notifications->last_revision;
    notification_action(list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(list->model.notifications->last_revision == revision);
    assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_SINGLE}));
    settle_dynamic("notification_alert_dismiss");
    assert(current_product()->product == list && list->model.notification_clear_confirm);
    assert(list->notification_clear_revision == token);
    notification_action(list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(list->model.message == IW_TEXT_NOTIFICATION_CHANGED && list->model.notifications->count == 2u);
    notification_clear_begin(list);
    iw_alerts_t alerts = service.alerts;
    iw_chronograph_t chronograph = service.chronograph;
    iw_alarms_t alarms = service.alarms;
    iw_service_stats_t stats = service.stats;
    iw_input_context_t context = router_test_input_context;
    notification_action(list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(!list->model.notifications->count && context == router_test_input_context);
    assert(!memcmp(&alerts, &service.alerts, sizeof(alerts)));
    assert(!memcmp(&chronograph, &service.chronograph, sizeof(chronograph)));
    assert(!memcmp(&alarms, &service.alarms, sizeof(alarms)));
    assert(!memcmp(&stats, &service.stats, sizeof(stats)));
    /* 仅在隔离断言完成后，用既有提醒动作显式确认，便于收尾下一场景。 */
    notification_action(list, IW_ACTION_ALERT_OPEN);
    settle_dynamic("notification_reopen_retained_alert");
    assert(iw_router_alert_visible());
    unlock_alert_action(IW_ACTION_ALERT_ACK);
    assert(current_product()->product == list);
    notification_home();
    printf("ASSERT notification alert=%s overlay_restore=1 stale_confirm_rejected=1 clear_no_ack_stop_lock_change=1\n", alarm ? "alarm" : "timer");
}

static void notification_create_failure(void)
{
    iw_product_page_t *list = notification_open();
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "create failure", 14u));
    (void)iw_product_process();
    const iw_notification_store_t *store = list->model.notifications;
    uint32_t id = store->last_id, revision = store->last_revision;
    fail_next_page_view = true;
    notification_hit(list, IW_ACTION_NOTIFICATION_OPEN_BASE);
    settle_dynamic("notification_failed_detail_rollback");
    assert(failed_view_injections == 1u && !fail_next_page_view);
    assert(current_product()->product == list && depth == 2u);
    assert(store->last_revision == revision && !iw_notification_find(store, id)->read);
    notification_hit(list, IW_ACTION_NOTIFICATION_OPEN_BASE);
    settle_dynamic("notification_detail_retry");
    assert(current_product()->route.page_id == IW_PAGE_NOTIFICATION_DETAIL);
    assert(iw_notification_find(store, id)->read);
    assert(iw_router_back());
    settle_dynamic("notification_retry_back");
    notification_home();
    puts("ASSERT notification real_view_create_failure=1 rollback_source=1 not_read_or_cleared=1 retry_read=1");
}

static void notification_update_failure(void)
{
    iw_product_page_t *list = notification_open();
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "first", 5u));
    (void)iw_product_process();
    uint32_t published = list->notification_view_revision;
    assert(iw_product_notification_add(IW_NOTIFICATION_LOCAL, "second", 6u));
    uint32_t revision = list->model.notifications->last_revision;
    notification_action(list, IW_ACTION_NOTIFICATION_OPEN_BASE);
    assert(list->model.message == IW_TEXT_NOTIFICATION_CHANGED);
    /* 旧帧点击产生新的警告字体；在真实 update 资源分配点注入一次失败。 */
    size_t allocations = test_font_allocation_sequence();
    test_font_arm_failure(1u);
    (void)iw_product_process();
    test_font_arm_failure(0);
    assert(test_font_allocation_sequence() > allocations && iw_gui_fault_pending());
    assert(list->notification_view_revision == published && list->dirty);
    assert(list->model.notifications->last_revision == revision && list->model.notifications->count == 2u);
    notification_action(list, IW_ACTION_NOTIFICATION_CLEAR);
    notification_action(list, IW_ACTION_NOTIFICATION_CLEAR_CONFIRM);
    assert(list->model.notifications->last_revision == revision);
    assert(!iw_gui_fault_process());
    settle_dynamic("notification_update_fault_recovery");
    iw_gui_fault_dismiss();
    assert(depth == 1u);
    list = notification_open();
    assert(list->model.notifications->last_revision == revision && list->model.notifications->count == 2u);
    assert(list->notification_view_revision == revision && !list->model.notification_clear_confirm);
    notification_home();
    puts("ASSERT notification real_view_update_failure=1 frame_revision_not_published=1 no_clear=1 recovery_retains_store=1");
}

static void test_notification_router_cases(unsigned mode)
{
    if (mode == 23u) {
        notification_lock_cases();
        notification_alert_case(false);
        notification_alert_case(true);
    } else if (mode == 24u) notification_create_failure();
    else {
        assert(mode == 25u);
        notification_update_failure();
    }
}
