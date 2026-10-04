#!/usr/bin/env python3
"""把 Figma JSON 渲染为 SVG 设计图。

渲染源是仓库内的 `docs/ui/.figma-export/Page_1_0-1.json`（REST API 原始数据），
不依赖 Figma 在线图片，也不依赖导出的 `all.svg`（其文字已转轮廓）。

坐标系：Figma 返回的是画布绝对坐标（同一页内不同画板 x/y 可以相差上千像素）。
本模块在渲染时把所有坐标平移到画板局部空间，使输出的 `viewBox` 总是 `0 0 w h`。
这一点很关键：若直接把绝对坐标写进 `viewBox`，会被非等比缩放，
导致**所有文字不渲染**（仅绘制几何图形）。

用法:
    python scripts/render-figma-svg.py            # 渲染全部画板（默认）
    python scripts/render-figma-svg.py --png      # 额外用 Chrome headless 截 PNG
    python scripts/render-figma-svg.py 3:26936   # 只渲染指定节点
"""
from __future__ import annotations

import argparse
import html
import itertools
import json
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
EXPORT_DIR = REPO_ROOT / "docs" / "ui" / ".figma-export"
OUT_DIR = REPO_ROOT / "docs" / "ui" / "svg"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ui_patches  # noqa: E402


# --------------------------------------------------------------------------
# 画板编号表：按操作先后顺序编号
# --------------------------------------------------------------------------
DESKTOP = [
    ("01", "桌面端_解锁与锁定", "3:26936"),
    ("02", "桌面端_主密钥管理与详情", "3:27258"),
    ("03", "桌面端_生成主密钥", "3:27385"),
    ("04", "桌面端_导入主密钥", "3:27603"),
    ("05", "桌面端_主密钥文件导出", "3:27603"),
    ("06", "桌面端_机密信息管理", "3:26990"),
    ("07", "桌面端_新增机密信息", "3:27146"),
    ("08", "桌面端_导入机密信息与密码验证", "3:27493"),
    ("09", "桌面端_安全设置", "3:27713"),
]

MOBILE = [
    # 编号按操作流重排（解锁 → 主密钥 → 机密信息 → 安全设置）
    ("10", "移动端_解锁", "3:27821"),
    ("11", "移动端_主密钥管理", "3:28135"),
    ("12", "移动端_生成主密钥", "3:28205"),
    ("13", "移动端_导入主密钥", "3:28205"),
    ("14", "移动端_机密信息列表", "3:27863"),
    ("15", "移动端_机密信息详情", "3:27971"),
    ("16", "移动端_缺失主密钥详情", "3:28025"),
    ("17", "移动端_新增机密信息", "3:28077"),
    ("18", "移动端_导入机密信息", "3:28252"),
    ("19", "移动端_导出与备份删除确认", "3:28300"),
    ("20", "移动端_安全设置", "3:28359"),
]

SPEC = [("21", "安全状态与错误反馈说明板", "3:28423")]

BOARDS = DESKTOP + MOBILE + SPEC


def load_canvas() -> dict:
    """加载画布 JSON。"""
    for path in sorted(EXPORT_DIR.glob("*.json")):
        if path.name == "manifest.json":
            continue
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("type") == "CANVAS" and data.get("children"):
            return data
    sys.exit("未找到画布 JSON，请先运行 scripts/fetch-figma.py")


# --------------------------------------------------------------------------
# 样式转换
# --------------------------------------------------------------------------
def color_of(paint: dict) -> tuple[str, float] | None:
    color = paint.get("color") or {}
    r = round(float(color.get("r", 0)) * 255)
    g = round(float(color.get("g", 0)) * 255)
    b = round(float(color.get("b", 0)) * 255)
    a = float(color.get("a", 1))
    if a <= 0.001:
        return None
    return f"#{r:02X}{g:02X}{b:02X}", a


def paint_to_svg(paints) -> str | None:
    """Figma paints 数组 -> SVG paint。只处理纯色，渐变取首色近似。"""
    if not paints:
        return None
    for paint in paints:
        if paint.get("visible", True) is False:
            continue
        if paint.get("type") == "SOLID":
            result = color_of(paint)
            if result:
                return result[0]
        elif paint.get("type", "").startswith("GRADIENT"):
            stops = paint.get("gradientStops") or []
            if stops:
                result = color_of(stops[0])
                if result:
                    return result[0]
    return None


def opacity_of(paints) -> float:
    if not paints:
        return 1.0
    for paint in paints:
        if paint.get("visible", True) is False:
            continue
        if paint.get("type") == "SOLID":
            return float((paint.get("color") or {}).get("a", 1))
    return 1.0


def radius_values(node: dict, w: float, h: float) -> list[float]:
    radii = node.get("rectangleCornerRadii")
    if radii:
        if isinstance(radii, list):
            return [float(x) for x in radii]
        return [float(radii)] * 4
    radius = node.get("cornerRadius")
    if isinstance(radius, (int, float)) and radius:
        return [float(radius)] * 4
    return [0.0] * 4


_UID_SEQ = itertools.count(1)


def unique_uid(node: dict) -> str:
    """给节点分配本次渲染内唯一的短 id。

    必须这样做：ui_patches 会 deepcopy 原有节点来插入新字段，
    副本沿用原节点 id，于是 clipPath id 重复——同一 id 出现两次时，
    后一个 <g> 会被前面的定义整体接管（clip 区域是第一个节点的位置），
    表现为"第二个输入框及其内容凭空消失"。这个 bug 极隐蔽：
    XML 合法、文字数门禁也过，只有看图才发现。
    """
    return f"{re.sub(r'[^0-9A-Za-z]', '', node.get('id', '0'))}x{next(_UID_SEQ)}"


def effects_to_filter(node: dict, uid: str) -> tuple[str | None, list[str]]:
    """返回 (filter id, defs 片段)。仅支持投影与模糊。"""
    defs: list[str] = []
    filter_ref: str | None = None
    for index, effect in enumerate(node.get("effects", []) or []):
        if effect.get("visible", True) is False:
            continue
        etype = effect.get("type")
        if etype == "DROP_SHADOW":
            fid = f"sh-{uid}-{index}"
            result = color_of(effect) or ("#000000", 0.25)
            off = effect.get("offset") or {}
            dx = float(off.get("x", 0))
            dy = float(off.get("y", 0))
            blur = float(effect.get("radius", 0))
            defs.append(
                f'<filter id="{fid}" x="-50%" y="-50%" width="200%" height="200%">'
                f'<feDropShadow dx="{dx:g}" dy="{dy:g}" stdDeviation="{blur / 2:g}" '
                f'flood-color="{result[0]}" flood-opacity="{result[1]:g}"/></filter>'
            )
            filter_ref = fid
        elif etype in ("LAYER_BLUR", "BACKGROUND_BLUR"):
            fid = f"bl-{uid}-{index}"
            radius = float(effect.get("radius", 0))
            defs.append(
                f'<filter id="{fid}" x="-50%" y="-50%" width="200%" height="200%">'
                f'<feGaussianBlur stdDeviation="{radius / 2:g}"/></filter>'
            )
            filter_ref = fid
    return filter_ref, defs


# --------------------------------------------------------------------------
# 渲染
# --------------------------------------------------------------------------
def render_text(node: dict, ox: float, oy: float) -> str:
    """文本节点。

    输出绝对坐标的 `x` / `y`（已减去画板原点），
    不用相对 `dy`——相对定位在部分浏览器下会让文字整体失效。
    """
    chars = node.get("characters", "")
    if not chars.strip():
        return ""
    box = node.get("absoluteBoundingBox") or {}
    x = float(box.get("x", 0)) - ox
    y = float(box.get("y", 0)) - oy
    w = float(box.get("width", 0))

    style = node.get("style") or {}
    size = float(style.get("fontSize") or node.get("fontSize") or 14)
    weight = int(style.get("fontWeight") or node.get("fontWeight") or 400)
    family = style.get("fontFamily") or node.get("fontFamily") or "Noto Sans SC"
    line_height = float(style.get("lineHeightPx") or node.get("lineHeightPx") or size * 1.4)
    letter_spacing = float(style.get("letterSpacing") or node.get("letterSpacing") or 0)
    align = (style.get("textAlignHorizontal") or node.get("textAlignHorizontal") or "LEFT").upper()
    fill = paint_to_svg(node.get("fills")) or "#000000"
    opacity = opacity_of(node.get("fills"))

    anchor = {"LEFT": "start", "CENTER": "middle", "RIGHT": "end"}.get(align, "start")
    tx = {"LEFT": x, "CENTER": x + w / 2, "RIGHT": x + w}.get(align, x)

    attrs = (
        f'font-family="{family}, sans-serif" font-size="{size:g}" font-weight="{weight}" '
        f'fill="{fill}" text-anchor="{anchor}"'
    )
    if letter_spacing:
        attrs += f' letter-spacing="{letter_spacing:g}"'
    if opacity < 0.999:
        attrs += f' opacity="{opacity:g}"'

    # 基线：Figma 的文字外盒是行高容器，基线约在 y + line_height * 0.8
    lines = chars.split("\n")
    if len(lines) == 1:
        return f'<text {attrs} x="{tx:g}" y="{y + line_height * 0.8:g}">{html.escape(lines[0])}</text>'

    spans = []
    for index, line in enumerate(lines):
        baseline = y + line_height * 0.8 + index * line_height
        spans.append(f'<tspan x="{tx:g}" y="{baseline:g}">{html.escape(line) or " "}</tspan>')
    return f'<text {attrs}>{"".join(spans)}</text>'


def render_frame(node: dict, defs: list[str], ox: float, oy: float) -> str:
    """渲染一个节点子树，所有坐标与 (ox, oy) 对齐到本地坐标系。"""
    box = node.get("absoluteBoundingBox") or {}
    x = float(box.get("x", 0)) - ox
    y = float(box.get("y", 0)) - oy
    w = float(box.get("width", 0))
    h = float(box.get("height", 0))
    uid = unique_uid(node)

    parts: list[str] = []
    ntype = node.get("type", "")

    fill = paint_to_svg(node.get("fills"))
    stroke = paint_to_svg(node.get("strokes"))
    stroke_weight = float(node.get("strokeWeight") or 0)
    opacity = opacity_of(node.get("fills"))
    radii = radius_values(node, w, h)
    filter_ref, local_defs = effects_to_filter(node, uid)
    defs.extend(local_defs)

    needs_clip = any(r > 0 for r in radii)
    if needs_clip:
        defs.append(
            f'<clipPath id="clip-{uid}"><rect x="{x:g}" y="{y:g}" '
            f'width="{w:g}" height="{h:g}" rx="{(radii[0] or 0):g}"/></clipPath>'
        )

    if ntype in ("FRAME", "COMPONENT", "COMPONENT_SET", "RECTANGLE", "ELLIPSE"):
        if ntype == "ELLIPSE":
            tag = "ellipse"
            geom = f'cx="{x + w / 2:g}" cy="{y + h / 2:g}" rx="{w / 2:g}" ry="{h / 2:g}"'
        else:
            tag = "rect"
            geom = f'x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="{(radii[0] or 0):g}"'
        # 注意：属性必须全部写完再自闭合，否则会产生
        # <rect .../> stroke="..." 这样的畸形 XML，会把整个 SVG 的解析打断。
        attrs = f'<{tag} {geom} fill="{fill or "none"}"'
        if stroke and stroke_weight:
            attrs += f' stroke="{stroke}" stroke-width="{stroke_weight:g}"'
        if opacity < 0.999:
            attrs += f' opacity="{opacity:g}"'
        if filter_ref:
            attrs += f' filter="url(#{filter_ref})"'
        parts.append(attrs + "/>")

    if ntype == "VECTOR":
        for geo_key in ("fillGeometry", "strokeGeometry"):
            geometry = node.get(geo_key) or []
            is_stroke = geo_key == "strokeGeometry"
            paint = stroke if is_stroke else fill
            for shape in geometry:
                d = shape.get("path")
                if not d:
                    continue
                attrs = f'<path d="{d}" transform="translate({x:g},{y:g})"'
                if is_stroke:
                    attrs += (
                        f' fill="none" stroke="{paint or "#1D2C31"}"'
                        f' stroke-width="{stroke_weight or 1:g}"'
                        ' stroke-linecap="round" stroke-linejoin="round"'
                    )
                else:
                    attrs += f' fill="{paint or "none"}"'
                if opacity < 0.999:
                    attrs += f' opacity="{opacity:g}"'
                parts.append(attrs + "/>")

    if ntype == "TEXT":
        parts.append(render_text(node, ox, oy))

    for child in node.get("children", []) or []:
        if child.get("visible", True) is False:
            continue
        rendered = render_frame(child, defs, ox, oy)
        if rendered:
            parts.append(rendered)

    inner = "\n".join(p for p in parts if p)
    if needs_clip:
        return f'<g clip-path="url(#clip-{uid})">{inner}</g>'
    return inner


def count_text(node: dict) -> int:
    """统计子树里的 TEXT 节点数——用作渲染完整性门禁。"""
    if node.get("type") == "TEXT":
        return 1 if node.get("characters", "").strip() else 0
    return sum(count_text(c) for c in node.get("children", []) or [])


def content_extent(node: dict, ox: float, oy: float) -> tuple[float, float]:
    """返回子树相对画板原点的 max(x+w) / max(y+h)。

    ui_patches 会往下插入字段，被下移的节点可能超出画板原有的
    absoluteBoundingBox。此时若仍沿用原尺寸，SVG 的 viewBox 会把溢出部分
    直接裁掉——表现为"底部的导出按钮被切了一半"。所以画板尺寸必须按
    内容实际占用来放大。
    """
    max_x = max_y = 0.0
    stack = [node]
    while stack:
        cur = stack.pop()
        b = cur.get("absoluteBoundingBox") or {}
        if b:
            max_x = max(max_x, float(b.get("x", 0)) - ox + float(b.get("width", 0)))
            max_y = max(max_y, float(b.get("y", 0)) - oy + float(b.get("height", 0)))
        stack.extend(cur.get("children") or [])
    return max_x, max_y


def render_board(node: dict) -> tuple[str, float, float]:
    box = node.get("absoluteBoundingBox") or {}
    ox = float(box.get("x", 0))
    oy = float(box.get("y", 0))
    w = float(box.get("width", 0))
    h = float(box.get("height", 0))

    # 画板按内容放大：仅在内容真的溢出时撑开，下沿留 24px 余量避免贴边
    need_w, need_h = content_extent(node, ox, oy)
    w = max(w, need_w)
    h = max(h, need_h + 24)
    box = dict(box)
    box["width"] = w
    box["height"] = h

    defs: list[str] = []
    body = render_frame(node, defs, ox, oy)
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:g}" height="{h:g}" '
        f'viewBox="0 0 {w:g} {h:g}">',
        f'<title>{html.escape(node.get("name", "frame"))}</title>',
        f'<defs>{"".join(defs)}</defs>',
        f'<rect width="{w:g}" height="{h:g}" fill="#FFFFFF"/>',
        body,
        "</svg>",
    ]
    return "\n".join(parts), w, h


# --------------------------------------------------------------------------
# 自检
# --------------------------------------------------------------------------
def validate(svg: str, expected_text: int, name: str) -> None:
    """硬门禁：XML 必须合法，文字节点数必须与 JSON 一致。"""
    try:
        root = ET.fromstring(svg)
    except ET.ParseError as exc:
        sys.exit(f"[{name}] SVG 非法 XML：{exc}")

    texts = [e for e in root.iter() if e.tag.endswith("}text")]
    if len(texts) != expected_text:
        sys.exit(f"[{name}] 文字数不匹配：渲染出 {len(texts)} 个，JSON 中有 {expected_text} 个")
    if "dy=" in svg:
        sys.exit(f"[{name}] 残留 dy 相对定位，文字可能不渲染")
    bad = re.search(r'<rect[^>]*?/>\s+(?:stroke|opacity|filter)=', svg)
    if bad:
        sys.exit(f"[{name}] 存在畸形 XML：{bad.group(0)[:60]}")


# --------------------------------------------------------------------------
# PNG 导出（可选）
# --------------------------------------------------------------------------
def find_chrome() -> str | None:
    for name in ("chrome", "msedge"):
        found = shutil.which(name)
        if found:
            return found
    for path in (
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    ):
        if Path(path).exists():
            return path
    return None


def export_png(svg_path: Path, scale: int = 2) -> Path | None:
    chrome = find_chrome()
    if not chrome:
        print("  未找到 Chrome/Edge，跳过 PNG 导出")
        return None
    import re as _re

    head = svg_path.read_text(encoding="utf-8")[:400]
    w = int(_re.search(r'width="([\d.]+)"', head).group(1))
    h = int(_re.search(r'height="([\d.]+)"', head).group(1))

    tmp = Path(tempfile.mkdtemp(prefix="sk-ui-"))
    html_path = tmp / "board.html"
    html_path.write_text(
        '<!DOCTYPE html><html><head><meta charset="utf-8"></head>'
        f'<body style="margin:0;background:#fff">{svg_path.read_text(encoding="utf-8")}</body></html>',
        encoding="utf-8",
        newline="\n",
    )
    png_path = tmp / "board.png"
    subprocess.run(
        [
            chrome, "--headless=new", "--disable-gpu", "--no-first-run",
            f'--user-data-dir={tmp / "profile"}',  # 每次必须独占，否则会命中缓存
            f"--screenshot={png_path}",
            # 窗口宽度按设计坐标给。Chrome headless 默认页面宽 800px，
            # 若传图片尺寸而不传页面宽，SVG 左侧会留出空白，截图就像只画了左半屏。
            f"--window-size={w + 40},{h + 40}",
            "--hide-scrollbars", "--force-device-scale-factor=1",
            str(html_path),
        ],
        check=True,
        capture_output=True,
    )
    out = svg_path.with_suffix(".png")
    shutil.copyfile(png_path, out)
    shutil.rmtree(tmp, ignore_errors=True)
    return out


# --------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description="把 Figma JSON 渲染为 SVG 设计图")
    parser.add_argument("nodes", nargs="*", help="只渲染指定节点 ID（默认全部）")
    parser.add_argument("--png", action="store_true", help="额外导出 2 倍 PNG")
    parser.add_argument("--no-patch", action="store_true", help="跳过设计修改，直接渲染 Figma 原始树")
    args = parser.parse_args()

    canvas = load_canvas()
    # 图标素材库先从原始树里收集：后面部分 patch 需要克隆这些矩形。
    ui_patches.load_icon_library(canvas)

    boards = BOARDS
    if args.nodes:
        wanted = set(args.nodes)
        boards = [b for b in boards if b[2] in wanted]
        if not boards:
            sys.exit(f"未找到节点：{wanted}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    # 画板编号重排后会留下旧编号的文件。只在全量渲染时清理，
    # 指定节点重绘单张时不能把其他画板一并删掉。
    if not args.nodes:
        wanted_names = {f"{n}-{f}.svg" for n, f, _ in boards}
        for stale in OUT_DIR.glob("*.svg"):
            if stale.name not in wanted_names:
                stale.unlink()
                print(f"× 删除旧例 {stale.name}")
    failures: list[str] = []

    for number, filename, node_id in boards:
        node = ui_patches.build_board(canvas, number, node_id, apply_patches=not args.no_patch)
        svg, w, h = render_board(node)
        try:
            validate(svg, count_text(node), filename)
        except SystemExit as exc:
            failures.append(str(exc))
            continue
        out = OUT_DIR / f"{number}-{filename}.svg"
        out.write_text(svg, encoding="utf-8", newline="\n")
        note = " [PNG]" if args.png else ""
        print(f"✓ {out.relative_to(REPO_ROOT)}  {w:g}x{h:g}  text={count_text(node)}{note}")
        if args.png:
            export_png(out)

    if failures:
        print("\n以下画板超标：", file=sys.stderr)
        for item in failures:
            print("  " + item, file=sys.stderr)
        return 1

    print(f"\n共 {len(boards)} 张，全部通过 XML 与文字数门禁")
    return 0


if __name__ == "__main__":
    sys.exit(main())
