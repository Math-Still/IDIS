#!/usr/bin/env python3
"""
md / docx → 商务 HTML 一键转换脚本

用法：
    python3 convert.py 数字员工.md
    python3 convert.py 数字员工.docx
    python3 convert.py 数字员工.md --title "数字员工" --subtitle "副标题" --badge "互联网+"

说明：
    - 输入 .md  → 直接转换（图片用相对路径，media/ 目录需在同级）
    - 输入 .docx → 先用 pandoc 提取 media/，再转换
    - 图片全部保持相对路径，方便手动替换
    - 依赖：pandoc（brew install pandoc）
"""

import re, os, sys, subprocess, argparse, tempfile

# ── 配置 ──────────────────────────────────────────────────────────────────────

HERO_STATS = [
    ("500ms", "危气采集链路"),
    ("99%",   "采集准确率"),
    ("5大",   "安全监测功能"),
    ("7天",   "极简部署"),
]

# ── CSS ───────────────────────────────────────────────────────────────────────

CSS = """
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#eef2f7;--surface:#fff;--border:#dde3ed;--border2:#f1f5f9;
  --text:#1e293b;--muted:#64748b;
  --primary:#1d4ed8;--pl:#eff6ff;--pm:#3b82f6;--accent:#0ea5e9;
  --toc:240px
}
html{scroll-behavior:smooth}
body{
  background:#ffffff;
  background-attachment:fixed;
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei","Segoe UI",sans-serif;
  font-size:15px;line-height:1.8;padding-top:56px
}
::-webkit-scrollbar{width:6px}
::-webkit-scrollbar-track{background:var(--bg)}
::-webkit-scrollbar-thumb{background:#cbd5e1;border-radius:3px}

/* ── Navbar ── */
#navbar{
  position:fixed;top:0;left:0;right:0;z-index:200;height:56px;
  background:rgba(255,255,255,.95);backdrop-filter:blur(12px);
  border-bottom:1px solid var(--border);
  display:flex;align-items:center;padding:0 32px;gap:12px
}
.nl{font-size:15px;font-weight:800;color:var(--primary);white-space:nowrap}
.ns{color:#cbd5e1}
.nm{font-size:13px;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.nr{margin-left:auto}
.nb{
  padding:6px 16px;border-radius:6px;font-size:13px;font-weight:600;
  background:var(--primary);color:#fff;text-decoration:none
}
.nb:hover{opacity:.85}

/* ── Progress bar ── */
#progress{position:fixed;top:56px;left:0;right:0;z-index:199;height:3px;background:#e2e8f0}
#pb{height:100%;width:0%;background:linear-gradient(90deg,var(--primary),var(--accent));transition:width .1s}

/* ── Hero ── */
#hero{
  position:relative;overflow:hidden;
  background:linear-gradient(135deg,#0f172a 0%,#1e3a5f 50%,#0f172a 100%);
  padding:80px 40px 100px;text-align:center
}
.hm{
  position:absolute;inset:0;z-index:0;
  background-image:
    linear-gradient(rgba(59,130,246,.08) 1px,transparent 1px),
    linear-gradient(90deg,rgba(59,130,246,.08) 1px,transparent 1px);
  background-size:48px 48px;animation:mm 25s linear infinite
}
@keyframes mm{0%{transform:translateY(0)}100%{transform:translateY(48px)}}
.hg{
  position:absolute;inset:0;z-index:0;
  background:radial-gradient(ellipse 70% 60% at 50% 40%,rgba(59,130,246,.15) 0%,transparent 70%)
}
.hi{position:relative;z-index:1;max-width:800px;margin:0 auto}
.hbadge{
  display:inline-flex;align-items:center;gap:8px;padding:6px 18px;border-radius:20px;
  border:1px solid rgba(59,130,246,.4);background:rgba(59,130,246,.1);
  color:#93c5fd;font-size:13px;font-weight:600;margin-bottom:28px
}
.hdot{width:6px;height:6px;border-radius:50%;background:#60a5fa;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
.ht{font-size:clamp(28px,4vw,52px);font-weight:900;line-height:1.2;color:#fff;margin-bottom:16px}
.ht span{
  background:linear-gradient(135deg,#60a5fa,#38bdf8);
  -webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text
}
.hsub{font-size:16px;color:#94a3b8;margin-bottom:40px;line-height:1.7}
.hstats{
  display:flex;justify-content:center;gap:40px;flex-wrap:wrap;
  padding:24px 40px;border-radius:16px;
  background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.1)
}
.hsn{font-size:30px;font-weight:800;color:#60a5fa;font-family:"SF Mono",monospace}
.hsl{font-size:12px;color:#94a3b8;margin-top:2px}

/* ── Layout ── */
#layout{display:flex;align-items:flex-start;max-width:1100px;margin:0 auto;padding:28px 20px;gap:24px}

/* ── TOC ── */
#toc{
  width:var(--toc);flex-shrink:0;position:sticky;top:72px;
  max-height:calc(100vh - 90px);overflow-y:auto;
  background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:16px 0;
  box-shadow:0 2px 8px rgba(0,0,0,.06)
}
.tt{
  font-size:11px;font-weight:700;color:var(--muted);letter-spacing:1.5px;text-transform:uppercase;
  padding:0 16px 10px;border-bottom:1px solid var(--border2);margin-bottom:6px
}
#toc ul{list-style:none}
#toc a{
  display:block;padding:5px 16px;font-size:12.5px;color:var(--muted);
  text-decoration:none;border-left:2px solid transparent;transition:all .15s;line-height:1.5
}
#toc a:hover{color:var(--primary);background:var(--pl);border-left-color:var(--primary)}
#toc a.active{color:var(--primary);font-weight:600;border-left-color:var(--primary);background:var(--pl)}

/* ── Content ── */
#content{flex:1;min-width:0;max-width:800px}
.section-block{
  background:var(--surface);border:1px solid var(--border);border-radius:14px;
  padding:40px 48px;margin-bottom:18px;position:relative;
  box-shadow:0 1px 4px rgba(0,0,0,.05),0 4px 16px rgba(0,0,0,.04);transition:box-shadow .2s
}
.section-block:hover{box-shadow:0 4px 24px rgba(0,0,0,.09)}

/* ── Typography ── */
h1{
  font-size:24px;font-weight:800;padding-bottom:14px;margin-bottom:24px;
  display:flex;align-items:center;gap:10px;position:relative
}
h1::before{
  content:'';display:inline-block;width:4px;height:26px;
  background:linear-gradient(180deg,var(--primary),var(--accent));
  border-radius:2px;flex-shrink:0
}
h2{font-size:19px;font-weight:700;margin:28px 0 14px;padding-left:12px;position:relative}
h3{font-size:16px;font-weight:700;color:var(--primary);margin:20px 0 10px}
h4{font-size:15px;font-weight:700;margin:16px 0 8px}
h5,h6{font-size:14px;font-weight:600;color:var(--muted);margin:14px 0 6px}
p{margin-bottom:12px}
strong{font-weight:700}
em{font-style:italic;color:var(--muted)}
a{color:var(--primary);text-decoration:none}
a:hover{text-decoration:underline}
code{background:#f1f5f9;padding:2px 6px;border-radius:4px;font-size:13px;font-family:"SF Mono",monospace}
pre{background:#1e293b;color:#e2e8f0;padding:20px;border-radius:8px;overflow-x:auto;margin:16px 0}
hr{border:none;border-top:1px solid var(--border);margin:24px 0}
blockquote{
  border-left:3px solid var(--pm);padding:10px 18px;
  background:var(--pl);border-radius:0 8px 8px 0;margin:14px 0;color:var(--muted)
}
ul,ol{padding-left:22px;margin-bottom:12px}
li{margin-bottom:5px;line-height:1.7}
ol{list-style:none;counter-reset:ol-counter calc(var(--ol-start,1) - 1);padding-left:32px}
ol>li{counter-increment:ol-counter;position:relative}
ol>li::before{content:"（" counter(ol-counter) "）";position:absolute;left:-32px;color:var(--text);font-size:.95em}

/* ── Tables ── */
.table-wrap{overflow-x:auto;margin:16px 0;border-radius:8px;border:1px solid var(--border)}
table{
  width:100%;border-collapse:collapse;font-size:13.5px;margin:0;
  table-layout:auto;word-break:break-word
}
th{
  background:linear-gradient(135deg,#1e3a5f,#1d4ed8);color:#fff;
  font-weight:600;padding:9px 12px;text-align:left;white-space:nowrap
}
td{padding:8px 12px;border-bottom:1px solid var(--border2);vertical-align:top;word-wrap:break-word}
tr:last-child td{border-bottom:none}
tr:nth-child(even) td{background:#f8faff}
tr:hover td{background:var(--pl)}

/* ── Images ── */
img{
  max-width:100%;height:auto;border-radius:8px;display:block;margin:16px auto;
  border:1px solid var(--border);box-shadow:0 2px 12px rgba(0,0,0,.08)
}
figure{margin:20px 0;text-align:center}
figcaption{
  font-size:13px;color:var(--muted);margin-top:8px;
  text-align:center;font-style:italic;
}

/* ── Table caption ── */
caption{
  font-size:13px;color:var(--muted);
  text-align:center;font-style:italic;
  padding:0 0 8px 0;caption-side:top;
  font-weight:600;
}

/* ── 图/表 标签段落 ── */
p.fig-label{
  font-size:13px;color:var(--muted);
  text-align:center;font-style:italic;
  margin-top:4px;margin-bottom:16px
}

/* ── 背景纹理（点阵） ── */
body::before{
  display:none;
}

/* ── 打印目录块 ── */
.toc-print h1{margin-bottom:16px}
.toc-print .toc-grid{display:flex;flex-direction:column;gap:3px}
.toc-print .toc-item{
  text-decoration:none;color:var(--text);font-size:13.5px;
  padding:5px 12px;border-radius:6px;transition:background .15s;
  border-left:2px solid transparent;line-height:1.5
}
.toc-print .toc-item:hover{background:var(--pl);color:var(--primary);border-left-color:var(--primary)}
.toc-print .toc-l2{font-weight:600;font-size:13px;color:var(--muted)}
.toc-print .toc-l3{font-size:12.5px;color:var(--muted)}
@media print{
  .toc-print{break-after:page !important;page-break-after:always !important}
}
.section-block::before,.section-block::after{
  content:'';position:absolute;width:18px;height:18px;
  border-color:var(--primary);border-style:solid;opacity:.18;pointer-events:none;
  transition:opacity .3s
}
.section-block::before{top:-1px;left:-1px;border-width:2px 0 0 2px;border-radius:4px 0 0 0}
.section-block::after{bottom:-1px;right:-1px;border-width:0 2px 2px 0;border-radius:0 0 4px 0}
.section-block:hover::before,.section-block:hover::after{opacity:.45}

/* ── h1 底部渐变装饰线（替换原纯色 border-bottom） ── */
h1{border-bottom:none;position:relative}
h1::after{
  content:'';position:absolute;bottom:0;left:0;right:0;height:2px;
  background:linear-gradient(90deg,var(--primary) 0%,var(--accent) 55%,transparent 100%);
  border-radius:1px
}

/* ── h2 渐变左边框（替换原纯色 border-left） ── */
h2{border-left:none;position:relative}
h2::before{
  content:'';position:absolute;left:0;top:2px;bottom:2px;width:3px;
  background:linear-gradient(180deg,var(--primary),var(--accent));border-radius:2px
}

/* ── 卡片蓝色光晕 hover ── */
.section-block:hover{
  box-shadow:
    0 4px 24px rgba(0,0,0,.09),
    0 0 0 1px rgba(29,78,216,.12),
    0 0 36px rgba(14,165,233,.12)
}

/* ── 彩色关键词标签 ── */
.hl-blue{display:inline-block;padding:1px 7px;border-radius:4px;font-size:.88em;font-weight:700;
  background:rgba(59,130,246,.12);color:#1d4ed8;border:1px solid rgba(59,130,246,.25)}
.hl-cyan{display:inline-block;padding:1px 7px;border-radius:4px;font-size:.88em;font-weight:700;
  background:rgba(14,165,233,.12);color:#0369a1;border:1px solid rgba(14,165,233,.25)}
.hl-green{display:inline-block;padding:1px 7px;border-radius:4px;font-size:.88em;font-weight:700;
  background:rgba(16,185,129,.12);color:#047857;border:1px solid rgba(16,185,129,.25)}
.hl-gold{display:inline-block;padding:1px 7px;border-radius:4px;font-size:.88em;font-weight:700;
  background:rgba(245,158,11,.12);color:#b45309;border:1px solid rgba(245,158,11,.25)}
.hl-red{display:inline-block;padding:1px 7px;border-radius:4px;font-size:.88em;font-weight:700;
  background:rgba(239,68,68,.12);color:#b91c1c;border:1px solid rgba(239,68,68,.25)}
/* 纯数字 strong 自动药丸样式 */
strong.num{display:inline-block;padding:1px 7px;border-radius:4px;font-size:.9em;
  background:rgba(29,78,216,.10);color:#1d4ed8;border:1px solid rgba(29,78,216,.2);
  font-family:"SF Mono",monospace}

/* ── Animations ── */
.fade-in{opacity:0;transform:translateY(16px);transition:opacity .5s ease,transform .5s ease}
.fade-in.visible{opacity:1;transform:translateY(0)}

/* ── Responsive ── */
@media(max-width:900px){
  #toc{display:none}
  #layout{padding:16px}
  .section-block{padding:24px 20px}
  #navbar{padding:0 16px}
}
@media(max-width:600px){
  .hstats{gap:20px;padding:16px 20px}
  .section-block{padding:20px 16px}
}

/* ── Landing page iframe ── */
.landing-embed{
  width:100%;height:600px;border:none;border-radius:12px;
  margin:24px 0;box-shadow:0 4px 24px rgba(0,0,0,.12);
  border:1px solid var(--border)
}
.landing-embed-wrap{margin:24px 0}
.landing-embed-label{
  font-size:12px;color:var(--muted);margin-bottom:8px;
  display:flex;align-items:center;gap:6px
}
.landing-embed-label::before{
  content:'';display:inline-block;width:3px;height:14px;
  background:var(--primary);border-radius:2px
}

/* ── 封面页（仅打印可见） ── */
#cover-page{display:none}

/* ── Print / PDF 导出 ── */
@media print{
  @page cover{size:A4;margin:0}
  @page{size:A4;margin:20mm 16mm 16mm 16mm}

  html,body{
    -webkit-print-color-adjust:exact;
    print-color-adjust:exact
  }
  html{
    background:#ffffff !important;
    -webkit-print-color-adjust:exact;
    print-color-adjust:exact
  }

  /* 移除 body::before，改用 html 背景铺满整页 */

  #navbar,#progress,#hero{display:none !important}
  body::before{display:none !important}
  body{
    padding-top:0 !important;
    font-size:11pt;
    line-height:1.6;
    background:transparent !important
  }
  #toc{display:none !important}
  #layout{
    padding:0 !important;
    max-width:100% !important;
    display:block !important
  }
  #content{max-width:100% !important}
  #hero{
    padding:24px !important;
    -webkit-print-color-adjust:exact;
    print-color-adjust:exact
  }
  .hstats{display:none !important}
  .section-block{
    box-shadow:none !important;
    border:1px solid #c8d8ee !important;
    border-radius:8px !important;
    break-inside:avoid-page;
    margin-bottom:10px !important;
    padding:20px 24px !important;
    background:rgba(255,255,255,0.82) !important;
    -webkit-print-color-adjust:exact;
    print-color-adjust:exact
  }
  .fade-in{opacity:1 !important;transform:none !important}
  img{
    max-width:100% !important;
    max-height:280px !important;
    break-inside:avoid;
    object-fit:contain
  }
  h1{font-size:16pt !important;break-after:avoid}
  h2{font-size:13pt !important;break-after:avoid}
  h3{font-size:11pt !important;break-after:avoid}
  h4{font-size:10.5pt !important;break-after:avoid}
  table{font-size:9pt !important;break-inside:avoid}
  th,td{padding:5px 8px !important}
  /* 表标题和表格不分页 */
  .table-wrap{break-inside:avoid !important;page-break-inside:avoid !important}
  .table-label-wrap{break-inside:avoid !important;page-break-inside:avoid !important}
  .landing-embed-wrap{display:none !important}
  .section-block::before,.section-block::after{display:none !important}
  h1::after{background:var(--primary) !important}
  h2::before{background:var(--pm) !important}
  blockquote{break-inside:avoid}

  /* 封面页 */
  #cover-page{
    display:block !important;
    position:relative;
    width:210mm;
    height:297mm;
    page-break-after:always;
    break-after:page;
    margin:0;padding:0;overflow:hidden;
    page:cover
  }
  #cover-page img{
    width:100% !important;height:100% !important;
    object-fit:cover !important;display:block !important;
    border:none !important;box-shadow:none !important;
    border-radius:0 !important;margin:0 !important;max-height:none !important
  }
}

/* ── 无导航栏模式（--no-nav） ── */
body.no-nav{padding-top:0}
body.no-nav #navbar,body.no-nav #progress{display:none}
"""

# ── JS ────────────────────────────────────────────────────────────────────────

JS = """
// Progress bar
window.addEventListener('scroll', () => {
  const d = document.documentElement;
  document.getElementById('pb').style.width =
    (d.scrollTop / (d.scrollHeight - d.clientHeight) * 100) + '%';
});

// TOC active highlight
const hs = document.querySelectorAll('h1[id], h2[id], h3[id]');
const tls = document.querySelectorAll('#toc a');
const tocObs = new IntersectionObserver(entries => {
  entries.forEach(e => {
    if (e.isIntersecting) {
      tls.forEach(a => a.classList.remove('active'));
      const a = document.querySelector('#toc a[href="#' + e.target.id + '"]');
      if (a) { a.classList.add('active'); a.scrollIntoView({ block: 'nearest' }); }
    }
  });
}, { rootMargin: '-10% 0px -75% 0px' });
hs.forEach(h => tocObs.observe(h));

// Fade in on scroll
const fadeObs = new IntersectionObserver(entries => {
  entries.forEach(e => { if (e.isIntersecting) e.target.classList.add('visible'); });
}, { threshold: 0.03 });
document.querySelectorAll('.fade-in').forEach(el => fadeObs.observe(el));

// Smooth scroll with fixed nav offset
document.querySelectorAll('a[href^="#"]').forEach(a => {
  a.addEventListener('click', e => {
    const t = document.querySelector(a.getAttribute('href'));
    if (t) {
      e.preventDefault();
      window.scrollTo({ top: t.getBoundingClientRect().top + window.scrollY - 72, behavior: 'smooth' });
    }
  });
});
"""

# ── 主逻辑 ────────────────────────────────────────────────────────────────────

def clean_body(body):
    """清理 pandoc 转换后的 Word 残留"""
    # 移除封面图（image1）
    body = re.sub(r'<p><img[^>]*image1[^>]*/></p>\n?', '', body)
    # 移除 Word 自动目录段落
    body = re.sub(r'<p>目录</p>\n?', '', body)
    body = re.sub(r'<p><a href="#[^"]*">[^<]*<span>\d+</span></a></p>\n?', '', body)
    # 移除空 blockquote（Word 用来缩进）
    body = re.sub(r'<blockquote>\s*</blockquote>\n?', '', body)
    # 移除 Word 锚点 span
    body = re.sub(r'<span id="_Toc[^"]*"></span>', '', body)
    # 移除图片 inline style（交给 CSS 控制）
    body = re.sub(r'(<img[^>]*?) style="[^"]*"', r'\1', body)
    # 加 lazy loading
    body = body.replace('<img ', '<img loading="lazy" ')
    # 把 table 包裹进 .table-wrap（支持横向滚动）
    body = re.sub(r'(<table[\s>])', r'<div class="table-wrap">\1', body)
    body = re.sub(r'(</table>)', r'\1</div>', body)
    # 合并 Pandoc 拆散的相邻 <ol>（每个 li 被包成独立 <ol>）
    body = re.sub(r'</ol>\s*<ol[^>]*>', '\n', body)
    # 把 <ol start="N"> 的 start 属性转为 CSS 变量，让 counter 从正确值开始
    body = re.sub(
        r'<ol start="(\d+)"',
        lambda m: f'<ol style="--ol-start:{m.group(1)}"',
        body
    )

    # 给图/表标签段落加 class（如 <p>图2.1 xxx</p>）
    body = re.sub(
        r'<p>((图|表)\d[\d.]*\s*[^<]{0,80})</p>',
        r'<p class="fig-label">\1</p>',
        body
    )
    # 把表标签段落和紧跟的 table-wrap 包进同一个 keep-together div，防止分页
    body = re.sub(
        r'(<p class="fig-label">表[^<]+</p>\s*)(<div class="table-wrap">)',
        r'<div class="table-label-wrap">\1\2',
        body
    )
    body = re.sub(
        r'(<div class="table-label-wrap">.*?</div>)\s*(</div>)',
        r'\1\2</div>',
        body,
        flags=re.DOTALL
    )
    return body


def build_toc(body):
    """从 h1/h2/h3 构建目录"""
    items = []
    seen = set()
    for m in re.finditer(r'<h([1-3])[^>]*id="([^"]*)"[^>]*>(.*?)</h\1>', body, re.DOTALL):
        level, anchor, text = int(m.group(1)), m.group(2), m.group(3)
        clean = re.sub(r'<[^>]+>', '', text).strip()
        if not clean or anchor in seen or anchor.startswith('_Toc'):
            continue
        seen.add(anchor)
        items.append((level, anchor, clean))

    toc = '<nav id="toc"><div class="tt">目录</div><ul>'
    for level, anchor, text in items:
        indent = (level - 1) * 14
        toc += f'<li style="padding-left:{indent}px"><a href="#{anchor}">{text}</a></li>'
    toc += '</ul></nav>'
    return toc


LANDING_PAGE_PATH = '/Users/still/Desktop/项目相关/agent/Flowise/landing-page.html'

LANDING_IFRAME = """
<div class="landing-embed-wrap">
  <div class="landing-embed-label">产品演示 · 数字员工平台 Landing Page</div>
  <iframe src="{path}" class="landing-embed" title="产品演示"></iframe>
</div>
"""

def wrap_sections(body):
    """按 h1 分割，每段包裹成卡片；在章节3的3.1之后插入 landing page iframe"""
    # 在 body 里直接注入 iframe（在 平台架构 h2 之前）
    iframe_html = LANDING_IFRAME.format(path=LANDING_PAGE_PATH)
    body = re.sub(
        r'(<h2[^>]*id="平台架构")',
        iframe_html + r'\1',
        body,
        count=1
    )
    parts = re.split(r'(?=<h1[ >])', body)
    wrapped = ''
    for part in parts:
        if part.strip():
            wrapped += f'<div class="section-block fade-in">{part}</div>\n'
    return wrapped


def build_hero(title, subtitle, badge):
    stats_html = ''.join(
        f'<div><div class="hsn">{n}</div><div class="hsl">{l}</div></div>'
        for n, l in HERO_STATS
    )
    return f"""
<div id="hero">
  <div class="hm"></div><div class="hg"></div>
  <div class="hi">
    <div class="hbadge"><span class="hdot"></span>{badge}</div>
    <h1 class="ht">{title}<br><span>{subtitle}</span></h1>
    <div class="hstats">{stats_html}</div>
  </div>
</div>"""


def clean_md(md_text, md_dir):
    """
    清理 pandoc 从 Word 转出的 md 的各种残留，
    并把图片路径统一成相对路径（相对于 md 文件所在目录）。
    """
    # 1. 图片：把绝对路径 + {attrs} 统一成 ![alt](media/imageX.ext)
    def normalize_img(m):
        alt  = m.group(1)
        path = m.group(2)
        # 取文件名，拼成相对路径
        fname = os.path.basename(path)
        # 清理 alt 里的 Word 图片名（很长的乱码）
        if len(alt) > 40 or re.search(r'[\d_]{10,}', alt):
            alt = ''
        return f'![{alt}](media/{fname})'

    md_text = re.sub(
        r'!\[([^\]]*)\]\(([^)]+)\)(?:\{[^}]*\})?',
        normalize_img,
        md_text
    )

    # 2. 移除 Word 自动目录块（文件开头到第一个 # 标题之间的内容）
    md_text = re.sub(r'\A.*?(?=^#)', '', md_text, flags=re.DOTALL | re.MULTILINE)

    # 3. 移除 heading 属性 {#anchor .class}
    md_text = re.sub(r'\s*\{[^}]*\}', '', md_text)

    # 4. 移除 Word TOC 链接残留（只匹配单行，避免跨行吞内容）
    #    [[]{#_Toc...}文字](#_Toc...) → 文字
    md_text = re.sub(r'\[\[?\]\{?[^}\n]*\}?([^\]\n]*)\]\([^)\n]*\)', r'\1', md_text)
    #    []{#_Toc...} → 空
    md_text = re.sub(r'\[\]\{[^}\n]*\}', '', md_text)

    # 5. 修复非标准章节标题，几种 Word 残留格式：
    #    []{#_Toc... .anchor}**六、商业模式** → # 六、商业模式
    #    []**六、商业模式**                   → # 六、商业模式
    md_text = re.sub(
        r'^\[(?:[^\]]*)\]\s*\*\*([一二三四五六七八九十]+[、．.]\s*[^\n*]+)\*\*\s*$',
        r'# \1',
        md_text,
        flags=re.MULTILINE
    )

    # 6. 修复图片转义：\![](...) → ![](...)
    md_text = re.sub(r'\\\!', '!', md_text)

    return md_text


def md_to_html_body(md_path):
    """用 pandoc 把（已清理的）md 转成 HTML body"""
    tmp_md   = os.path.join(tempfile.gettempdir(), '_convert_clean.md')
    tmp_html = os.path.join(tempfile.gettempdir(), '_convert_body.html')
    md_dir   = os.path.dirname(md_path)

    with open(md_path, 'r', encoding='utf-8') as f:
        md_text = f.read()

    md_text = clean_md(md_text, md_dir)

    with open(tmp_md, 'w', encoding='utf-8') as f:
        f.write(md_text)

    result = subprocess.run(
        ['pandoc', tmp_md,
         '--to', 'html5',
         '--wrap=none',
         '--standalone=false',
         '-o', tmp_html],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print('pandoc 错误:', result.stderr)
        sys.exit(1)

    with open(tmp_html, 'r', encoding='utf-8') as f:
        return f.read()




def convert(input_path, title, subtitle, badge, no_nav=False):
    input_path = os.path.abspath(input_path)
    out_dir    = os.path.dirname(input_path)
    stem       = os.path.splitext(os.path.basename(input_path))[0]
    ext        = os.path.splitext(input_path)[1].lower()
    out_path   = os.path.join(out_dir, stem + '.html')
    media_dir  = os.path.join(out_dir, 'media')
    tmp_html   = os.path.join(tempfile.gettempdir(), stem + '_body.html')

    # ── Step 1: 转换为 HTML body ──────────────────────────────────────────────
    if ext == '.md':
        print(f'[1/4] 处理 Markdown：{os.path.basename(input_path)} ...')
        body = md_to_html_body(input_path)

    elif ext == '.docx':
        print(f'[1/4] pandoc 转换 docx：{os.path.basename(input_path)} ...')
        result = subprocess.run(
            ['pandoc', input_path,
             '--to', 'html5',
             '--wrap=none',
             f'--extract-media={out_dir}',
             '--standalone=false',
             '-o', tmp_html],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print('pandoc 错误:', result.stderr)
            sys.exit(1)
        with open(tmp_html, 'r', encoding='utf-8') as f:
            body = f.read()

    else:
        print(f'不支持的格式：{ext}，请传入 .md 或 .docx')
        sys.exit(1)

    # ── Step 2: 清理 HTML body ────────────────────────────────────────────────
    print('[2/4] 清理残留内容 ...')
    body = clean_body(body)

    # ── Step 3: 构建目录 & 包裹章节 ──────────────────────────────────────────
    print('[3/4] 构建目录和章节 ...')
    toc_html  = build_toc(body)
    wrapped   = wrap_sections(body)
    hero_html = build_hero(title, subtitle, badge)

    # ── 生成内容区可见目录（用于印刷） ─────────────────────────────────────────
    def build_content_toc(body):
        """从 body 提取 h1/h2/h3 生成可见目录块"""
        items = []
        seen = set()
        for m in re.finditer(r'<h([1-4])[^>]*id="([^"]*)"[^>]*>(.*?)</h\1>', body, re.DOTALL):
            level, anchor, text = int(m.group(1)), m.group(2), m.group(3)
            clean = re.sub(r'<[^>]+>', '', text).strip()
            if not clean or anchor in seen or anchor.startswith('_Toc'):
                continue
            seen.add(anchor)
            items.append((level, anchor, clean))

        html = '<div class="section-block fade-in toc-print"><h1>目录</h1><div class="toc-grid">'
        for level, anchor, text in items:
            if level > 3:
                continue
            padding = level - 1
            html += f'<a href="#{anchor}" class="toc-item toc-l{level}" style="padding-left:{padding * 20 + 8}px">{text}</a>'
        html += '</div></div>'
        return html

    content_toc = build_content_toc(body)
    wrapped = content_toc + '\n' + wrapped

    # ── Step 4: 组装最终 HTML ─────────────────────────────────────────────────
    print('[4/4] 生成 HTML ...')
    navbar_title = re.sub(r'<[^>]+>', '', title)
    body_class = ' class="no-nav"' if no_nav else ''
    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{navbar_title} — {subtitle}</title>
<style>{CSS}</style>
</head>
<body{body_class}>

<div id="cover-page">
  <img src="media/image1.png" alt="封面">
</div>

<div id="navbar">
  <span class="nl">{navbar_title}</span>
  <span class="ns">|</span>
  <span class="nm">{subtitle}</span>
  <div class="nr"><a href="#hero" class="nb">回到顶部</a></div>
</div>
<div id="progress"><div id="pb"></div></div>

{hero_html}

<div id="layout">
  {toc_html}
  <div id="content">{wrapped}</div>
</div>

<script>{JS}</script>
</body>
</html>"""

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(html)

    size = os.path.getsize(out_path) / 1024
    print(f'\n✓ 完成：{out_path}')
    print(f'  文件大小 : {size:.0f} KB')
    print(f'  图片目录 : {media_dir}')
    print(f'  章节数   : {html.count("section-block fade-in")}')
    print(f'  目录项   : {html.count("<li style")}')
    print(f'  表格数   : {html.count("<table")}')


# ── 入口 ──────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='md / docx → 商务 HTML 一键转换')
    parser.add_argument('input', help='输入文件（.md 或 .docx）')
    parser.add_argument('--title',    default='国产化智慧工厂安全监测平台', help='页面主标题')
    parser.add_argument('--subtitle', default='基于国产操作系统的铜冶炼场景安全监测控制平台', help='副标题')
    parser.add_argument('--badge',    default='挑战杯 · 揭榜挂帅 · 路演项目书', help='Hero 徽章文字')
    parser.add_argument('--no-nav',   action='store_true', help='隐藏导航栏（适合导出 PDF）')
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f'文件不存在：{args.input}')
        sys.exit(1)

    convert(args.input, args.title, args.subtitle, args.badge, no_nav=args.no_nav)
