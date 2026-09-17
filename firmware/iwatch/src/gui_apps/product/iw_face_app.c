#include "lvsf.h"
#include "gui_app_fwk.h"
#include "iw_router.h"
#include "iw_routes.h"

static void face_message(gui_app_msg_type_t message,void *parameter)
{
    (void)parameter;
    iw_router_root_event(IW_PAGE_FACE,(unsigned)message);
}
static int face_entry(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler("iwface",face_message);
    return 0;
}
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(clock),NULL,"iwface",face_entry,1);
