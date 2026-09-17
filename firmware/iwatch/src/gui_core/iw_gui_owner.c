#include "iw_gui_owner.h"
#include "iw_font.h"
#include "iw_font_port.h"
#include "iw_recovery.h"

static iw_gui_owner_t *owners;
static unsigned owner_count;
static bool drawing_fault, cleaning;
extern void iw_gui_cancel_input(void);

bool iw_gui_fault_pending(void)
{
    return drawing_fault || iw_font_fault_pending();
}

bool iw_gui_owner_add(iw_gui_owner_t *owner, void (*stop)(void *), void *context)
{
    if (!iw_font_port_is_owner() || !owner || !stop || cleaning || iw_gui_fault_pending()) return false;
    if (owner_count == IW_GUI_OWNER_LIMIT) return false;
    for (iw_gui_owner_t *it = owners; it; it = it->next) if (it == owner) return false;
    owner->stop = stop;
    owner->context = context;
    owner->next = owners;
    owners = owner;
    owner_count++;
    return true;
}

bool iw_gui_owner_remove(iw_gui_owner_t *owner)
{
    if (!iw_font_port_is_owner() || !owner) return false;
    for (iw_gui_owner_t **link = &owners; *link; link = &(*link)->next) {
        if (*link != owner) continue;
        *link = owner->next;
        owner_count--;
        *owner = (iw_gui_owner_t){0};
        break;
    }
    return true;
}

void iw_gui_fault_raise(void) { drawing_fault = true; }

bool iw_gui_fault_process(void)
{
    if (!iw_gui_fault_pending()) return false;
    if (!iw_font_port_is_owner() || cleaning || !iw_font_port_render_idle()) return true;
    cleaning = true;
    iw_gui_cancel_input();
    while (owners) {
        iw_gui_owner_t *owner = owners;
        void (*stop)(void *) = owner->stop;
        void *context = owner->context;
        /* 先摘除，允许回调销毁包含节点的页面，并容忍重复注销。 */
        (void)iw_gui_owner_remove(owner);
        stop(context);
    }
    cleaning = false;
    /* 未登记的违规引用仍会阻止确认，不强行销毁字体。 */
    if (!iw_font_ack_fault()) return true;
    drawing_fault = false;
    iw_font_note_fallback();
    iw_recovery_show("gui");
    return false;
}

void iw_gui_fault_dismiss(void)
{
    if (iw_font_port_is_owner()) iw_recovery_hide("gui");
}
