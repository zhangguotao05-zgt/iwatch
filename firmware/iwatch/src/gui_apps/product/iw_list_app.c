#include "lvsf.h"
#include "gui_app_fwk.h"
#include "iw_router.h"
#include "iw_routes.h"

static void list_message(gui_app_msg_type_t message,void *parameter)
{
    (void)parameter;
    iw_router_root_event(IW_PAGE_LAUNCHER_LIST,(unsigned)message);
}
static int list_entry(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler("iwlist",list_message);
    return 0;
}
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(mainmenu),NULL,"iwlist",list_entry,1);
