#ifndef IW_FONT_PORT_H
#define IW_FONT_PORT_H

#include "iw_font.h"

/* RTOS 与硬件边界；主机测试替换这些接口，字体管理逻辑保持生产实现。 */
bool iw_font_port_bind_owner(void);
bool iw_font_port_is_owner(void);
bool iw_font_port_render_idle(void);
void iw_font_port_memory(iw_font_memory_t *memory);
void iw_font_port_publish(const iw_font_stats_t *stats);
void iw_font_port_read(iw_font_stats_t *stats);

#endif
