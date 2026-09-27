/* 路由、控制器和真实命令处理共用同一服务实例，避免双账本假阳性。 */
#include "../d07_tiny_ttf_oom/test_product_controller.c"

iw_service_t *test_router_service(void)
{
    return &model;
}
