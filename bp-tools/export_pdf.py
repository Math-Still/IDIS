#!/usr/bin/env python3
"""
HTML → PDF 导出脚本（基于 playwright）

用法：
    .venv/bin/python export_pdf.py 数字员工.html
    .venv/bin/python export_pdf.py 数字员工.html --out 输出.pdf
    .venv/bin/python export_pdf.py 数字员工.html --no-pagenum   # 不显示页码
"""

import os, sys, argparse
from pathlib import Path
from playwright.sync_api import sync_playwright

# ── 页眉模板（空，不显示） ────────────────────────────────────────────────────
HEADER_TEMPLATE = '<div style="font-size:0;"></div>'

# ── 页脚模板：右下角页码 ──────────────────────────────────────────────────────
FOOTER_TEMPLATE = """
<div style="
  width:100%;
  font-family:-apple-system,'PingFang SC','Microsoft YaHei',sans-serif;
  font-size:9pt;
  color:#94a3b8;
  display:flex;
  justify-content:space-between;
  align-items:center;
  padding:0 16mm;
  box-sizing:border-box;
">
  <span style="color:#1d4ed8;font-weight:600;">国产化智慧工厂安全监测平台 · 铜冶炼场景商业计划书</span>
  <span><span class="pageNumber"></span> / <span class="totalPages"></span></span>
</div>
"""

FOOTER_EMPTY = '<div style="font-size:0;"></div>'


def export(html_path: str, out_path: str, show_pagenum: bool = True):
    html_path = os.path.abspath(html_path)
    if not os.path.exists(html_path):
        print(f'文件不存在：{html_path}')
        sys.exit(1)

    file_url = f'file://{html_path}'

    print(f'[1/3] 启动浏览器 ...')
    with sync_playwright() as p:
        browser = p.chromium.launch(args=['--allow-file-access-from-files'])
        page = browser.new_page()

        print(f'[2/3] 加载页面：{html_path}')
        page.goto(file_url, wait_until='networkidle', timeout=60000)

        # 移除 lazy loading，强制加载所有图片
        page.evaluate('''() => {
            document.querySelectorAll('img[loading="lazy"]').forEach(img => {
                img.removeAttribute('loading');
                if (!img.complete || img.naturalWidth === 0) {
                    const src = img.src;
                    img.src = '';
                    img.src = src;
                }
            });
        }''')
        # 等待所有图片加载完成
        page.wait_for_function('''() => {
            const imgs = Array.from(document.querySelectorAll('img'));
            return imgs.every(img => img.complete);
        }''', timeout=30000)
        page.wait_for_timeout(1000)

        print(f'[3/3] 导出 PDF：{out_path}')
        page.pdf(
            path=out_path,
            format='A4',
            print_background=True,          # 打印背景色/渐变
            display_header_footer=show_pagenum,
            header_template=HEADER_TEMPLATE,
            footer_template=FOOTER_TEMPLATE if show_pagenum else FOOTER_EMPTY,
            margin={
                'top':    '15mm',
                'bottom': '18mm',
                'left':   '12mm',
                'right':  '12mm',
            },
            # 封面页单独处理：第一页不显示页脚
            # （playwright 不支持 @page :first，用 prefer_css_page_size 绕过）
            prefer_css_page_size=False,
        )

        browser.close()

    size = os.path.getsize(out_path) / 1024 / 1024
    print(f'\n✓ 完成：{out_path}')
    print(f'  文件大小：{size:.1f} MB')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='HTML → PDF 导出')
    parser.add_argument('html', help='输入 HTML 文件路径')
    parser.add_argument('--out', default='', help='输出 PDF 路径（默认同名 .pdf）')
    parser.add_argument('--no-pagenum', action='store_true', help='不显示页码')
    args = parser.parse_args()

    out = args.out
    if not out:
        out = str(Path(args.html).with_suffix('.pdf'))

    export(args.html, out, show_pagenum=not args.no_pagenum)
