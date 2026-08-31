"""
国产化智慧工厂安全监测平台
挑战杯揭榜挂帅 路演 PPT 生成脚本
输出：心冶智炼-互联网+路演PPT.pptx
"""
from pptx import Presentation
from pptx.util import Inches, Pt, Emu
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN
from pptx.util import Inches, Pt
import os

# ── 全局色板 ──────────────────────────────────────────────
BG        = RGBColor(0x0a, 0x0e, 0x1a)   # 深海蓝黑
CARD      = RGBColor(0x0d, 0x14, 0x21)   # 卡片背景
CYAN      = RGBColor(0x00, 0xd4, 0xff)   # 主色 青蓝
CYAN2     = RGBColor(0x00, 0xb4, 0xd8)   # 次色
GOLD      = RGBColor(0xf5, 0x9e, 0x0b)   # 金色强调
GREEN     = RGBColor(0x10, 0xb9, 0x81)   # 成功绿
TEXT      = RGBColor(0xe2, 0xe8, 0xf0)   # 正文白
MUTED     = RGBColor(0x94, 0xa3, 0xb8)   # 次要文字
WHITE     = RGBColor(0xff, 0xff, 0xff)
DARK_CARD = RGBColor(0x07, 0x0b, 0x14)   # 更深卡片

SLIDE_W = Inches(13.33)
SLIDE_H = Inches(7.5)

FONT_TITLE = "微软雅黑"
FONT_BODY  = "微软雅黑"

MEDIA_DIR = os.path.join(os.path.dirname(__file__), "media")

# ── 辅助函数 ──────────────────────────────────────────────

def new_prs():
    prs = Presentation()
    prs.slide_width  = SLIDE_W
    prs.slide_height = SLIDE_H
    return prs


def blank_slide(prs):
    layout = prs.slide_layouts[6]   # 完全空白
    return prs.slides.add_slide(layout)


def set_bg(slide, color=BG):
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = color


def add_rect(slide, x, y, w, h, fill_color, alpha=None):
    shape = slide.shapes.add_shape(
        1,  # MSO_SHAPE_TYPE.RECTANGLE
        Inches(x), Inches(y), Inches(w), Inches(h)
    )
    shape.line.fill.background()  # no border
    shape.fill.solid()
    shape.fill.fore_color.rgb = fill_color
    return shape


def add_textbox(slide, text, x, y, w, h,
                font_name=FONT_BODY, font_size=18,
                bold=False, color=TEXT,
                align=PP_ALIGN.LEFT, wrap=True,
                italic=False):
    txBox = slide.shapes.add_textbox(
        Inches(x), Inches(y), Inches(w), Inches(h)
    )
    tf = txBox.text_frame
    tf.word_wrap = wrap
    p = tf.paragraphs[0]
    p.alignment = align
    run = p.add_run()
    run.text = text
    run.font.name  = font_name
    run.font.size  = Pt(font_size)
    run.font.bold  = bold
    run.font.color.rgb = color
    run.font.italic = italic
    return txBox


def add_para(tf, text, font_name=FONT_BODY, font_size=16,
             bold=False, color=TEXT, align=PP_ALIGN.LEFT,
             space_before=0, italic=False):
    """向已有 text_frame 追加段落"""
    from pptx.util import Pt as Pt2
    p = tf.add_paragraph()
    p.alignment = align
    p.space_before = Pt2(space_before)
    run = p.add_run()
    run.text = text
    run.font.name  = font_name
    run.font.size  = Pt2(font_size)
    run.font.bold  = bold
    run.font.color.rgb = color
    run.font.italic = italic
    return p


def add_divider(slide, y, color=CYAN, x=0.5, w=12.33, h=0.03):
    r = add_rect(slide, x, y, w, h, color)
    return r


def slide_number(slide, n, total=18):
    add_textbox(slide, f"{n:02d} / {total}",
                12.0, 7.1, 1.2, 0.3,
                font_size=10, color=MUTED, align=PP_ALIGN.RIGHT)


def page_header(slide, tag, title, subtitle=""):
    """通用页眉：左侧标签条 + 标题"""
    add_rect(slide, 0, 0, 0.08, 7.5, CYAN)
    add_textbox(slide, tag,
                0.18, 0.18, 2.0, 0.4,
                font_size=11, color=CYAN, bold=True)
    add_textbox(slide, title,
                0.18, 0.5, 10.0, 0.7,
                font_size=30, bold=True, color=WHITE)
    if subtitle:
        add_textbox(slide, subtitle,
                    0.18, 1.15, 10.0, 0.4,
                    font_size=15, color=MUTED)
    add_divider(slide, 1.5)


# ── 18 页幻灯片 ────────────────────────────────────────────
# ── 18 页幻灯片 ────────────────────────────────────────────

def slide_01_cover(prs):
    slide = blank_slide(prs)
    set_bg(slide)

    add_rect(slide, 0, 0, 0.12, 7.5, CYAN)
    add_rect(slide, 0.12, 0, 0.04, 7.5, CYAN2)
    add_rect(slide, 0, 0, 13.33, 0.06, CYAN)

    add_textbox(slide, "挑战杯 · 揭榜挂帅 · 路演项目书",
                0.4, 0.25, 8.0, 0.5,
                font_size=13, color=CYAN)

    add_textbox(slide, "基于国产操作系统",
                0.4, 0.9, 10.0, 1.0,
                font_size=52, bold=True, color=WHITE)
    add_textbox(slide, "智慧工厂安全监测控制平台",
                0.4, 1.9, 11.0, 1.0,
                font_size=52, bold=True, color=WHITE)

    add_textbox(slide, "—— 铜冶炼场景 ——",
                0.4, 3.0, 9.0, 0.6,
                font_size=32, bold=True, color=CYAN)

    add_textbox(slide,
                "一个能「插上就用、换系统就跑、越用越聪明」的国产化工业安全监测平台",
                0.4, 3.8, 11.0, 0.5,
                font_size=18, color=TEXT)
    add_textbox(slide,
                "从危气预警到应用生态与智能决策，让每一座工厂都安全、自主、高效",
                0.4, 4.3, 11.0, 0.5,
                font_size=18, color=GOLD, bold=True)

    add_divider(slide, 4.9, color=CYAN2, x=0.4, w=9.0)

    add_textbox(slide, "题目编号：XA-202606   发榜单位：诚迈科技股份有限公司",
                0.4, 5.1, 10.0, 0.4,
                font_size=14, color=MUTED)
    add_textbox(slide, "应用层 · 平台层 · 硬件协议对接层",
                0.4, 5.5, 10.0, 0.4,
                font_size=14, color=MUTED)

    # 右侧大数字装饰
    add_textbox(slide, "500ms",
                9.5, 1.0, 3.5, 1.3,
                font_size=66, bold=True, color=CYAN,
                align=PP_ALIGN.CENTER)
    add_textbox(slide, "危气采集链路",
                9.5, 2.3, 3.5, 0.4,
                font_size=14, color=MUTED, align=PP_ALIGN.CENTER)
    add_textbox(slide, "99%",
                9.5, 3.1, 3.5, 1.0,
                font_size=66, bold=True, color=GOLD,
                align=PP_ALIGN.CENTER)
    add_textbox(slide, "采集准确率",
                9.5, 4.2, 3.5, 0.4,
                font_size=14, color=MUTED, align=PP_ALIGN.CENTER)

    add_rect(slide, 0, 7.3, 13.33, 0.2, CARD)
    slide_number(slide, 1)


def slide_02_story(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "故事开篇", "深夜车间，一场危气泄漏的生死竞速")

    story = (
        "铜冶炼阳极炉车间，凌晨2点。\n\n"
        "还原期天然气阀门微渗漏，CO 浓度开始悄悄攀升。\n\n"
        "按照传统模式，这要靠人工巡检发现——\n"
        "巡检周期是分钟级，而 CO 超标到危险浓度只需要几十秒。\n\n"
        "等值班人员闻到气味、拿起报警器赶到现场：\n"
        "泄漏可能已扩散，处置窗口已经错过。\n\n"
        "更棘手的是：现场 DCS、PLC、气体分析仪、工业相机……\n"
        "来自不同厂商、不同协议，数据各自为政，无法联动。\n\n"
        "而这一切，本可以在 1 秒内完成「监测 → 预警 → 联动处置」。"
    )
    txBox = slide.shapes.add_textbox(
        Inches(0.4), Inches(1.7), Inches(7.4), Inches(5.2)
    )
    tf = txBox.text_frame
    tf.word_wrap = True
    p = tf.paragraphs[0]
    p.alignment = PP_ALIGN.LEFT
    run = p.add_run()
    run.text = story
    run.font.name = FONT_BODY
    run.font.size = Pt(16)
    run.font.color.rgb = TEXT

    # 右侧数据卡片
    add_rect(slide, 8.2, 1.7, 4.8, 2.0, CARD)
    add_textbox(slide, "人工巡检响应",
                8.4, 1.85, 4.4, 0.45,
                font_size=16, bold=True, color=CYAN)
    add_textbox(slide, "分钟级",
                8.4, 2.3, 4.4, 0.9,
                font_size=40, bold=True, color=RGBColor(0xef, 0x44, 0x44),
                align=PP_ALIGN.CENTER)
    add_textbox(slide, "错过最佳处置窗口",
                8.4, 3.15, 4.4, 0.35,
                font_size=13, color=MUTED, align=PP_ALIGN.CENTER)

    add_rect(slide, 8.2, 3.9, 4.8, 2.0, CARD)
    add_textbox(slide, "平台联动处置",
                8.4, 4.05, 4.4, 0.45,
                font_size=16, bold=True, color=CYAN)
    add_textbox(slide, "秒级",
                8.4, 4.5, 4.4, 0.9,
                font_size=40, bold=True, color=GREEN,
                align=PP_ALIGN.CENTER)
    add_textbox(slide, "监测→预警→排风/声光/断电 一键闭环",
                8.4, 5.35, 4.4, 0.4,
                font_size=13, color=MUTED, align=PP_ALIGN.CENTER)

    add_rect(slide, 0.4, 6.85, 12.5, 0.45, CYAN)
    add_textbox(slide, "「 危气不会等人，安全必须秒级响应 」",
                0.6, 6.92, 12.0, 0.35,
                font_size=15, bold=True, color=BG,
                align=PP_ALIGN.CENTER)
    slide_number(slide, 2)


def slide_03_pain(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "行业痛点", "四大结构性痛点，阻碍工业安全与信创落地")

    pains = [
        ("01  危气风险高，响应慢", "SO₂/CO/天然气泄漏依赖人工巡检，分钟级响应；联动处置滞后，小异常演变为停炉事故", RED := RGBColor(0xef, 0x44, 0x44)),
        ("02  协议异构，互联互通难", "DCS/PLC/工业相机/气体分析仪「七国八制」，数据孤岛，智能应用「无米下锅」", RED),
        ("03  国产OS适配难，投入大", "国外生态开发的平台迁移到国产OS需重编译、适配驱动、调优性能，周期3~6个月、投入数百万", RED),
        ("04  工艺知识流失，智能化浅", "炉况/终点判断依赖老师傅经验，老龄化断档；多数项目停留在「看板」层面", RED),
    ]
    y = 1.8
    for title, desc, color in pains:
        add_rect(slide, 0.5, y, 12.3, 1.15, CARD)
        add_rect(slide, 0.5, y, 0.08, 1.15, color)
        add_textbox(slide, title, 0.75, y + 0.12, 11.8, 0.45,
                    font_size=18, bold=True, color=WHITE)
        add_textbox(slide, desc, 0.75, y + 0.58, 11.8, 0.45,
                    font_size=14, color=MUTED)
        y += 1.3

    add_textbox(slide, "→ 行业需要：能快速适配国产OS、兼容大多数硬件、承载智能应用的一体化平台",
                0.5, 6.9, 12.3, 0.4,
                font_size=15, bold=True, color=GOLD, align=PP_ALIGN.CENTER)
    slide_number(slide, 3)


def slide_04_solution(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "解决方案", "三层架构：硬件即插即用 · 服务按需挂载 · 应用灵活扩展")

    layers = [
        ("应用层", "安全监测 · 可视化大屏 · 远程控制 · AI智能体 · 视觉识别 · 管理对接", CYAN),
        ("平台层", "消息服务 · 时序数据库 · 规则引擎 · AI推理 · 容器/集群 · 国密安全", CYAN2),
        ("硬件协议对接层", "Modbus/OPC UA/GB28181/HJ212… 驱动插件生态，兼容大多数、开发解决少数", GOLD),
    ]
    y = 2.0
    for name, desc, color in layers:
        add_rect(slide, 1.0, y, 11.3, 1.5, CARD)
        add_rect(slide, 1.0, y, 2.6, 1.5, color)
        add_textbox(slide, name, 1.0, y + 0.5, 2.6, 0.6,
                    font_size=24, bold=True, color=BG,
                    align=PP_ALIGN.CENTER)
        add_textbox(slide, desc, 3.9, y + 0.55, 8.2, 0.6,
                    font_size=17, color=TEXT)
        y += 1.7

    add_textbox(slide, "┌────────────────── 国产操作系统底座：麒麟OS / 统信UOS / OpenHarmony / 诚迈HongZOS ──────────────────┐",
                1.0, 6.9, 11.8, 0.4,
                font_size=14, color=CYAN, align=PP_ALIGN.CENTER)
    slide_number(slide, 4)


def slide_05_employees(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "核心功能", "五大安全监测功能：题目必做 · 全链路可用")

    funcs = [
        ("温湿度智能监控", "精矿库/电解车间环境监测，联动除湿/通风", "±0.3℃ 精度"),
        ("红外感应照明", "人员感应自动照明，无人延时关灯节能", "≤500ms 响应"),
        ("危气监测", "SO₂/CO/天然气三级预警，联动排风/声光/断电", "≤1s 联动"),
        ("AGV 避障", "阳极板/阴极铜搬运，多路超声波分级避障", "≤100ms 避障"),
        ("货物感应计数", "光电计数阳极板产量，按班次统计库存", "≥99.5% 准确"),
    ]
    y = 1.85
    for name, desc, metric in funcs:
        add_rect(slide, 0.5, y, 12.3, 0.95, CARD)
        add_textbox(slide, name, 0.75, y + 0.1, 3.2, 0.5,
                    font_size=18, bold=True, color=CYAN)
        add_textbox(slide, desc, 4.1, y + 0.22, 6.0, 0.5,
                    font_size=14, color=TEXT)
        add_textbox(slide, metric, 10.4, y + 0.22, 2.2, 0.5,
                    font_size=15, bold=True, color=GOLD,
                    align=PP_ALIGN.RIGHT)
        y += 1.1

    add_textbox(slide, "五大功能全部映射至铜冶炼真实场景，可实物沙盘演示",
                0.5, 6.9, 12.3, 0.4,
                font_size=15, bold=True, color=GOLD, align=PP_ALIGN.CENTER)
    slide_number(slide, 5)


def slide_06_workflow(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "AI智能体应用", "多类智能体按需挂载 · 工艺数字员工只是其中一类")

    agents = [
        ("工艺数字员工", "炉况监测 · 终点判断 · 节奏协同", "铜冶炼五工序"),
        ("设备预测性维护", "振动/温度趋势分析 · 故障预警", "变事后维修为预测维护"),
        ("能耗优化智能体", "能耗统计 · 能效异常识别 · 节能建议", "双碳合规"),
        ("环保合规智能体", "烟气/废水汇总 · HJ212上报 · 排放预警", "环保监管"),
        ("安全巡检智能体", "巡检编排 · 异常识别 · 整改闭环", "安全管理"),
    ]
    y = 1.85
    for name, skill, feat in agents:
        add_rect(slide, 0.5, y, 12.3, 0.95, CARD)
        add_textbox(slide, name, 0.75, y + 0.12, 2.8, 0.5,
                    font_size=17, bold=True, color=CYAN)
        add_textbox(slide, skill, 3.8, y + 0.22, 5.6, 0.5,
                    font_size=14, color=TEXT)
        add_textbox(slide, feat, 9.6, y + 0.22, 3.0, 0.5,
                    font_size=14, bold=True, color=GOLD,
                    align=PP_ALIGN.RIGHT)
        y += 1.1

    add_textbox(slide, "ReAct智能体范式 · 共享平台AI服务 · 新场景只需开发新智能体应用，无需改动平台",
                0.5, 6.9, 12.3, 0.4,
                font_size=15, bold=True, color=GOLD, align=PP_ALIGN.CENTER)
    slide_number(slide, 6)


def slide_07_ui(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "平台界面", "数字可视化大屏 + 远程控制台")

    # 大屏布局示意（模拟）
    add_rect(slide, 0.5, 1.8, 7.8, 4.9, DARK_CARD)
    add_textbox(slide, "数字可视化大屏", 0.8, 2.0, 7.0, 0.5,
                font_size=20, bold=True, color=CYAN)
    zones = [
        ("产线总览", "五工序实时态势"),
        ("危气仪表盘", "SO₂ / CO / 天然气"),
        ("报警中心", "实时报警流 + 声光提醒"),
        ("AGV 轨迹", "搬运路径与避障事件"),
        ("产量看板", "阳极板/阴极铜计数"),
        ("AI智能体", "智能应用运行状态"),
    ]
    zx, zy = 0.8, 2.55
    for i, (t, d) in enumerate(zones):
        add_rect(slide, zx, zy, 2.35, 1.25, CARD)
        add_textbox(slide, t, zx + 0.1, zy + 0.1, 2.1, 0.4,
                    font_size=13, bold=True, color=WHITE)
        add_textbox(slide, d, zx + 0.1, zy + 0.55, 2.1, 0.5,
                    font_size=11, color=MUTED)
        zx += 2.5
        if (i + 1) % 3 == 0:
            zx = 0.8
            zy += 1.45

    # 右侧远程控制
    add_rect(slide, 8.6, 1.8, 4.2, 4.9, DARK_CARD)
    add_textbox(slide, "远程控制台", 8.9, 2.0, 3.8, 0.5,
                font_size=20, bold=True, color=CYAN)
    ctrls = ["远程开/关排风", "报警复位", "AGV 启停/转向", "阈值参数配置", "分级审批", "操作审计留痕"]
    cy = 2.6
    for c in ctrls:
        add_rect(slide, 8.9, cy, 3.7, 0.55, CARD)
        add_textbox(slide, c, 9.1, cy + 0.08, 3.4, 0.4,
                    font_size=13, color=TEXT)
        cy += 0.7

    add_textbox(slide, "Web + 移动端 H5 · 统一认证 · 权限分级（管理员/操作员/观察员）",
                0.5, 6.9, 12.3, 0.4,
                font_size=14, color=MUTED, align=PP_ALIGN.CENTER)
    slide_number(slide, 7)


def slide_08_data(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "核心指标", "可实测、可对比、可验证的量化能力")

    stats = [
        ("500ms", "危气采集链路", "三级预警 + 趋势预测"),
        ("≤1s", "报警联动响应", "排风→声光→断电序列"),
        ("99.5%", "计数准确率", "光电状态机去抖"),
        ("15fps+", "边缘视觉推理", "RK3568 NPU 实时识别"),
        ("7天", "极简部署", "旁路接入不停产"),
        ("4+OS", "国产系统适配", "麒麟/统信/HongZOS/OpenHarmony"),
    ]
    x, y = 0.7, 1.9
    for i, (num, label, desc) in enumerate(stats):
        add_rect(slide, x, y, 3.9, 2.2, CARD)
        add_textbox(slide, num, x, y + 0.2, 3.9, 1.0,
                    font_size=44, bold=True, color=CYAN,
                    align=PP_ALIGN.CENTER)
        add_textbox(slide, label, x, y + 1.15, 3.9, 0.4,
                    font_size=16, bold=True, color=WHITE,
                    align=PP_ALIGN.CENTER)
        add_textbox(slide, desc, x, y + 1.55, 3.9, 0.4,
                    font_size=12, color=MUTED, align=PP_ALIGN.CENTER)
        x += 4.1
        if (i + 1) % 3 == 0:
            x = 0.7
            y += 2.4

    add_textbox(slide, "多平台同口径对比测试：国产OS vs 通用OS 基线，用数据证明可用、可信",
                0.5, 6.9, 12.3, 0.4,
                font_size=15, bold=True, color=GOLD, align=PP_ALIGN.CENTER)
    slide_number(slide, 8)


def slide_09_moat(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "技术壁垒", "三大壁垒，构成平台可复制性的护城河")

    moats = [
        ("壁垒一", "驱动插件生态", "内置 Modbus/OPC UA/GB28181/HJ212 等协议驱动，兼容大多数硬件；Driver SDK 快速开发新驱动，新设备接入不改上层逻辑", CYAN),
        ("壁垒二", "国产OS适配方法论", "HAL 抽象层 + 容器化交付 + 构建适配矩阵 + 性能对比验证，7天级部署、真机验证、适配报告三件套", CYAN2),
        ("壁垒三", "应用生态机制", "四类开箱即用应用 + 应用商店 + 低代码画布 + 开放API，新场景只需挂载新应用，无需改动平台", GOLD),
    ]
    y = 1.9
    for tag, title, desc, color in moats:
        add_rect(slide, 0.5, y, 12.3, 1.55, CARD)
        add_rect(slide, 0.5, y, 0.08, 1.55, color)
        add_textbox(slide, tag, 0.8, y + 0.15, 1.6, 0.4,
                    font_size=14, bold=True, color=color)
        add_textbox(slide, title, 2.4, y + 0.1, 6.0, 0.5,
                    font_size=20, bold=True, color=WHITE)
        add_textbox(slide, desc, 2.4, y + 0.62, 10.2, 0.8,
                    font_size=14, color=MUTED)
        y += 1.75

    add_textbox(slide, "→ 壁垒不是代码，而是「能快速适配 + 能兼容硬件 + 能承载智能」的系统能力",
                0.5, 6.9, 12.3, 0.4,
                font_size=15, bold=True, color=GOLD, align=PP_ALIGN.CENTER)
    slide_number(slide, 9)


def slide_10_arch(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "技术架构", "端—边—云协同 · 国产OS贯穿全栈")

    # 插入 SVG 渲染的总体架构图 PNG（1200x860，比例约 1.40）
    import os as _os
    img_path = _os.path.join(_os.path.dirname(__file__), "..", "docs", "media", "architecture.png")
    if _os.path.exists(img_path):
        # 可用区域：宽 12.3in，高约 5.3in（页眉 1.5 之下到 6.9）
        w = 7.4
        h = w * 860 / 1200
        x = (13.33 - w) / 2
        y = 1.75 + (5.2 - h) / 2
        slide.shapes.add_picture(img_path, Inches(x), Inches(y), Inches(w), Inches(h))
    else:
        add_textbox(slide, "架构图缺失", 5.0, 4.0, 3.0, 0.5,
                    font_size=18, color=RGBColor(0xef, 0x44, 0x44), align=PP_ALIGN.CENTER)

    add_textbox(slide, "应用层（六大业务域）→ 平台层（标准化服务）→ 硬件协议对接层（驱动插件）→ 国产操作系统底座",
                0.5, 6.85, 12.3, 0.4,
                font_size=13, color=MUTED, align=PP_ALIGN.CENTER)
    slide_number(slide, 10)


def slide_11_modules(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "驱动插件生态", "工业硬件「即插即用」，协议对接全覆盖")

    cats = [
        ("工业总线/控制", ["Modbus RTU/TCP", "OPC UA", "PROFINET", "EtherNet/IP", "EtherCAT"]),
        ("视频/视觉", ["海康/大华 SDK", "RTSP / ONVIF", "GB28181 国标"]),
        ("气体/环保", ["烟气分析仪", "SO₂/CO 传感器", "天然气检漏", "HJ212 上报"]),
        ("无线物联网", ["LoRa", "NB-IoT", "4G/5G", "ZigBee", "WiFi/蓝牙"]),
    ]
    x, y = 0.5, 1.85
    for cat, items in cats:
        add_rect(slide, x, y, 6.0, 2.3, CARD)
        add_textbox(slide, cat, x + 0.2, y + 0.15, 5.6, 0.45,
                    font_size=17, bold=True, color=CYAN)
        iy = y + 0.7
        for it in items:
            add_textbox(slide, "▸ " + it, x + 0.35, iy, 5.4, 0.4,
                        font_size=14, color=TEXT)
            iy += 0.42
        x += 6.2
        if x > 7.0:
            x = 0.5
            y += 2.5

    add_rect(slide, 0.5, 6.55, 12.3, 0.75, RGBColor(0x06, 0x0a, 0x12))
    add_textbox(slide, "未内置驱动？Driver SDK 插件化开发 → 新设备快速接入，无需修改平台核心，驱动市场持续累积",
                0.8, 6.7, 11.8, 0.45,
                font_size=15, bold=True, color=GOLD, align=PP_ALIGN.CENTER)
    slide_number(slide, 11)


def slide_12_validation(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "国产OS适配", "可快速深度融合嵌入国产操作系统")

    steps = [
        ("硬件抽象层(HAL)", "统一设备访问接口，屏蔽OS驱动差异", "上层业务与OS解耦"),
        ("容器化交付", "Docker 镜像跨OS通用，支持国产CPU架构", "开发阶段模拟加速迭代"),
        ("构建适配矩阵", "源码→交叉编译→打包→部署流水线", "麒麟/统信/HongZOS多平台自动产出"),
        ("真机部署验证", "国产OS真机运行 + 性能同口径对比", "《适配报告》+ 运行截图三件套"),
    ]
    y = 1.85
    for title, desc, out in steps:
        add_rect(slide, 0.5, y, 12.3, 1.15, CARD)
        add_textbox(slide, title, 0.75, y + 0.12, 3.4, 0.5,
                    font_size=18, bold=True, color=CYAN)
        add_textbox(slide, desc, 4.3, y + 0.22, 5.2, 0.5,
                    font_size=14, color=TEXT)
        add_textbox(slide, out, 9.7, y + 0.22, 2.9, 0.5,
                    font_size=14, bold=True, color=GOLD,
                    align=PP_ALIGN.RIGHT)
        y += 1.3

    add_rect(slide, 0.5, 6.75, 12.3, 0.55, RGBColor(0x06, 0x0a, 0x12))
    add_textbox(slide, "麒麟OS(工业版·PREEMPT_RT实时增强) │ 统信UOS │ OpenHarmony │ 诚迈HongZOS(内置Modbus/CAN)",
                0.8, 6.87, 11.8, 0.4,
                font_size=14, bold=True, color=CYAN, align=PP_ALIGN.CENTER)
    slide_number(slide, 12)


def slide_13_market(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "市场分析", "三重市场叠加：信创 + 安全监测 + 冶炼智能化")

    mkt = [
        ("信创改造市场", "关键基础设施国产化是政策刚需，工业OS/软件国产化加速落地", "政策驱动"),
        ("工业安全监测市场", "危气/环境/设备监测合规刚需，双碳与环保监管趋严", "合规驱动"),
        ("冶炼智能化市场", "加工费下行倒逼精细化管控，行业智能化渗透率<30%", "效益驱动"),
    ]
    y = 1.85
    for name, desc, tag in mkt:
        add_rect(slide, 0.5, y, 12.3, 1.3, CARD)
        add_rect(slide, 0.5, y, 0.08, 1.3, GOLD)
        add_textbox(slide, name, 0.8, y + 0.12, 3.2, 0.5,
                    font_size=19, bold=True, color=WHITE)
        add_textbox(slide, desc, 4.2, y + 0.22, 6.2, 0.6,
                    font_size=14, color=TEXT)
        add_textbox(slide, tag, 10.8, y + 0.22, 1.8, 0.5,
                    font_size=15, bold=True, color=CYAN,
                    align=PP_ALIGN.RIGHT)
        y += 1.5

    add_textbox(slide, "目标客群：大型冶炼集团(私有化+分成) │ 中小冶炼企业(SaaS+定制包) │ 其他流程工业(平台复制)",
                0.5, 6.9, 12.3, 0.4,
                font_size=15, bold=True, color=GOLD, align=PP_ALIGN.CENTER)
    slide_number(slide, 13)


def slide_14_compete(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "竞品分析", "差异化定位：四大能力集于一身的稀缺组合")

    rows = [
        ("工业互联网平台厂商", "卡奥斯/浪潮云洲", "数据连接强，工艺深度浅", "我们：国产OS原生+安全闭环垂直"),
        ("工控/自动化厂商", "和利时/宝信", "控制执行强，决策智能弱", "我们：智能决策+多协议即插即用"),
        ("工业AI厂商", "华院计算/数商云", "擅长诊断，缺控制闭环", "我们：安全监测+AI决策+控制闭环"),
        ("国际工业软件", "西门子/罗克韦尔", "全栈强，非国产化", "我们：信创合规刚需替代优势"),
    ]
    y = 1.8
    for name, who, weak, diff in rows:
        add_rect(slide, 0.5, y, 12.3, 1.15, CARD)
        add_textbox(slide, name, 0.7, y + 0.08, 2.6, 0.4,
                    font_size=14, bold=True, color=CYAN)
        add_textbox(slide, who, 0.7, y + 0.5, 2.6, 0.4,
                    font_size=12, color=MUTED)
        add_textbox(slide, "短板：" + weak, 3.5, y + 0.15, 4.0, 0.4,
                    font_size=13, color=RGBColor(0xef, 0x44, 0x44))
        add_textbox(slide, "我们的优势：" + diff, 7.6, y + 0.15, 5.2, 0.7,
                    font_size=13, color=GREEN)
        y += 1.3

    add_textbox(slide, "「国产OS适配+多协议即插即用+安全控制闭环+多智能体决策」四大能力集于一身，市场稀缺",
                0.5, 6.9, 12.3, 0.4,
                font_size=15, bold=True, color=GOLD, align=PP_ALIGN.CENTER)
    slide_number(slide, 14)


def slide_15_biz(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "商业模式", "「平台+插件+应用」：一次研发、多次复用")

    biz = [
        ("私有化部署", "大型企业/信创项目", "100~300万/项 + 增量效益分成20~30%"),
        ("SaaS 订阅", "中小企业", "5~10万/年，7天极简部署"),
        ("行业定制包", "铜冶炼企业", "50~100万/项（炉况/终点/调度等）"),
        ("驱动/应用插件", "存量平台客户", "1~10万/个，生态持续变现"),
    ]
    y = 1.85
    for name, who, price in biz:
        add_rect(slide, 0.5, y, 12.3, 1.15, CARD)
        add_textbox(slide, name, 0.75, y + 0.15, 2.8, 0.5,
                    font_size=18, bold=True, color=CYAN)
        add_textbox(slide, who, 3.7, y + 0.22, 3.4, 0.4,
                    font_size=14, color=TEXT)
        add_textbox(slide, price, 7.3, y + 0.22, 5.4, 0.5,
                    font_size=15, bold=True, color=GOLD,
                    align=PP_ALIGN.RIGHT)
        y += 1.3

    add_rect(slide, 0.5, 6.75, 12.3, 0.55, RGBColor(0x06, 0x0a, 0x12))
    add_textbox(slide, "核心逻辑：让客户为可量化的价值买单——无增量收益不付费，风险共担、利益共享",
                0.8, 6.87, 11.8, 0.4,
                font_size=15, bold=True, color=CYAN, align=PP_ALIGN.CENTER)
    slide_number(slide, 15)


def slide_16_finance(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "财务预测", "五年营收预测（保守测算 · 产业化展望）")

    # 收入柱状示意（文字表）
    years = ["2026", "2027", "2028", "2029", "2030"]
    revs  = [140, 520, 1200, 2500, 4600]
    profits = [-130, 10, 280, 870, 1890]

    add_rect(slide, 0.5, 1.85, 7.6, 4.6, DARK_CARD)
    add_textbox(slide, "营业总收入（万元）", 0.8, 2.0, 7.0, 0.45,
                font_size=16, bold=True, color=CYAN)
    # 简单柱状图
    max_r = max(revs)
    bx = 1.2
    for i, (yr, rv) in enumerate(zip(years, revs)):
        h = rv / max_r * 3.4
        add_rect(slide, bx, 5.7 - h, 0.9, h, CYAN if i < 4 else GOLD)
        add_textbox(slide, yr, bx - 0.1, 5.8, 1.1, 0.4,
                    font_size=12, color=MUTED, align=PP_ALIGN.CENTER)
        add_textbox(slide, str(rv), bx - 0.1, 5.7 - h - 0.35, 1.1, 0.35,
                    font_size=11, color=WHITE, align=PP_ALIGN.CENTER)
        bx += 1.55

    add_textbox(slide, "净利润（万元）", 0.8, 6.05, 7.0, 0.35,
                font_size=13, color=MUTED)
    add_textbox(slide, "2026投入期 -130 → 2027盈亏平衡 → 2030规模化盈利 1,890", 0.8, 6.35, 7.2, 0.35,
                font_size=13, color=GREEN, bold=True)

    # 右侧要点
    add_rect(slide, 8.4, 1.85, 4.4, 4.6, CARD)
    add_textbox(slide, "关键节点", 8.7, 2.05, 3.8, 0.45,
                font_size=16, bold=True, color=CYAN)
    pts = [
        "2027 年实现盈亏平衡",
        "2028 年起毛利率超 50%",
        "2030 年营收 4,600 万、净利 1,890 万",
        "平台经济：驱动生态复用",
        "重度压力下 4 年内回收投资",
    ]
    py = 2.6
    for p in pts:
        add_textbox(slide, "▸ " + p, 8.7, py, 3.9, 0.5,
                    font_size=14, color=TEXT)
        py += 0.75

    add_textbox(slide, "注：测算示意，参赛阶段以团队实测数据为准",
                0.5, 6.9, 12.3, 0.35,
                font_size=12, color=MUTED, align=PP_ALIGN.CENTER)
    slide_number(slide, 16)


def slide_17_team(prs):
    slide = blank_slide(prs)
    set_bg(slide)
    page_header(slide, "项目团队", "跨学科复合型攻关团队")

    roles = [
        ("队长/项目经理", "进度 · 资源 · 对外联络 · 方案统筹"),
        ("硬件工程师 ×2", "传感器/AGV/工业相机 · 驱动联调"),
        ("系统/嵌入式 ×2", "国产OS适配 · 采集服务 · 驱动插件框架"),
        ("后端/平台工程师", "MQTT · TDengine · 规则引擎 · 开放API"),
        ("AI 工程师", "多智能体编排 · 视觉模型训练与边缘部署"),
        ("前端/可视化", "大屏 · 远程控制台 · 低代码画布"),
        ("测试/文档", "测试执行 · 全部文档与视频材料"),
    ]
    y = 1.85
    for role, duty in roles:
        add_rect(slide, 0.5, y, 12.3, 0.68, CARD)
        add_textbox(slide, role, 0.75, y + 0.14, 4.2, 0.4,
                    font_size=15, bold=True, color=CYAN)
        add_textbox(slide, duty, 5.1, y + 0.14, 7.5, 0.4,
                    font_size=14, color=TEXT)
        y += 0.8

    add_textbox(slide, "团队 ≤10 人 · 指导教师 ≤3 人 · 跨专业/跨校组队（信息提交前更新）",
                0.5, 6.9, 12.3, 0.4,
                font_size=14, color=MUTED, align=PP_ALIGN.CENTER)
    slide_number(slide, 17)


def slide_18_end(prs):
    slide = blank_slide(prs)
    set_bg(slide)

    add_rect(slide, 0, 0, 0.12, 7.5, CYAN)
    add_rect(slide, 0.12, 0, 0.04, 7.5, CYAN2)
    add_rect(slide, 0, 0, 13.33, 0.06, CYAN)

    add_textbox(slide, "基于国产操作系统",
                1.0, 1.6, 11.0, 1.0,
                font_size=52, bold=True, color=WHITE,
                align=PP_ALIGN.CENTER)
    add_textbox(slide, "智慧工厂安全监测控制平台",
                1.0, 2.5, 11.0, 1.0,
                font_size=52, bold=True, color=WHITE,
                align=PP_ALIGN.CENTER)

    add_divider(slide, 3.7, color=CYAN2, x=4.0, w=5.3)

    add_textbox(slide, "插上就用 · 换系统就跑 · 越用越聪明",
                1.0, 4.0, 11.0, 0.6,
                font_size=24, bold=True, color=CYAN,
                align=PP_ALIGN.CENTER)
    add_textbox(slide, "让每一座工厂，都安全、自主、高效",
                1.0, 4.7, 11.0, 0.6,
                font_size=22, color=GOLD, bold=True,
                align=PP_ALIGN.CENTER)

    add_textbox(slide, "题目编号 XA-202606 · 发榜单位 诚迈科技股份有限公司",
                1.0, 5.9, 11.0, 0.5,
                font_size=16, color=MUTED, align=PP_ALIGN.CENTER)
    add_textbox(slide, "谢谢聆听 · 敬请指正",
                1.0, 6.4, 11.0, 0.6,
                font_size=20, bold=True, color=WHITE,
                align=PP_ALIGN.CENTER)

    add_rect(slide, 0, 7.3, 13.33, 0.2, CARD)
    slide_number(slide, 18)
# ── 主程序 ────────────────────────────────────────────────

def main():
    prs = new_prs()

    funcs = [
        slide_01_cover,
        slide_02_story,
        slide_03_pain,
        slide_04_solution,
        slide_05_employees,
        slide_06_workflow,
        slide_07_ui,
        slide_08_data,
        slide_09_moat,
        slide_10_arch,
        slide_11_modules,
        slide_12_validation,
        slide_13_market,
        slide_14_compete,
        slide_15_biz,
        slide_16_finance,
        slide_17_team,
        slide_18_end,
    ]

    for i, fn in enumerate(funcs):
        print(f"  生成第 {i+1:02d}/18 页: {fn.__name__} ...", end=" ")
        fn(prs)
        print("✓")

    out = os.path.join(os.path.dirname(__file__), "基于国产操作系统智慧工厂安全监测控制平台-路演PPT.pptx")
    prs.save(out)
    print(f"\n✅ 生成完成：{out}")


if __name__ == "__main__":
    main()
