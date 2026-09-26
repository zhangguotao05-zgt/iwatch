# -*- coding: utf-8 -*-
"""把 V00 资源探针结果渲染成可审阅的工程交接页。"""

import html
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs/ui/assets/v00/resource-study.json"
OUTPUT = ROOT / "docs/ui/V00_资源容量与最小接口验证_v1.html"


def cell(value):
    return html.escape(str(value), quote=True)


def main():
    report = json.loads(SOURCE.read_text(encoding="utf-8"))
    rows = []
    for item in report["resources"]:
        source = (item["source_name"] or "项目绘制材料")
        if item["source_url"]:
            source = '<a href="{}">{}</a>'.format(cell(item["source_url"]), cell(source))
        else:
            source = cell(source)
        rows.append("<tr><td>{}</td><td><code>{}</code></td><td>{}</td><td>{}×{}</td>"
                    "<td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{}</td></tr>".format(
                        cell(", ".join(item["pages"])), cell(item["id"]),
                        cell(item["kind"]), item["width"], item["height"],
                        item["raw_bytes"], item["ezip_bin_bytes"],
                        item["estimated_linked_bytes"], source,
                        cell(item["license_status"])))
    foreground = report["cases"]["foreground"]
    all_assets = report["cases"]["all"]
    fonts = report["fonts"]
    body = """<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>V00 资源容量与最小接口验证</title><link rel="stylesheet" href="../assets/engineering.css">
<style>main{{max-width:1260px;margin:auto;padding:28px 20px 70px}}table{{width:100%;border-collapse:collapse;font-size:13px}}th,td{{border:1px solid #394657;padding:7px;text-align:left;vertical-align:top}}.scroll{{overflow:auto}}.note{{padding:14px 18px;background:#142c30;border:1px solid #3c817f;border-radius:10px}}code{{overflow-wrap:anywhere}}</style>
</head><body><header class="site-header"><a class="brand" href="../../index.html">iwatch</a><span class="doc-name">V00 · 资源接口</span></header><main>
<h1>V00 资源容量与最小接口验证</h1>
<p class="note">本批离线资源研究与最小校验接口获架构阶段通过，未批准完整目标接入。批准的视觉标识为 <code>{visual}</code>，字体标识为 <code>{font}</code>。主机样片仍按<a href="V00_字体接入与六页主机复核_v1.html">已复核版本</a>执行；本批没有把 V00 页面和图标链接进目标固件，没有刷板、推送或关闭 Issue。</p>
<h2>容量判断</h2>
<p>现有 GCC、Keil 镜像的图片段均为 <strong>{base:,} B</strong>，图片预算为 <strong>{limit:,} B</strong>。采用锁定 SDK 的默认 eZIP 模式，37 个前景资源按编码数据、28 B 图像描述符和每项最多 3 B 对齐估算，共 <strong>{fg:,} B</strong>；预计剩余 <strong>{fg_rem:,} B</strong>。连同控制中心整屏背景的 38 项共 <strong>{all:,} B</strong>，预计超额 <strong>{short:,} B</strong>。这些是离线保守估算，不是新镜像的 map 实测；完整六页方案在当前图片上限内尚未闭环。</p>
<p>旧镜像为 GCC <strong>{gcc_size:,} B</strong>、Keil <strong>{keil_size:,} B</strong>，均绑定 <code>{head}</code>，身份及现有预算复核通过；两套固件<strong>都没有链接 V00 资源</strong>。字体为旧字库与四个 Noto Sans SC 子集合计 <strong>{font_total:,} / {font_limit:,} B</strong>，静态余量 <strong>{font_rem:,} B</strong>；字体缓存、主堆和 PSRAM 字形池不能由文件大小推算，仍待目标端测量。</p>
<h2>转换、格式与故障边界</h2>
<p>探针逐项核对源 PNG 与 RGB565/A8 数据的长度和 SHA-256；用 eZIP <code>{version}</code> 编码并解码全部 38 项，尺寸不变、Alpha 逐像素一致，RGB 差值不超过本轮设定的 15/7/15 实测接受界限。它不是 RGB565 的通用数学误差上限。原始 RGB565 按小端顺序，RGB 行跨度为 <code>width×2</code>，A8 独立平面跨度为 <code>width</code>。eZIP 是压缩载荷，图像描述符的压缩 stride 为 0；不能把原始数据直接当作 eZIP 使用。</p>
<p>显式 <code>-chip sf58x</code> 的离线试验使项目图符 <code>{rejected_asset}</code> 的 {alpha_lost:,} 个像素失去原有 Alpha，因此本轮不采用这一组合。当前探针使用锁定 SDK 工具的默认 chip，帮助信息标为 sf55x；它没有采用正式图片构建链路的 <code>-dpt 1</code>，所以压缩体积仅绑定本批候选参数，不能写成当前目标链接值。新增 <code>iw_v00_resource_guard</code> 对可信外部元数据的版本、几何、长度、CRC32 与发布标记做只读校验，无动态分配；它不解析 eZIP 内部尺寸，也不能证明任意元数据与载荷正确配对。目标接入时应先确认元数据来自固定生成表，校验失败后由 GUI 线程转入既有应急层；还需独立验证目标解码器的损坏/OOM/忙状态与生命周期。</p>
<p>SDK 的 LVGL 9 C 描述符抽查与文件估算一致：59×59 项目图符为 <code>LV_COLOR_FORMAT_RAW_ALPHA</code>、数据 1,642 B；390×450 控制中心背景为 <code>LV_COLOR_FORMAT_RAW</code>、数据 62,236 B。两者对应 eZIP 文件各增加 4 B 文件头；测试使用临时合法 C 符号名，没有把设计包中带连字符的文件名直接编入目标工程。</p>
<p>正式主机核心回归在隔离的干净基础 SDK 与补丁 SDK 下共执行 46 项，其中 45 项通过、1 项因当前 Python 缺少 FontTools 跳过；架构师随后使用装有 FontTools 的 Python 3.13 独立复测为 46/46 通过。资源守卫的 MSVC 主机测试、GCC 与 Keil ARM 单文件编译均通过。默认被污染 SDK 下的回归有 1 个失败和 1 个错误，原因是基线目录已带补丁，不记作当前资源代码失败；隔离重跑记录保存在本地 <code>work/v00/resource-study/core-tests-clean.log</code>。</p>
<p>控制中心背景的原始 RGB565 为 351,000 B；不能以文件压缩后的大小推断解码峰值内存。下一步保留已批准视觉：先验证现有绘制原语能否逐区域还原该背景，并交叠图复核；若不行，需在不破坏现有功能的前提下证明可回收至少 {short:,} B 的旧图片段，之后再做完整链接。不得暗中提高预算、调整分区、直接采用 zlib 加整屏缓冲，或以相似图标替换未授权素材。</p>
<h2>方案选择</h2>
<div class="scroll"><table><thead><tr><th>候选</th><th>图片段结果</th><th>进入下一批的条件</th></tr></thead><tbody>
<tr><td>前景 eZIP＋按已批准颜色/材质绘制背景（优先验证）</td><td>前景估算余 {fg_rem:,} B；背景不占图片段</td><td>控制中心背景逐区域与批准稿对照，颜色/渐变/暗部不变；目标绘制时间和主堆通过</td></tr>
<tr><td>38 项全部 eZIP，清退可证实不用的旧资源</td><td>先证明至少腾出 {short:,} B，尚未实施</td><td>逐项证明旧资源不再被页面引用，并完成旧页回归与新 map 检查</td></tr>
<tr><td>主机 zlib 全包移植到目标</td><td>仅主机压缩体积较小</td><td>需新解码器及分块绘制、OOM/时延论证；当前不推荐</td></tr>
</tbody></table></div>
<h2>来源与发布</h2>
<p>资源明细与 SHA-256 见<a href="assets/v00/resource-study.json">机器清单</a>，设计来源见<a href="../assets/v00-design-handoff/manifest.json">批准设计包清单</a>。17 个蜂窝图标和 8 个控制图符为 Apple 参考副本，尚无再分发许可；12 个项目绘制图符及 1 个项目背景也保留发布核查状态。全部资源当前 <code>release_allowed=false</code>，技术压缩通过不构成可发布授权。四个字体子集附<a href="assets/v00/font-specimens/subsets/OFL.txt">OFL 文本</a>，仍需在发布包履行许可说明。</p>
<div class="scroll"><table><thead><tr><th>页面</th><th>资源 ID</th><th>类别</th><th>尺寸</th><th>原始 B</th><th>eZIP 文件 B</th><th>估计链接 B</th><th>来源</th><th>许可状态</th></tr></thead><tbody>{rows}</tbody></table></div>
<h2>下一次工程批次</h2>
<p>按一名熟悉现有工程的固件工程师估算；工时以资源许可及已批准背景实现可用为前提，不含不可预见的 SDK 或显示硬件故障。</p>
<ol><li>资源桥与背景对照：2～4 人日。先选定统一的 eZIP 参数，把探针、C 描述符、清单和正式构建输入逐字节绑定，再用新 GCC/Keil map 重算；不得把当前估算当成新镜像上界。元数据须来自可信生成表，或另设容器/内部尺寸校验，错误配对不能进入解码器。</li><li>解决 {short:,} B 图片段缺口：先对照批准稿验证程序绘制的控制中心背景。接通实际目标解码/失败回退，补损坏、截断、格式/尺寸不一致、OOM、忙时资源持有和重复释放测试，保留 1 MiB 图片与 128 KiB 字体上限。上述资源桥通过复核前不铺开 V01/V02。</li><li>V01：蜂窝、应用列表、表盘、控制中心及现有两种锁的路由/输入整合，6～9 人日。先核对四个 READY 入口和完整返回链，再做主机图与目标构建。</li><li>V02：计时器、闹钟编辑、显示设置，5～8 人日。保留既有服务、草稿、能力状态、滚动及取消行为。</li><li>双工具链、资源峰值与实机验收，另计 2～4 人日；只有新镜像身份、板上性能和实体输入通过后才申请功能收口。素材发布来源仍单独确认。</li></ol>
<p>状态：设计资源校验通过；主机转换/回放通过；守卫的主机与两套 ARM 编译通过；现有 GCC/Keil 归档身份通过；V00 目标镜像与开发板未验证。此文档供架构阶段复核，不代替 V00 功能收口。</p>
</main></body></html>
""".format(visual=cell(report["visual_approval_id"]),
           font=cell(report["font_approval_id"]),
           base=report["baseline"]["gcc"]["image_bytes"],
           limit=report["image_budget_limit_bytes"],
           fg=foreground["estimated_linked_bytes"],
           fg_rem=foreground["gcc_headroom_after_bytes"],
           all=all_assets["estimated_linked_bytes"],
           short=-all_assets["gcc_headroom_after_bytes"],
           gcc_size=report["baseline"]["gcc"]["main_bin_bytes"],
           keil_size=report["baseline"]["keil"]["main_bin_bytes"],
           head=cell(report["baseline"]["gcc"]["git_head"][:7]),
           font_total=fonts["combined_bytes"], font_limit=fonts["limit_bytes"],
           font_rem=fonts["headroom_bytes"],
           version=cell(report["ezip_version"]),
           rejected_asset=cell(report["rejected_chip_mode"]["asset_id"]),
           alpha_lost=report["rejected_chip_mode"]["alpha_changed_pixels"],
           rows="".join(rows))
    OUTPUT.write_text(body, encoding="utf-8")
    print("V00 RESOURCE HTML OK: {} entries".format(len(rows)))


if __name__ == "__main__":
    main()
