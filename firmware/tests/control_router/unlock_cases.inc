/* 使用真实 input_signal、路由、控制器、D12 服务及 LVGL 屏幕事件；不模拟面板。 */
static unsigned unlock_source_loads;
static void unlock_source_loaded(lv_event_t *event)
{
    (void)event;
    ++unlock_source_loads;
}

static void unlock_steps(unsigned count)
{
    for (unsigned step = 0; step < count; ++step) {
        test_tick_ms += 50u;
        iw_time_sample(&service_time, test_tick_ms);
        (void)iw_router_process();
    }
}

static void unlock_alert_action(uint16_t action_id)
{
    route_page_t *page = current_product();
    page->product->view.action(action_id, 0u, true, page->product->view.context);
    iw_service_work_t work;
    assert(iw_service_take_next(&service, &work) == IW_TAKE_COMPLETED);
    unlock_steps(8u);
}

static void test_unlock_cases(unsigned mode)
{
    const uint16_t locks[] = {IW_PAGE_LOCK, IW_PAGE_WATER_LOCK};
    const iw_alert_source_t sources[] = {IW_ALERT_SOURCE_TIMER, IW_ALERT_SOURCE_ALARM};
    for (unsigned lock = 0; lock < 2u; ++lock) for (unsigned alert = 0; alert < 2u; ++alert) {
        iw_input_context_t expected = lock ? IW_INPUT_WATER : IW_INPUT_LOCKED;
        if (mode == 21u) {
            assert(iw_router_open(IW_PAGE_SETTINGS));
            settle_dynamic("settings_source");
        }
        unsigned source_depth = depth;
        lv_obj_t *source_screen = stack[source_depth - 1u].screen;
        assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
        settle_dynamic("lock_control");
        memcpy(first_control_name, stack[source_depth].name, sizeof(first_control_name));
        assert(iw_router_open(locks[lock]));
        settle_dynamic("lock_page");
        assert(depth == source_depth + 2u && router_test_input_context == expected);
        uint32_t id = 100u + lock * 2u + alert, occurrence = 1u;
        if (!alert) {
            iw_timer_view_t timer;
            assert(iw_timer_create(&service.chronograph, test_tick_ms, 1000u, &timer) == IW_CHRONO_OK);
            id = timer.timer_id;
            occurrence = timer.occurrence;
            test_tick_ms += 1000u;
            iw_time_sample(&service_time, test_tick_ms);
            (void)iw_service_advance(&service, test_tick_ms);
        } else {
            assert(iw_alerts_note(&service.alerts, sources[alert], id, occurrence,
                                  test_tick_ms, 1u) == IW_ALERT_OK);
            assert(iw_alerts_present(&service.alerts, sources[alert], id, occurrence) == IW_ALERT_OK);
        }
        unlock_steps(8u);
        assert(depth == source_depth + 3u && iw_router_alert_visible() && router_test_input_context == expected);
        assert(!input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
        assert(!input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_SINGLE}));
        route_page_t *retained = current_product();
        lv_obj_t *screen = stack[depth - 1u].screen;
        unlock_source_loads = 0;
        lv_obj_add_event_cb(source_screen, unlock_source_loaded, LV_EVENT_SCREEN_LOADED, NULL);
        unsigned attempts = remove_request_count;
        failure_mode = mode == 11u ? 1u : 2u;
        remove_armed = mode == 11u || mode == 12u;
        persistent_failure = mode == 12u;
        if (mode == 14u) test_font_owner(true, false);
        if (mode == 15u) delay_removal = true;
        if (mode == 18u) {
            assert(request((route_request_t){.kind=REQUEST_STAT}, false));
            assert(!input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_UNLOCK}));
            unlock_steps(1u);
            assert(depth == 4u && router_test_input_context == expected);
        }
        if (mode == 19u) held_transition = true;
        if (mode == 20u) stop_render_busy = true;
        assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_UNLOCK}));
        if (mode == 19u) {
            unlock_steps(6u);
            assert(depth == 4u && router_test_input_context == expected);
            held_transition = false;
        }
        if (mode == 20u) {
            unlock_steps(6u);
            assert(depth == 3u && router_test_input_context == expected && overlay_cleanup_page);
            stop_render_busy = false;
            test_font_owner(true, true);
        }
        if (mode == 14u) {
            unlock_steps(6u);
            assert(depth == 4u && remove_request_count == attempts && router_test_input_context == expected);
            test_font_owner(true, true);
        }
        if (mode == 15u) {
            unlock_steps(6u);
            assert(held_transition && depth == 4u && router_test_input_context == expected);
            held_transition = delay_removal = false;
        }
        if (mode == 13u) {
            unlock_steps(2u);
            assert(depth == 3u && !overlay_root_id && router_test_input_context == expected);
            memcpy(first_control_name, stack[1].name, sizeof(first_control_name));
            remove_armed = persistent_failure = true;
        }
        if (mode == 12u || mode == 13u) {
            unlock_steps(12u);
            assert(overlay_cleanup_blocked && router_test_input_context == expected);
            assert(lv_screen_active() == screen && unlock_source_loads == 0u);
            unsigned exhausted = remove_request_count;
            unlock_steps(12u);
            assert(remove_request_count == exhausted);
            remove_armed = persistent_failure = false;
            assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_UNLOCK}));
        }
        if (mode == 16u || mode == 17u) {
            unlock_steps(1u);
            if (mode == 16u) {
                gui_app_route_snapshot_t state;
                (void)gui_app_get_route_snapshot(&state);
                state.user_data = NULL;
                memcpy(state.page_id, "root", 5u);
                route_page_t *source = snapshot_page(&state, false);
                assert(source);
                source->failed = true;
            } else {
                assert(iw_alerts_remove_source(&service.alerts, sources[alert], id) == IW_ALERT_OK);
                if (!alert) assert(iw_timer_alert_ack(&service.chronograph, test_tick_ms, id, occurrence) == IW_CHRONO_OK);
            }
            unlock_steps(24u);
            assert(depth == (mode == 16u ? 2u : 1u) && router_test_input_context == IW_INPUT_NORMAL && !overlay_root_id);
            lv_obj_remove_event_cb(stack[0].screen, unlock_source_loaded);
            if (mode == 16u) {
                assert(iw_router_alert_visible());
                unlock_alert_action(IW_ACTION_ALERT_ACK);
                assert(depth == 1u);
            }
            printf("ASSERT unlock mode=%u lock=%u alert=%u invalidation_recovered=1\n", mode, lock, alert);
            continue;
        }
        unlock_steps(16u);
        assert(unlock_source_loads == 0u);
        assert(lv_screen_active() == screen && current_product() == retained);
        assert(depth == source_depth + 1u && router_test_input_context == IW_INPUT_NORMAL && !overlay_root_id);
        iw_alert_snapshot_t remaining;
        assert(iw_service_alerts_read(&service, &remaining));
        assert(remaining.count == 1u && remaining.records[0].state == IW_ALERT_PRESENTING);
        assert(remaining.records[0].entity_id == id && remaining.records[0].occurrence == occurrence);
        lv_obj_remove_event_cb(source_screen, unlock_source_loaded);
        if (mode == 21u) {
            /* 非根来源也必须成为稍后、再次到期及停止后的返回目标。 */
            unlock_alert_action(IW_ACTION_ALERT_SNOOZE);
            assert(depth == source_depth && lv_screen_active() == source_screen);
            test_tick_ms += 9u * 60u * 1000u;
            iw_time_sample(&service_time, test_tick_ms);
            (void)iw_service_advance(&service, test_tick_ms);
            unlock_steps(8u);
            assert(depth == source_depth + 1u && iw_router_alert_visible());
            unlock_alert_action(IW_ACTION_ALERT_ACK);
            assert(depth == source_depth && lv_screen_active() == source_screen);
            assert(iw_router_home());
            unlock_steps(8u);
            assert(depth == 1u);
            printf("ASSERT unlock mode=%u lock=%u alert=%u no_source_load=1 nonroot_ack_snooze_return=1\n", mode, lock, alert);
            continue;
        }
        /* 显式隐藏、跨扫描不重弹，再经列表产品动作重新打开。 */
        assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_SINGLE}));
        unlock_steps(24u);
        assert(depth == source_depth && !iw_router_alert_visible() && lv_screen_active() == source_screen);
        route_page_t *source = snapshot_page(&(gui_app_route_snapshot_t){.app_id="iwlist",.page_id="root"}, false);
        assert(source && source->product && source->product->model.selected_alert.entity_id == id);
        source->product->view.action(IW_ACTION_ALERT_OPEN, 0u, true, source->product->view.context);
        unlock_steps(8u);
        assert(depth == 2u && iw_router_alert_visible());
        unlock_alert_action(IW_ACTION_ALERT_SNOOZE);
        assert(depth == 1u);
        assert(iw_service_alerts_read(&service, &remaining));
        assert(remaining.count == 1u && remaining.records[0].state == IW_ALERT_SNOOZED);
        uint32_t epoch = remaining.records[0].presentation_epoch;
        test_tick_ms += 9u * 60u * 1000u;
        iw_time_sample(&service_time, test_tick_ms);
        (void)iw_service_advance(&service, test_tick_ms);
        unlock_steps(8u);
        assert(depth == 2u && iw_router_alert_visible());
        assert(iw_service_alerts_read(&service, &remaining));
        assert(remaining.records[0].presentation_epoch > epoch);
        unlock_alert_action(IW_ACTION_ALERT_ACK);
        assert(depth == 1u && !iw_router_alert_visible());
        assert(iw_service_alerts_read(&service, &remaining) && !remaining.count);
        /* 无提醒的原解锁路径保持不变。 */
        assert(input_signal((iw_key_signal_t){IW_KEY_SIDE, IW_KEY_SINGLE}));
        settle_dynamic("empty_control");
        assert(iw_router_open(locks[lock]));
        settle_dynamic("empty_lock");
        assert(input_signal((iw_key_signal_t){IW_KEY_CROWN, IW_KEY_UNLOCK}));
        unlock_steps(8u);
        assert(depth == 1u && router_test_input_context == IW_INPUT_NORMAL);
        printf("ASSERT unlock mode=%u lock=%u alert=%u no_source_load=1 retained=1 explicit_ack=1 snooze_epoch=1\n",
               mode, lock, alert);
    }
}
