#ifndef IW_V00_TOKENS_H
#define IW_V00_TOKENS_H

/* 已批准审阅台 v2 的项目色值；只用于 V00 样片，不覆盖旧页面主题。 */
enum {
    IW_V00_BLACK = 0x000000,
    IW_V00_WHITE = 0xf5f5f7,
    IW_V00_SECONDARY = 0xa8a8b1,
    IW_V00_SURFACE = 0x242426,
    IW_V00_CARD = 0x2c2c2e,
    IW_V00_OUTLINE = 0x6f6f74,
    /* 视图以 0 表示透明；最低蓝位在 RGB565 中仍映射为黑色。 */
    IW_V00_DRAW_BLACK = 0x000001,
    IW_V00_BLUE = 0x159fff,
    IW_V00_ORANGE = 0xff9f0a,
    IW_V00_GREEN = 0x30d158,
    IW_V00_LIME = 0xb5f500,
    IW_V00_PINK = 0xff0a64,
    IW_V00_CYAN = 0x00d8ea,
    IW_V00_PURPLE = 0xab76ff,
    IW_V00_RED = 0xff453a
};

#endif
