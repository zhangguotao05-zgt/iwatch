#ifndef IW_COMPONENTS_DEMO_TEXT_H
#define IW_COMPONENTS_DEMO_TEXT_H
enum { STATE_TEXT_BASE = 8, LONG_TITLE = 20, LONG_DETAIL, LONG_VALUE };
/* 与正式字符清单绑定；修改后必须重新生成并核对字体清单。 */
static const char *const demo_texts[] = {
    "组件展示", "亮度", "亮度校正", "确定", "暂无数据", "请稍等", "成功", "字体资源",
    "常用", "按下", "禁用", "等待", "危险", "加载中", "暂无数据", "错误", "不可用", "内容", "成功", "提醒",
    "思澈科技是一家专注于物联网技术的公司，提供一站式的物联网解决方案。最先提出嵌入式设计A",
    "思澈科技是一家专注于物联网技术的公司，提供一站式的物联网解决方案。最先提出嵌入式MCU+GPU的物联网解决方案，为客户提供更高性能、更低功耗的物联网产品。思澈科技欢迎您。亮度校正OK",
    "思澈科技欢迎您思澈科技欢迎您思80"
};
#endif
