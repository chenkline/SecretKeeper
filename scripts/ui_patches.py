"""UI \u8bbe\u8ba1\u56fe\u7684\u7ed3\u6784\u6027\u4fee\u6539\u3002

\u8bbe\u8ba1\u7a3f\u7684\u6e90\u6570\u636e\u662f Figma REST \u5bfc\u51fa\u7684\u539f\u59cb\u6811\uff0c\u6211\u4eec\u4e0d\u80fd\u56de Figma \u6539\u56fe\uff0c
\u56e0\u6b64\u5728\u8fd9\u91cc\u4ee5\u7a0b\u5e8f\u65b9\u5f0f\u5bf9\u6811\u505a\u4fee\u6539\uff1a\u6539\u6587\u6848\u3001\u5220\u8282\u70b9\u3001\u63d2\u5165\u8282\u70b9\u3001
\u91cd\u6392\u5e03\u5c40\u3002\u4fee\u6539\u540e\u7684\u6811\u76f4\u63a5\u4ea4\u7ed9 `render-figma-svg.py` \u6e32\u67d3\u3002

\u4fee\u6539\u5185\u5bb9\u5bf9\u5e94\u7528\u6237\u5728 2026-10 \u63d0\u51fa\u7684\u610f\u89c1\uff0c\u9010\u6761\u5f15\u7528\u5728\u5404 patch \u51fd\u6570\u7684\u6ce8\u91ca\u91cc\u3002

\u4fee\u6539\u5fc5\u987b\u51f3\u5b88\u4e24\u6761\uff1a
1. \u5750\u6807\u4e00\u5f8b\u4fdd\u6301 Figma \u7edd\u5bf9\u5750\u6807\uff08\u540c\u4e00\u9875\u5185\u4e0d\u540c\u753b\u677f x/y \u53ef\u5dee\u4e0a\u5343\u50cf\u7d20\uff09\uff0c
   \u5e73\u79fb\u753b\u677f\u65f6\u7531\u6e32\u67d3\u5668\u7edf\u4e00\u5b8c\u6210\u3002
2. \u4e0d\u5f97\u4f7f\u7528\u4f1a\u88ab Figma \u5ffd\u7565\u7684\u5b57\u6bb5\u540d\uff08\u5982 `variantId`\uff09\uff0c
   \u65b0\u5efa\u8282\u70b9\u53ea\u7528\u6e32\u67d3\u5668\u8bfb\u53d6\u7684\u5b57\u6bb5\u3002
"""
from __future__ import annotations

import copy
from typing import Any, Callable

# \u4e3b\u9898\u8272\uff08\u4ece\u8bbe\u8ba1\u89c4\u8303\u91cc\u63d0\u53d6\uff0c\u65b0\u5efa\u8282\u70b9\u4e0e\u539f\u6709\u8282\u70b9\u4fdd\u6301\u4e00\u81f4\uff09
INK = {"r": 0.114, "g": 0.173, "b": 0.192, "a": 1.0}          # #1D2C31 \u4e3b\u6587\u5b57
ACCENT = {"r": 0.078, "g": 0.471, "b": 0.431, "a": 1.0}        # #14786D \u5f3a\u8c03
MUTED = {"r": 0.443, "g": 0.502, "b": 0.529, "a": 1.0}          # #718087 \u6b21\u8981\u6587\u5b57
BORDER = {"r": 0.875, "g": 0.898, "b": 0.906, "a": 1.0}        # #DFE5E7 \u63cf\u8fb9
SURFACE = {"r": 0.953, "g": 0.961, "b": 0.965, "a": 1.0}       # #F3F5F6
WHITE = {"r": 1.0, "g": 1.0, "b": 1.0, "a": 1.0}


def _solid(color: dict) -> list[dict]:
    return [{"blendMode": "NORMAL", "type": "SOLID", "color": color}]


# ---------------------------------------------------------------------------
# \u5de5\u5177
# ---------------------------------------------------------------------------
def find(node: dict, name: str) -> dict | None:
    """\u6309\u540d\u79f0\u6df1\u5ea6\u9996\u5148\u67e5\u627e\u8282\u70b9\u3002"""
    # None \u5b50\u8282\u70b9\u662f\u6784\u9020\u753b\u677f\u65f6\u7559\u4e0b\u7684\u7a7a\u69fd\uff0c\u904d\u5386\u65f6\u8df3\u8fc7\u3002
    if node is None:
        return None
    if node.get("name") == name:
        return node
    for child in node.get("children", []) or []:
        found = find(child, name)
        if found is not None:
            return found
    return None


def walk_all(node: dict):
    """\u9012\u5f52\u904d\u5386\u6240\u6709\u8282\u70b9\u3002"""
    if node is None:
        return
    yield node
    for child in node.get("children", []) or []:
        yield from walk_all(child)


def find_parent(node: dict, target: dict) -> dict | None:
    """\u627e\u5230 target \u7684\u7236\u8282\u70b9\u3002"""
    if node is None:
        return None
    for child in node.get("children", []) or []:
        if child.get("id") == target.get("id"):
            return node
        found = find_parent(child, target)
        if found is not None:
            return found
    return None


def box(node: dict) -> dict:
    return node.setdefault("absoluteBoundingBox", {}) if node else {}


def shift(node: dict, dx: float, dy: float) -> None:
    """\u5e73\u79fb\u8282\u70b9\u81ea\u8eab\u4e0e\u6240\u6709\u5b50\u8282\u70b9\u3002

    \u5fc5\u987b\u9012\u5f52：Figma \u7684\u5b50\u8282\u70b9\u5750\u6807\u662f\u72ec\u7acb的\u7edd\u5bf9\u503c，
    \u53ea\u79fb\u7236\u8282\u70b9\u4f1a\u8ba9\u5b50\u8282\u70b9\u7559\u5728\u539f\u5730。
    """
    b = box(node)
    b["x"] = float(b.get("x", 0)) + dx
    b["y"] = float(b.get("y", 0)) + dy
    for child in node.get("children", []) or []:
        shift(child, dx, dy)


def grow_to_fit(node: dict, pad: float = 0.0, min_h: float | None = None) -> None:
    """把 FRAME 的高度撑到能装下所有子节点。

    Figma 的 clipsContent 是按 absoluteBoundingBox 裁剪的。ui_patches 往下
    插入字段后，容器高度还是原值，新增内容会被静默裁掉——图上表现为
    "底部按钮凭空消失"，而文字数门禁仍然通过。插入类 patch 结束后必须调用。

    只撑高度、不动宽度与 y：横向溢出通常是真出了布局错误，
    应该暴露出来而不是被悄悄放大掩盖。
    """
    if node.get("type") not in ("FRAME", "COMPONENT", "COMPONENT_SET", "GROUP"):
        return
    own = box(node)
    top = float(own.get("y", 0))
    bottom = top + float(own.get("height", 0))
    for child in node.get("children", []) or []:
        cb = child.get("absoluteBoundingBox") or {}
        if cb:
            bottom = max(bottom, float(cb.get("y", 0)) + float(cb.get("height", 0)))
    target = bottom - top + pad
    if min_h is not None:
        target = max(target, min_h)
    own["height"] = max(float(own.get("height", 0)), target)


def bottom_anchors(root: dict, band: float = 90.0) -> list[str]:
    """记录「贴着画板底边」的节点 id，供插入内容后下移。

    桌面端画板结构是「标题栏 + 工作区」，工作区里侧边导航与主内容是通栏高，
    页脚状态栏与侧栏底部块靠底对齐。这些节点在 Figma 里高度与 y 都写死，
    插入字段后画板被撑长，它们不会跟随，结果是页脚压在导出按钮上。

    必须在改动之前快照：改动之后卡片自身已被撑高，底边关系全变了，
    再去猜就会把卡片也当成锚定点重复下移（内容整体漂移）。

    通栏容器（高度接近画板高度）要排除——它们距底也是 0，
    但它们要的是「撑高」而非「平移」，平移会让整块内容凭空位移。
    """
    board = box(root)
    board_h = float(board.get("height", 0))
    board_bottom = float(board.get("y", 0)) + board_h
    stretch_min = board_h * 0.6

    ids: list[str] = []
    stack = [root]
    while stack:
        cur = stack.pop()
        b = cur.get("absoluteBoundingBox") or {}
        if b and cur is not root:
            h = float(b.get("height", 0))
            is_stretch = cur.get("type") == "FRAME" and h >= stretch_min
            bottom = float(b.get("y", 0)) + h
            if not is_stretch and (board_bottom - bottom) <= band:
                ids.append(cur.get("id", ""))
                continue      # 命中即停，整块连同子树一起处理
        stack.extend(cur.get("children") or [])
    return ids


def expand_canvas(root: dict, delta: float, anchor_ids: list[str]) -> None:
    """按已知的插入高度撑开画板。

    delta 由插入的 patch 直接给出（就是它下移后续节点用的那个值），
    不去反推内容底边——反推会把刚撑高的容器也算进去，导致重复位移。
    """
    if delta <= 0:
        return

    # 0. 画板自身也要撑高，否则它的 clipsContent 仍被裁在旧底边。
    root_box = box(root)
    root_box["height"] = float(root_box.get("height", 0)) + delta

    # 1. 撑高通栏容器（工作区 / 侧边导航 / 主内容）：只改 height，绝不动 y
    board_h = float(box(root).get("height", 0))
    stretch_min = board_h * 0.6
    stack = [root]
    while stack:
        cur = stack.pop()
        b = cur.get("absoluteBoundingBox") or {}
        if b and cur is not root and cur.get("type") == "FRAME":
            h = float(b.get("height", 0))
            if h >= stretch_min:
                b["height"] = h + delta
        stack.extend(cur.get("children") or [])

    # 2. 下移底部锚定块
    if not anchor_ids:
        return
    wanted = set(anchor_ids)
    stack = [root]
    while stack:
        cur = stack.pop()
        if cur.get("id") in wanted:
            shift(cur, 0, delta)
            continue
        stack.extend(cur.get("children") or [])


def text_node(name: str, chars: str, x: float, y: float, size: float,
              color: dict = INK, weight: int = 400, width: float | None = None) -> dict:
    """\u6784\u9020\u4e00\u4e2a TEXT \u8282\u70b9\u3002"""
    # CJK 粗体约 1 em，拉丁字符约 0.55 em；否则文字框会宽得过宽或过窄。
    em = sum(0.55 if ord(c) < 0x2E80 else 1.0 for c in chars)
    w = width if width is not None else max(em * size, size)
    return _auto_layout_defaults({
        "id": f"sk-t{id(chars) % 10**9}-{x:.0f}-{y:.0f}",
        "name": name,
        "type": "TEXT",
        "scrollBehavior": "SCROLLS",
        "blendMode": "PASS_THROUGH",
        "characters": chars,
        "fills": _solid(color),
        "absoluteBoundingBox": {"x": x, "y": y, "width": w, "height": size * 1.4},
        "style": {
            "fontFamily": "Noto Sans SC",
            "fontStyle": "Regular",
            "fontWeight": weight,
            "textAutoResize": "HEIGHT",
            "fontSize": size,
            "textAlignHorizontal": "LEFT",
            "textAlignVertical": "TOP",
            "letterSpacing": 0.0,
            "lineHeightPx": size * 1.4,
            "lineHeightPercent": 100.0,
            "lineHeightUnit": "INTRINSIC_%",
        },
    })


def _auto_layout_defaults(node: dict) -> dict:
    """给自建节点补齐 auto-layout 默认值。

    Figma 原生节点一定有 layoutGrow / layoutAlign / layoutMode 三个字段，
    而补丁自建的节点若缺失，relayout 会把 layoutGrow 缺失当成"未知"，
    进而误判为撑满。这里统一补 0.0 / "INHERIT" / "NONE"，语义与 Figma 一致。
    """
    node.setdefault("layoutMode", "NONE")
    node.setdefault("layoutAlign", "INHERIT")
    node.setdefault("layoutGrow", 0.0)
    node.setdefault("layoutPositioning", "AUTO")
    return node


def rect_node(name: str, x: float, y: float, w: float, h: float,
              fill: dict | None = None, radius: float = 0.0,
              stroke: dict | None = None) -> dict:
    """\u6784\u9020\u4e00\u4e2a RECTANGLE \u8282\u70b9\u3002"""
    node: dict[str, Any] = {
        "id": f"sk-r{x:.0f}-{y:.0f}-{radius:.0f}",
        "name": name,
        "type": "RECTANGLE",
        "blendMode": "PASS_THROUGH",
        "fills": _solid(fill) if fill else [],
        "absoluteBoundingBox": {"x": x, "y": y, "width": w, "height": h},
    }
    if radius:
        node["cornerRadius"] = radius
    if stroke:
        node["strokes"] = _solid(stroke)
        node["strokeWeight"] = 1.0
    return _auto_layout_defaults(node)


def clone_frame(source: dict, new_name: str) -> dict:
    """\u6df1\u526f\u672c\u4e00\u4e2a FRAME\uff0c\u7528\u4f5c\u65b0\u753b\u677f\u7684\u5143\u67a4\u3002"""
    node = copy.deepcopy(source)
    node["id"] = f"sk-f{abs(hash(new_name)) % 10**9}"
    node["name"] = new_name
    return node


def set_text(node: dict, chars: str) -> None:
    node["characters"] = chars


def drop_text(parent: dict, needle: str) -> int:
    """按**文本内容**删除子节点，返回删除数量。

    drop() 按 name 匹配，但 Figma 里一屏里大量节点都叫「说明」，
    拿 name 去删会误伤同级兄弟。要删掉「明文已隐藏…」那一行，只能按内容匹配。

    needle 传子串即可，不必写全。
    """
    removed = 0
    kept: list[dict] = []
    for child in parent.get("children", []) or []:
        if needle in str(child.get("characters", "")):
            removed += 1
            continue
        removed += drop_text(child, needle)
        kept.append(child)
    parent["children"] = kept
    return removed


def drop(parent: dict, name: str) -> None:
    """递归删除指定名称的子节点。

    旧版只删直属子节炵，但同名的块常被嵌在中间层（如页面内容
    → 卡片 → 安全提示），表面上看似删掉了实际还在。
    改成递归后消费方式不变，只是多深查几层。
    """
    kept = []
    for child in parent.get("children", []) or []:
        if child.get("name") == name:
            continue
        drop(child, name)
        kept.append(child)
    parent["children"] = kept


# ---------------------------------------------------------------------------
# \u4fee\u6539 1\uff1a\u5de6\u4fa7\u4e3b\u83dc\u5355\u91cd\u6784
#
# \u7528\u6237\u8981\u6c42\uff1a\u83dc\u5355\u987a\u5e8f\u4e3a\u300c\u4e3b\u5bc6\u94a5\u7ba1\u7406 -> \u673a\u5bc6\u4fe1\u606f\u7ba1\u7406 -> \u5b89\u5168\u8bbe\u7f6e\u300d\uff0c
# \u5220\u9664\u300c\u6587\u4ef6\u5bfc\u5165\u300d\u4e0e\u300c\u6587\u4ef6\u5bfc\u51fa\u300d\u4e24\u9879\u3002\u5bfc\u5165/\u5bfc\u51fa\u5165\u53e3\u6539\u5728\u5404\u7ba1\u7406\u9875\u5185\u3002
# \u5f71\u54cd\u6240\u6709\u542b\u4fa7\u8fb9\u5bfc\u822a\u7684\u684c\u9762\u7aef\u753b\u677f\u3002
# ---------------------------------------------------------------------------
NAV_ITEMS = [
    ("key-round", "\u4e3b\u5bc6\u94a5\u7ba1\u7406"),
    ("files", "\u673a\u5bc6\u4fe1\u606f\u7ba1\u7406"),
    ("settings-2", "\u5b89\u5168\u8bbe\u7f6e"),
]


def patch_navigation(root: dict, active: str) -> None:
    """\u91cd\u5199\u4fa7\u8fb9\u5bfc\u822a\uff1a\u53ea\u7559\u4e09\u9879\u3002active \u4e3a\u5f53\u524d\u9009\u4e2d\u9879\u7684\u540d\u79f0\u3002"""
    nav = find(root, "\u5bfc\u822a\u9879\u76ee")
    if nav is None:
        return
    items = nav.get("children", [])
    if len(items) < 3:
        return

    # \u5148\u8bb0\u4f4f\u539f\u6709\u8282\u70b9\u7684\u51e0\u4f55\uff0c\u4f9b\u91cd\u65b0\u6392\u7248\u4f7f\u7528
    src_items = items[:3]
    item_h = float(box(src_items[0]).get("height", 44))
    gap = 8.0
    x = float(box(src_items[0]).get("x", 0))

    # \u539f\u6811\u91cc\u5df2\u7ecf\u6709 key-round / files / settings-2 \u4e09\u4e2a\u56fe\u6807\uff0c
    templates = [items[1], items[0], items[4]] if len(items) >= 5 else [src_items[0]] * 3
    new_items: list[dict] = []
    for index, (icon, label) in enumerate(NAV_ITEMS):
        template = templates[index]
        item = copy.deepcopy(template)
        item["id"] = f"sk-nav-{index}"
        # \u6a21\u677f\u53ef\u80fd\u5df2\u5728\u5176\u4ed6\u4f4d\u7f6e\uff0c\u56e0\u6b64\u76f4\u63a5\u7edd\u5bf9\u5b9a\u4f4d y
        base_y = float(box(src_items[0]).get("y", 0))
        shift(item, 0, base_y + index * (item_h + gap) - float(box(item).get("y", 0)))

        for child in item.get("children", []) or []:
            if child.get("name") == "\u5bfc\u822a\u540d\u79f0":
                set_text(child, label)

        # \u9009\u4e2d\u6001\uff1a\u539f\u8282\u70b9\u7684\u9009\u4e2d\u6001\u7531\u7236\u5c42\u7684 fills \u4e0e\u5b50\u8282\u70b9\u5171\u540c\u8868\u8fbe\uff0c
        # \u8fd9\u91cc\u901a\u8fc7\u5e95\u8272\u533a\u5206\u5f3a\u8c03\u8272\u767d\u5e95\u3002
        selected = label == active
        item["fills"] = _solid({"r": 0.078, "g": 0.471, "b": 0.431, "a": 0.16} if selected else WHITE)
        new_items.append(item)

    nav["children"] = new_items
    nav["absoluteBoundingBox"] = {
        "x": x, "y": float(box(src_items[0]).get("y", 0)),
        "width": float(box(src_items[0]).get("width", 168)),
        "height": len(new_items) * (item_h + gap) - gap,
    }


# ---------------------------------------------------------------------------
# \u4fee\u6539 2\uff1a\u89e3\u9501\u9875\u9875\u811a\u4e0e\u9501\u5b9a\u8bf4\u660e
#
# \u7528\u6237\u8981\u6c42\uff1a\u9875\u811a\u53ea\u4fdd\u7559\u7248\u672c\u53f7\uff1b\u5220\u9664\u5173\u4e8e\u9690\u85cf\u660e\u6587\u3001\u5bc6\u94a5\u7f13\u5b58\u3001
# \u8f6e\u6362\u968f\u673a\u5bc6\u94a5\u7684\u63cf\u8ff0\u2014\u2014\u8fd9\u4e9b\u662f\u5b9e\u73b0\u7ec6\u8282\uff0c\u4e0d\u5e94\u76f4\u63a5\u5c55\u793a\u7ed9\u7528\u6237\u3002
# ---------------------------------------------------------------------------
def patch_unlock_footer(root: dict) -> None:
    """\u9875\u811a\u53ea\u4fdd\u7559\u7248\u672c\u53f7\u3002

    \u9700\u8981\u9012\u5f52\u904d\u5386\uff1a\u8be5\u6587\u6848\u5728\u300c\u54c1\u724c\u4e0e\u5b89\u5168\u627f\u8bfa\u300d\u5bb9\u5668\u5185\uff0c\u4e0d\u662f\u9876\u5c42\u5b50\u8282\u70b9\u3002
    """
    for node in walk_all(root):
        if node.get("type") == "TEXT":
            chars = node.get("characters", "")
            if "\u57fa\u7840\u7248" in chars and "v0.0.1" in chars:
                set_text(node, "v0.0.1")


def patch_unlock_lock_note(root: dict) -> None:
    """\u5220\u9664\u9501\u5b9a\u8bf4\u660e\u6574\u5757\uff1a\u9690\u85cf\u660e\u6587\u4e0e\u5bc6\u94a5\u7f13\u5b58\u3001\u968f\u673a\u5bc6\u94a5\u8f6e\u6362\u5747\u5c5e\u5b9e\u73b0\u7ec6\u8282\u3002"""
    area = find(root, "\u89e3\u9501\u533a\u57df")
    if area is not None:
        drop(area, "\u9501\u5b9a\u8bf4\u660e")


# ---------------------------------------------------------------------------
# \u4fee\u6539 3\uff1a\u5bfc\u51fa\u4e3b\u5bc6\u94a5\u9875\u8865\u300c\u4e3b\u5bc6\u94a5\u539f\u5bc6\u7801\u300d\u8f93\u5165\u6846
#
# \u7528\u6237\u8981\u6c42\uff1a\u5bfc\u51fa\u65f6\u5fc5\u987b\u9a8c\u8bc1\u4e3b\u5bc6\u94a5\u5f53\u524d\u5bc6\u7801\uff0c\u4f46\u73b0\u6709\u753b\u677f\u53ea\u6709
# \u300c\u5bfc\u51fa\u6587\u4ef6\u4fdd\u62a4\u5bc6\u7801\u300d\u4e0e\u300c\u786e\u8ba4\u4fdd\u62a4\u5bc6\u7801\u300d\u4e24\u4e2a\u6846\uff0c\u7f3a\u5c11\u8eab\u4efd\u9a8c\u8bc1\u73af\u8282\u3002
# \u65b0\u589e\u7b2c\u4e00\u4e2a\u6846\uff0c\u4e09\u6846\u4ece\u4e0a\u5230\u4e0b\u4e3a\uff1a\u539f\u5bc6\u7801 -> \u4fdd\u62a4\u5bc6\u7801 -> \u786e\u8ba4\u4fdd\u62a4\u5bc6\u7801\u3002
# \u6ce8\u610f\u8fd9\u91cc\u6709\u4e24\u628a\u4e0d\u540c\u7684\u5bc6\u7801\uff0c\u4e0d\u8981\u6df7\u4e3a\u4e00\u4e2a\uff1a
#   \u539f\u5bc6\u7801     \u2014\u2014 \u9a8c\u8bc1\u5f53\u524d\u8fd9\u628a\u4e3b\u5bc6\u94a5\u672c\u8eab\uff0c\u53ea\u8bfb\u9a8c\u8bc1\uff0c\u4e0d\u53c2\u4e0e\u4efb\u4f55\u52a0\u5bc6\u8fd0\u7b97
#   \u4fdd\u62a4\u5bc6\u7801   \u2014\u2014 \u4e3a\u5bfc\u51fa\u6587\u4ef6\u5355\u72ec\u65b0\u8bbe\uff0c\u5bfc\u5165\u8be5\u6587\u4ef6\u65f6\u4f7f\u7528
# \u786e\u8ba4\u6846\u786e\u8ba4\u7684\u662f\u4fdd\u62a4\u5bc6\u7801\uff0c\u4e0d\u662f\u539f\u5bc6\u7801\u3002
# ---------------------------------------------------------------------------
EXPORT_ORIGINAL_PWD = {
    "label": "\u4e3b\u5bc6\u94a5\u539f\u5bc6\u7801",
    "masked": "\u2022" * 12,
    "hint": "\u9a8c\u8bc1\u8fd9\u628a\u4e3b\u5bc6\u94a5\u7684\u5f53\u524d\u5bc6\u7801\uff1b\u9519\u8bef\u65f6\u5bfc\u51fa\u4e0d\u4f1a\u5f00\u59cb\u3002",
}


def _fix_field_hint_width(field: dict) -> None:
    """把字段内所有 TEXT 子节点的宽度对齐到字段宽度。

    字段里的辅助说明既可能是 FRAME（内含字段提示）也可能是裸 TEXT，
    两者都要跟着字段一起变宽，否则通栏后会停在旧宽度处、看起来居中。
    """
    fb = box(field)
    fw = float(fb.get("width", 0))
    for child in field.get("children", []) or []:
        cb = box(child)
        if child.get("type") == "TEXT":
            cb["x"] = float(fb.get("x", 0))
            cb["width"] = fw
        else:
            _widen(child, fw - float(cb.get("width", 0)))


def patch_export_original_password(root: dict) -> None:
    """\u5728\u5bfc\u51fa\u5361\u7247\u9876\u90e8\u63d2\u5165\u300c\u4e3b\u5bc6\u94a5\u539f\u5bc6\u7801\u300d\u5b57\u6bb5\u3002

    \u539f\u6709\u4e24\u4e2a\u5b57\u6bb5\u7684\u5750\u6807\u662f\u56fa\u5b9a\u7684\uff0c\u63d2\u5165\u65b0\u5b57\u6bb5\u540e\u5fc5\u987b\u628a\u540e\u7eed\u8282\u70b9\u6574\u4f53\u4e0b\u79fb\uff0c
    \u4e0d\u80fd\u53ea\u6539\u5b57\u6bb5\u81ea\u8eab\uff0c\u5426\u5219\u4f1a\u4e0e\u4e0b\u4e00\u4e2a\u5b57\u6bb5\u91cd\u53e0\u3002
    """
    area = find(root, "\u5bfc\u51fa\u5de5\u4f5c\u533a")
    if area is None:
        return
    card = next((c for c in area.get("children", []) or []
                 if c.get("name") == "\u5185\u5bb9\u5361\u7247"), None)
    if card is None:
        return

    children = card.get("children", []) or []
    first_field = next((c for c in children if c.get("name") == "\u8868\u5355\u5b57\u6bb5"), None)
    if first_field is None:
        return

    # \u5148\u4fdd\u7559\u4e00\u4efd\u672a\u79fb\u52a8\u7684\u526f\u672c\uff1a\u65b0\u5b57\u6bb5\u8981\u4ece\u5b83\u62df\u51fa\u5e03\u5c40\uff0c
    # \u4e0d\u80fd\u5728\u5b83\u5df2\u7ecf\u4e0b\u79fb\u4e4b\u540e\u518d\u62f7\u8d1d\uff0c\u5426\u5219\u4f1a\u7ee7\u627f\u65b0\u7684 y\u3002
    # 素材先快照：插入会撑高卡片自身，之后底边关系全变。
    # 很后才去猜哪些是底部锚定块，会把卡片也算进去，造成内容漂移。
    anchors = bottom_anchors(root)

    pristine = copy.deepcopy(first_field)

    # \u65b0\u5b57\u6bb5\u7684\u9ad8\u5ea6\u4e0e\u539f\u6709\u5b57\u6bb5\u4e00\u81f4\uff08102\uff0c\u542b\u5b57\u6bb5\u8f85\u52a9\u884c\uff09
    field_h = 102.0
    gap = 16.0
    top_y = float(box(first_field).get("y", 0))
    new_y = top_y
    push_down = field_h + gap

    # 卡片内在第一个字段之后的所有内容整体下移，让出 field_h + gap 的空间
    for child in children:
        if child is first_field:
            continue
        if float(box(child).get("y", 0)) > top_y:
            shift(child, 0, push_down)
    # 原第一个字段自己也要下移，新字段占用它原来的位置
    shift(first_field, 0, push_down)

    field = pristine
    set_text(field["children"][0], EXPORT_ORIGINAL_PWD["label"])
    # 克隆自「导出文件保护密码」字段，它的辅助行是 532 宽的独立 TEXT。
    # 卡片随后会被 widen_workspace 加宽，TEXT 不吃 _widen，
    # 这里必须先按字段宽度归位，否则辅助行会停在卡片中间。
    # hint 是随后新建的 TEXT，坐标按字段左边给；widen_workspace 之后
    # 字段会变宽，这里再按最终宽度重设一次，保证与标签、输入区左对齐。
    _fix_field_hint_width(field)
    input_area = find(field, "\u8f93\u5165\u533a\u57df")
    set_text(input_area["children"][0], EXPORT_ORIGINAL_PWD["masked"])
    drop(field, "\u5b57\u6bb5\u8f85\u52a9")
    hint = text_node("\u5b57\u6bb5\u8f85\u52a9", EXPORT_ORIGINAL_PWD["hint"],
                     float(box(field).get("x", 0)), new_y + 83, 12, MUTED,
                     width=float(box(field).get("width", 532)))
    field["children"].append(hint)
    field["absoluteBoundingBox"] = dict(box(field))
    field["absoluteBoundingBox"]["height"] = field_h

    out: list[dict] = []
    for child in children:
        if child is first_field:
            out.append(field)   # 新字段占住原第一个字段的位置
        out.append(child)       # 原字段（含其文案）已下移，跟在新字段之后
    card["children"] = out

    # 卡片与工作区都需撑高，否则底部按钮被 clipsContent 静默剪掉
    grow_to_fit(card, pad=24)
    grow_to_fit(area, pad=24)
    expand_canvas(root, push_down, anchors)

# ---------------------------------------------------------------------------
# \u4fee\u6539 4\uff1a\u673a\u5bc6\u4fe1\u606f\u7ba1\u7406\u9875\u2014\u2014\u6807\u9898\u6539\u540d + \u5bfc\u5165\u6309\u94ae\u4e0e\u65b0\u589e\u5e76\u6392
#
# \u7528\u6237\u8981\u6c42\uff1a\u9875\u9762\u6807\u9898\u4ece\u300c\u673a\u5bc6\u4fe1\u606f\u300d\u6539\u4e3a\u300c\u673a\u5bc6\u4fe1\u606f\u7ba1\u7406\u300d\uff08\u4e0e\u5de6\u4fa7\u83dc\u5355\u540c\u540d\uff09\uff1b
# \u5217\u8868\u5de5\u5177\u680f\u91cc\u72ec\u5360\u4e00\u5217\u7684\u300c\u5bfc\u5165\u300d\u6309\u94ae\u79fb\u5230\u9875\u9762\u6807\u9898\u53f3\u4fa7\uff0c\u4e0e\u300c\u65b0\u589e\u673a\u5bc6\u4fe1\u606f\u300d\u5e76\u6392\u3002
# ---------------------------------------------------------------------------
def patch_secret_page(root: dict) -> None:
    content = find(root, "\u4e3b\u5185\u5bb9")
    if content is None:
        return

    # \u6807\u9898\u6539\u540d
    for child in find(content, "\u6807\u9898\u8bf4\u660e").get("children", []) or []:
        if child.get("type") == "TEXT" and child.get("characters") == "\u673a\u5bc6\u4fe1\u606f":
            set_text(child, "\u673a\u5bc6\u4fe1\u606f\u7ba1\u7406")

    title = find(content, "\u9875\u9762\u6807\u9898")
    toolbar = find(content, "\u5217\u8868\u5de5\u5177\u680f")
    # \u300c\u65b0\u589e\u673a\u5bc6\u4fe1\u606f\u300d\u6309\u94ae\u5728\u9875\u9762\u6807\u9898\u53f3\u4fa7\uff0c
    # \u5bb9\u5668\u540d\u4e3a\u300c\u64cd\u4f5c\u4f5c\u7528\u300d\u4f46\u5b50\u8282\u70b9\u540d\u4e3a\u300c\u64cd\u4f5c\u6309\u94ae\u300d
    add_button = None
    if title is not None:
        add_button = next((c for c in title.get("children", []) or []
                              if c.get("name") == "\u64cd\u4f5c\u6309\u94ae"), None)
    # \u4ece\u5de5\u5177\u680f\u53d6\u51fa\u5bfc\u5165\u6309\u94ae\u7684\u6a21\u677f
    import_btn = None
    if toolbar is not None:
        for child in toolbar.get("children", []) or []:
            if child.get("name") == "\u64cd\u4f5c\u6309\u94ae":
                import_btn = child
        drop(toolbar, "\u64cd\u4f5c\u6309\u94ae")
        # \u641c\u7d22\u6846\u62c9\u5bbd\u56de\u6574\u884c\uff0c\u586b\u8d70\u6309\u94ae\u7684\u4f4d\u7f6e
        search = find(toolbar, "\u641c\u7d22")
        if search is not None:
            shift(search, float(box(import_btn).get("width", 72)) + 12 - 16, 0)
            search["absoluteBoundingBox"]["width"] = 530.0 - 32

    if import_btn is not None and add_button is not None:
        # \u4e0d\u80fd\u76f4\u63a5\u585e\u8fdb\u53bb\uff1a\u300c\u65b0\u589e\u673a\u5bc6\u4fe1\u606f\u300d\u662f\u5e26\u5f3a\u8c03\u8272\u5e95\u7684\u5b9e\u5fc3\u6309\u94ae\uff0c
        # \u5b50\u6309\u94ae\u88ab\u5305\u88f9\u540e\u4f1a\u88ab\u5b83\u7684 clip \u622a\u6389\uff0c\u4e14\u80cc\u666f\u4e5f\u4f1a\u88ab\u8986\u76d6\u3002
        # \u6539\u4e3a\u4e24\u4e2a\u6309\u94ae\u5e76\u5217\u653e\u5728\u4e00\u4e2a\u65e0\u80cc\u666f\u7684\u884c\u5bb9\u5668\u91cc\u3002
        gap = 12.0
        add_w = float(box(add_button).get("width", 146))
        imp_w = float(box(import_btn).get("width", 72))
        row_x = float(box(add_button).get("x", 0))
        row_y = float(box(add_button).get("y", 0))

        shift(add_button, 0, 0)
        shift(import_btn, row_x - float(box(import_btn).get("x", 0)),
              row_y - float(box(import_btn).get("y", 0)))
        # \u5bfc\u5165\u6309\u94ae\u539f\u672c\u5728\u5de6\u4fa7\uff0c\u8c03\u6362\u4e3a\u7d27\u8ddf\u5728\u53f3\u4fa7
        shift(import_btn, -(imp_w + gap), 0)

        row = {
            "id": "sk-btn-row",
            "name": "\u9875\u9762\u64cd\u4f5c",
            "type": "FRAME",
            "fills": [],
            "scrollBehavior": "SCROLLS",
            "absoluteBoundingBox": {
                "x": row_x - imp_w - gap, "y": row_y,
                "width": add_w + imp_w + gap, "height": float(box(add_button).get("height", 44)),
            },
            "children": [import_btn, add_button],
        }
        title["children"] = [row if c is add_button else c for c in title["children"]]

    # \u5de5\u5177\u680f\u62c9\u9ad8\u56de\u53bb\u6389\u6309\u94ae\u5360\u7684\u4e00\u884c\uff08\u641c\u7d22\u53d8\u6210\u5355\u884c\u5168\u5bbd\uff09
    if toolbar is not None:
        toolbar["absoluteBoundingBox"] = dict(box(toolbar))
        toolbar["absoluteBoundingBox"]["height"] = 36.0


# ---------------------------------------------------------------------------
# \u4fee\u6539 5\uff1a\u65b0\u589e\u300c\u5bfc\u5165\u4e3b\u5bc6\u94a5\u300d\u753b\u677f
#
# \u8bbe\u8ba1\u7a3f\u539f\u672c\u6ca1\u6709\u8fd9\u5f20\u753b\u677f\uff08\u684c\u9762\u7aef\u53ea\u6709\u5bfc\u51fa\uff09\u3002\u73b0\u6309\u4e0e\u300c\u5bfc\u51fa\u4e3b\u5bc6\u94a5\u300d\u5bf9\u79f0\u7684
# \u65b9\u5f0f\u8865\u4e00\u5f20\uff1a\u9009\u6587\u4ef6 -> \u6587\u4ef6\u4fdd\u62a4\u5bc6\u7801 -> \u65b0\u7684\u4e3b\u5bc6\u94a5\u5bc6\u7801 -> \u5bfc\u5165\u4e3a\u5907\u9009\u4e3b\u5bc6\u94a5\u3002
# \u5b57\u6bb5\u4e0e\u6587\u6848\u53d6\u81ea\u9700\u6c42\u539f\u6587 2.4 \u4e0e\u79fb\u52a8\u7aef\u73b0\u6709\u7684\u300c\u751f\u6210\u4e0e\u5bfc\u5165\u4e3b\u5bc6\u94a5\u300d\u753b\u677f\u3002
# ---------------------------------------------------------------------------
def build_import_master_key(source: dict) -> dict:
    """\u4ee5\u300c\u5bfc\u51fa\u4e3b\u5bc6\u94a5\u300d\u753b\u677f\u4e3a\u5143\u67a4\u751f\u6210\u300c\u5bfc\u5165\u4e3b\u5bc6\u94a5\u300d\u753b\u677f\u3002"""
    board = copy.deepcopy(source)
    board["id"] = "sk-import-master-key"
    board["name"] = "\u684c\u9762\u7aef \u00b7 \u5bfc\u5165\u4e3b\u5bc6\u94a5"

    # \u5bfc\u822a\u4e3b\u83dc\u5355\u5f53\u524d\u9009\u4e2d\u300c\u4e3b\u5bc6\u94a5\u7ba1\u7406\u300d
    patch_navigation(board, "\u4e3b\u5bc6\u94a5\u7ba1\u7406")

    content = find(board, "\u4e3b\u5185\u5bb9")
    patch_title(content, "\u5bfc\u5165\u4e3b\u5bc6\u94a5",
                "\u4ece\u4e4b\u524d\u5bfc\u51fa\u7684\u5907\u4efd\u6587\u4ef6\u6062\u590d\u4e3b\u5bc6\u94a5\uff0c\u5bfc\u5165\u540e\u4f5c\u4e3a\u5907\u9009\u4fdd\u5b58\u3002")

    # \u5bfc\u51fa\u5361\u7247 -> \u5bfc\u5165\u5361\u7247\uff1a\u91cd\u5efa\u5185\u5bb9\u533a
    area = find(content, "\u5bfc\u51fa\u5de5\u4f5c\u533a")
    if area is not None:
        area["name"] = "\u5bfc\u5165\u5de5\u4f5c\u533a"
        card = next((c for c in area.get("children", []) or [] if c.get("name") == "\u5185\u5bb9\u5361\u7247"), None)
        if card is not None:
            x = float(box(card).get("x", 0))
            w = float(box(card).get("width", 532))
            y = float(box(card).get("y", 0))

            # \u5220\u9664\u539f\u6709\u5185\u5bb9\uff0c\u6309\u4ece\u4e0a\u5230\u4e0b\u7684\u987a\u5e8f\u91cd\u5efa
            file_slot = _import_file_slot(x, y, w)
            fields = [
                _pwd_field(x, y + 116, w, "\u6587\u4ef6\u4fdd\u62a4\u5bc6\u7801", "\u2022" * 12,
                           "\u5bfc\u51fa\u65f6\u4e3a\u8fd9\u4efd\u5907\u4efd\u8bbe\u5b9a\u7684\u5bc6\u7801\u3002"),
                _pwd_field(x, y + 213, w, "\u65b0\u7684\u4e3b\u5bc6\u94a5\u5bc6\u7801", "\u2022" * 12,
                           "\u4e3a\u672c\u673a\u4fdd\u5b58\u7684\u4e3b\u5bc6\u94a5\u65b0\u8bbe\u5bc6\u7801\u3002"),
            ]
            actions = _import_actions(x, y + 315, w)

            card["children"] = [file_slot] + fields + [actions]
            card["absoluteBoundingBox"] = {
                "x": x, "y": y, "width": w, "height": 359.0,
            }
    return board


def _import_file_slot(x: float, y: float, w: float) -> dict:
    """\u6587\u4ef6\u9009\u62e9\u5361\uff08\u5bf9\u9f50\u79fb\u52a8\u7aef\u73b0\u6709\u7684\u300c\u6587\u4ef6\u9009\u62e9\u300d\u6837\u5f0f\uff09\u3002"""
    node = {
        "id": "sk-imp-file", "name": "\u6587\u4ef6\u9009\u62e9", "type": "FRAME",
        "fills": _solid(SURFACE), "cornerRadius": 12.0,
        "absoluteBoundingBox": {"x": x, "y": y, "width": w, "height": 96.0},
        "children": [
            text_node("\u6587\u4ef6\u540d\u79f0", "\u4e3b\u5bc6\u94a5\u5907\u4efd.key", x + 16, y + 24, 14, INK),
            text_node("\u8bf4\u660e", "\u672c\u5730\u6587\u4ef6 \u00b7 \u5df2\u9009\u62e9", x + 16, y + 52, 12, MUTED),
            {
                "id": "sk-imp-pick", "name": "\u64cd\u4f5c\u6309\u94ae", "type": "FRAME",
                "fills": _solid(WHITE), "cornerRadius": 8.0, "strokes": _solid(BORDER),
                "absoluteBoundingBox": {"x": x + w - 112, "y": y + 31, "width": 96, "height": 34},
                "children": [text_node("\u6309\u94ae\u6587\u5b57", "\u91cd\u65b0\u9009\u62e9\u6587\u4ef6", x + w - 96, y + 39, 13, INK)],
            },
        ],
    }
    return node


def _pwd_field(x: float, y: float, w: float, label: str, masked: str, hint: str) -> dict:
    return {
        "id": f"sk-imp-{label}", "name": "\u8868\u5355\u5b57\u6bb5", "type": "FRAME",
        "fills": [], "absoluteBoundingBox": {"x": x, "y": y, "width": w, "height": 81.0},
        "children": [
            text_node("\u5b57\u6bb5\u6807\u7b7e", label, x, y, 13, INK),
            {
                "id": f"sk-imp-in-{label}", "name": "\u8f93\u5165\u533a\u57df", "type": "FRAME",
                "fills": _solid(WHITE), "cornerRadius": 8.0, "strokes": _solid(BORDER),
                "absoluteBoundingBox": {"x": x, "y": y + 29, "width": w, "height": 46},
                "children": [text_node("\u63cf\u8fb9\u6587\u5b57", masked, x + 16, y + 42, 14, MUTED)],
            },
            text_node("\u5b57\u6bb5\u8f85\u52a9", hint, x, y + 60, 12, MUTED),
        ],
    }


def _import_actions(x: float, y: float, w: float) -> dict:
    return {
        "id": "sk-imp-actions", "name": "\u5bfc\u5165\u64cd\u4f5c", "type": "FRAME",
        "fills": [], "absoluteBoundingBox": {"x": x, "y": y, "width": w, "height": 44.0},
        "children": [
            {
                "id": "sk-imp-go", "name": "\u64cd\u4f5c\u6309\u94ae", "type": "FRAME",
                "fills": _solid(ACCENT), "cornerRadius": 8.0,
                "absoluteBoundingBox": {"x": x, "y": y, "width": 168, "height": 44},
                "children": [text_node("\u6309\u94ae\u6587\u5b57", "\u5bfc\u5165\u4e3a\u5907\u9009\u4e3b\u5bc6\u94a5", x + 24, y + 13, 14, WHITE)],
            },
            {
                "id": "sk-imp-cancel", "name": "\u64cd\u4f5c\u6309\u94ae", "type": "FRAME",
                "fills": _solid(WHITE), "cornerRadius": 8.0, "strokes": _solid(BORDER),
                "absoluteBoundingBox": {"x": x + 180, "y": y, "width": 106, "height": 44},
                "children": [text_node("\u6309\u94ae\u6587\u5b57", "\u8fd4\u56de\u7ba1\u7406", x + 204, y + 13, 14, INK)],
            },
        ],
    }


def patch_title(content: dict, title: str, subtitle: str) -> None:
    """\u6539\u5199\u9875\u9762\u6807\u9898\u533a\u7684\u4e3b\u6807\u9898\u4e0e\u526f\u6807\u9898\u3002"""
    holder = find(content, "\u6807\u9898\u8bf4\u660e")
    if holder is None:
        return
    for child in holder.get("children", []) or []:
        if child.get("type") != "TEXT":
            continue
        if child.get("name") == "\u6807\u9898":
            set_text(child, title)
        elif child.get("name") == "\u8bf4\u660e":
            set_text(child, subtitle)


# ---------------------------------------------------------------------------
# \u4fee\u6539 6\uff1a\u79fb\u52a8\u7aef\u4e09\u5904
#
# 6a \u89e3\u9501\u9875\uff1a\u540c\u684c\u9762\u7aef\uff0c\u5220\u9664\u300c\u660e\u6587\u5df2\u9690\u85cf\uff0c\u5185\u5b58\u5bc6\u94a5\u7f13\u5b58\u5df2\u6e05\u7a7a\u3002\u300d
# 6b \u673a\u5bc6\u4fe1\u606f\u5217\u8868\uff1a\u5bfc\u5165\u6309\u94ae\u4ece\u5e95\u90e8\u300c\u6587\u4ef6\u5165\u53e3\u300d\u79fb\u5230\u5217\u8868\u4e0a\u65b9\u4e0e\u300c\u65b0\u589e\u300d\u5e76\u6392\uff1b
#     \u5e76\u5220\u9664\u5e95\u90e8\u7684\u300c\u5bfc\u51fa\u4e0e\u5907\u4efd\u300d\u6309\u94ae\uff08\u79fb\u52a8\u7aef\u5bfc\u51fa\u4ec5\u4fdd\u7559\u5728\u8be6\u60c5\u9875\uff09\u3002
# 6c \u751f\u6210\u4e3b\u5bc6\u94a5 TAB\uff1a\u539f\u753b\u677f\u53ea\u6709\u300c\u4ece\u6587\u4ef6\u5bfc\u5165\u300d\u6001\uff0c\u8865\u4e00\u5f20\u300c\u751f\u6210\u4e3b\u5bc6\u94a5\u300d\u6001\u3002
# ---------------------------------------------------------------------------
GAP = 8.0  # \u6309\u94ae\u4e4b\u95f4\u7684\u95f4\u8ddd\uff0c\u4e0e\u684c\u9762\u7aef\u4fdd\u6301\u4e00\u81f4


def patch_mobile_secret_list(root: dict) -> None:
    content = find(root, "\u9875\u9762\u5185\u5bb9")
    if content is None:
        return

    header = find(content, "\u5bb9\u91cf\u4e0e\u65b0\u589e")
    footer = find(content, "\u6587\u4ef6\u5165\u53e3")
    add_button = find(header, "\u64cd\u4f5c\u6309\u94ae")

    # \u5e95\u90e8\u300c\u5bfc\u51fa\u4e0e\u5907\u4efd\u300d\u6309\u94ae\u5220\u9664\uff1a\u5bfc\u51fa\u4ec5\u4fdd\u7559\u5728\u8be6\u60c5\u9875
    import_btn = None
    if footer is not None:
        buttons = [c for c in footer.get("children", []) or [] if c.get("name") == "\u64cd\u4f5c\u6309\u94ae"]
        if buttons:
            import_btn = buttons[0]
        content["children"] = [c for c in content["children"] if c is not footer]

    # \u91cd\u6392\u4e0b\u65b9\u5185\u5bb9\uff0c\u56e0\u4e3a\u5e95\u90e8\u6574\u5757\u88ab\u5220\u9664\uff0c\u9700\u8981\u6536\u7d27\u9ad8\u5ea6
    shift(find(content, "\u8bf4\u660e"), 0, -48.0)

    if import_btn is not None and header is not None:
        # \u5bfc\u5165\u79fb\u5230\u5217\u8868\u4e0a\u65b9\uff0c\u4e0e\u300c\u65b0\u589e\u300d\u5e76\u6392\u3002
        # \u6ce8\u610f\uff1a\u65b0\u6309\u94ae\u662f\u4ece\u9875\u811a\u62bd\u8d70\u7684\uff0cy \u4ecd\u662f\u5e95\u90e8\u7684\u5750\u6807\uff0c
        # \u76f4\u63a5 append \u5230 header \u4f1a\u8ba9\u5b83\u6e32\u67d3\u5728\u5217\u8868\u4e0b\u65b9\u3002\u5fc5\u987b\u663e\u5f0f\u6539\u5199 y\u3002
        set_text(find(import_btn, "\u6309\u94ae\u6587\u5b57"), "\u5bfc\u5165")

        add_box = box(find(header, "\u64cd\u4f5c\u6309\u94ae"))
        new_w = 72.0
        # \u5fc5\u987b\u7528 shift() \u800c\u4e0d\u662f\u76f4\u63a5\u5199\u7236\u8282\u70b9\u5750\u6807\uff1a
        # Figma \u5b50\u8282\u70b9\u5750\u6807\u662f\u72ec\u7acb\u7edd\u5bf9\u503c\uff0c\u76f4\u63a5\u6539\u7236\u8282\u70b9\u4f1a\u8ba9\u56fe\u6807\u4e0e\u6587\u5b57
        # \u7559\u5728\u539f\u5730\uff08\u8868\u73b0\u4e3a\u6309\u94ae\u662f\u4e2a\u7a7a\u767d\u6846\uff09\u3002
        # header 是 FILL 容器（宽 350），但源画布里「新增」按钮停在 x=298
        # （右边缘 370）已越出容器。先把新增按钮贴到 header 右边缘，
        # 导入按钮再按「新增按钮左侧一个间距」落位。
        hdr = box(header)
        add_x = (float(hdr.get("x", 0)) + float(hdr.get("width", 0))
                 - float(add_box.get("width", 0)))
        shift(add_button, add_x - float(add_box.get("x", 0)), 0)
        add_box = box(add_button)
        dst_x = float(add_box.get("x", 0)) - GAP - new_w
        dst_y = float(add_box.get("y", 0))
        shift(import_btn, dst_x - float(box(import_btn).get("x", 0)),
              dst_y - float(box(import_btn).get("y", 0)))
        target = box(import_btn)
        target["width"] = new_w
        target["height"] = float(add_box.get("height", 34))

        # \u7f29\u5bbd\u5230 72px \u540e\uff0c\u539f\u6765 170px \u5bbd\u7684\u5185\u5bb9\u504f\u79fb\u4f1a\u8ba9\u56fe\u6807\u4e0e\u6587\u5b57\u6324\u5728\u53f3\u4fa7\u3002
        # \u91c7\u7528\u300c\u65b0\u589e\u300d\u6309\u94ae\u7684\u5185\u5bb9\u95f4\u8ddd\uff08\u56fe\u6807 +12\u3001\u6587\u5b57 +36\uff09\u91cd\u6392\u3002
        # \u6ce8\u610f\uff1a\u56fe\u6807 FRAME \u91cc\u7684 VECTOR \u4e5f\u662f\u7edd\u5bf9\u5750\u6807\uff0c\u5fc5\u987b\u4e00\u8d77\u5e73\u79fb\uff0c
        # \u5426\u5219\u8def\u5f84\u4f1a\u62c9\u51fa\u4e00\u6761\u622a\u5230\u6309\u94ae\u5916\u7684\u7ebf\u3002
        icon = next((c for c in import_btn.get("children", []) or []
                     if c.get("type") == "FRAME"), None)
        label = find(import_btn, "\u6309\u94ae\u6587\u5b57")
        base_x = float(target["x"])
        base_y = float(target["y"])
        bh = float(target["height"])
        if icon is not None:
            ib = box(icon)
            dst = base_x + 12.0
            shift(icon, dst - float(ib.get("x", dst)), 0)
            ib = box(icon)
            ib["y"] = base_y + (bh - float(ib.get("height", 16))) / 2
        if label is not None:
            lb = box(label)
            lb["x"] = base_x + 36.0
            lb["y"] = base_y + (bh - float(lb.get("height", 14))) / 2
            lb["width"] = 28.0
        header["children"].append(import_btn)
        # relayout 会按主轴重排 header 的子节点，而「导入」是后插入的成员。
        # 显式标成绝对定位并贴到「新增」按钮左侧，才不会被排到容器外。
        import_btn["layoutPositioning"] = "ABSOLUTE"
        import_btn["layoutSizingHorizontal"] = "FIXED"
        import_btn["layoutGrow"] = 0.0
        ib = box(import_btn)
        ib["width"] = new_w
        ib["x"] = float(add_box.get("x", 0)) - GAP - new_w
        ib["y"] = float(add_box.get("y", 0))
        align_icon_vector(find(import_btn, "file-input") or import_btn)


def build_mobile_generate_key(source: dict) -> dict:
    """生成不带 TAB 的「生成主密钥」画板。

    需求把移动端的生成与导入拆成两个独立界面，各自不该再带
    「生成主密钥 / 从文件导入」这组切换标签——页面标题已经说明了
    这是哪一步，留着标签只会让人以为还能切回去。
    """
    board = copy.deepcopy(source)
    board["id"] = "sk-mobile-generate-key"
    board["name"] = "\u79fb\u52a8\u7aef \u00b7 \u751f\u6210\u4e3b\u5bc6\u94a5"

    content = find(board, "\u9875\u9762\u5185\u5bb9")
    if content is None:
        return board
    drop(board, "\u6dfb\u52a0\u65b9\u5f0f")

    # y 基准必须是 content 的**绝对**顶边：源画板里 content 位于画布 y=2712，
    # 用相对画板的 268 会让整块内容飞到负坐标。
    cb = box(content)
    x = float(cb.get("x", 0)) + 20
    w = float(cb.get("width", 390)) - 40
    top = float(cb.get("y", 0)) + 8.0

    # 逐块累加 y：字段高度不一（带辅助说明的 97、纯输入的 75），
    # 用固定偏移会让相邻块重叠。
    cursor = top
    title = text_node("\u6807\u9898", "\u751f\u6210\u4e3b\u5bc6\u94a5", x, cursor, 20, INK, width=w)
    cursor += 40.0
    subtitle = text_node("\u8bf4\u660e", "\u5728\u672c\u673a\u751f\u6210\u4e00\u628a\u968f\u673a RSA-2048 \u4e3b\u5bc6\u94a5\u3002",
                         x, cursor, 13, MUTED, width=w)
    cursor += 34.0
    fields = []
    for label, value, hint in (
            ("\u4e3b\u5bc6\u94a5\u540d\u79f0\uff08\u53ef\u9009\uff09", "\u53ef\u4ee5\u7559\u7a7a",
             "\u540d\u79f0\u4ec5\u7528\u4e8e\u8fa8\u8bc6\uff0c\u4e0d\u53c2\u4e0e\u52a0\u5bc6\u3002"),
            ("\u4e3b\u5bc6\u94a5\u5bc6\u7801", "\u2022" * 11, None),
            ("\u786e\u8ba4\u4e3b\u5bc6\u94a5\u5bc6\u7801", "\u2022" * 11, None)):
        field = _mobile_pwd(x, cursor, w, label, value, hint)
        fields.append(field)
        cursor += float(box(field).get("height", 75.0)) + 16.0
    warning = _mobile_notice(x, cursor, w,
        "\u8bf7\u59a5\u5584\u8bb0\u4f4f\u4e3b\u5bc6\u94a5\u5bc6\u7801",
        "\u5bc6\u7801\u4e22\u5931\u65e0\u6cd5\u89e3\u5bc6\uff0c\u5bc6\u5323\u65e0\u6cd5\u4e3a\u4f60\u91cd\u7f6e\u5bc6\u7801\u3002")
    cursor += 68.0 + 20.0
    action = _mobile_button(x, cursor, w, "\u751f\u6210\u4e3b\u5bc6\u94a5", ACCENT, WHITE, 168)

    content["children"] = [title, subtitle] + fields + [warning, action]
    # 内容高度必须按最后一个子节点收紧：_shrink_board 靠 content 的底边
    # 推算底部固定块的位置，高度不对会把「系统手势区域」顶到负坐标。
    cb = box(content)
    last = box(action)
    cb["height"] = (float(last.get("y", 0)) + float(last.get("height", 0))
                    - float(cb.get("y", 0)))
    _shrink_board(board)
    return board


def _shrink_board(board: dict) -> None:
    """画板高度按内容收紧，并让底部固定块贴住新底边。"""
    content = find(board, "\u9875\u9762\u5185\u5bb9")
    if content is None:
        return
    cb = box(content)
    bottom = float(cb.get("y", 0)) + float(cb.get("height", 0))
    reflow_mobile(board, bottom + 16.0 + 72.0, ("\u7cfb\u7edf\u624b\u52bf\u533a\u57df", "\u5e95\u90e8\u5bfc\u822a"))
    bb = box(board)
    # 画板高度跟随内容增长；原来写死 844，内容超出时底部被 clipsContent 静默剪掉。
    bb["height"] = max(844.0, bottom + 16.0 + 72.0 - float(bb.get("y", 0)))


def _mobile_pwd(x: float, y: float, w: float, label: str, value: str, hint: str | None) -> dict:
    children = [
        text_node("\u5b57\u6bb5\u6807\u7b7e", label, x, y, 13, INK, width=w),
        {
            "id": f"sk-m-{label}", "name": "\u8f93\u5165\u533a\u57df", "type": "FRAME",
            "fills": _solid(WHITE), "cornerRadius": 8.0, "strokes": _solid(BORDER),
            "absoluteBoundingBox": {"x": x, "y": y + 29, "width": w, "height": 46},
            "children": [text_node("\u63cf\u8fb9\u6587\u5b57", value, x + 14, y + 42, 14, MUTED)],
        },
    ]
    height = 75.0
    if hint:
        children.append(text_node("\u5b57\u6bb5\u8f85\u52a9", hint, x, y + 60, 12, MUTED, width=w))
        height = 97.0
    return {
        "id": f"sk-mf-{label}", "name": "\u8868\u5355\u5b57\u6bb5", "type": "FRAME",
        "fills": [], "absoluteBoundingBox": {"x": x, "y": y, "width": w, "height": height},
        "children": children,
    }


def _mobile_notice(x: float, y: float, w: float, title: str, body: str) -> dict:
    return {
        "id": f"sk-n-{title}", "name": "\u5b89\u5168\u63d0\u793a", "type": "FRAME",
        "fills": _solid({"r": 1.0, "g": 0.969, "b": 0.89, "a": 1.0}),
        "cornerRadius": 8.0,
        "absoluteBoundingBox": {"x": x, "y": y, "width": w, "height": 68.0},
        "children": [
            text_node("\u63d0\u793a\u5185\u5bb9", title, x + 14, y + 12, 13, INK, width=w - 28),
            text_node("\u63d0\u793a\u5185\u5bb9", body, x + 14, y + 34, 12, MUTED, width=w - 28),
        ],
    }


def _mobile_button(x: float, y: float, w: float, label: str,
                   fill: dict, text_color: dict, width: int) -> dict:
    """中居按钮。width 是按钮宽，文字按字号真实宽度居中。

    CJK 字宽约为 1 em，按 14px 算 5 个字应占 70px。若按西文
    的 0.62 系数估算，文字框会弯到按钮左边被裁掉。
    """
    size = 14
    text_w = len(label) * size
    bx = x + (w - width) / 2
    return {
        "id": f"sk-b-{label}", "name": "操作按钮", "type": "FRAME",
        "fills": _solid(fill), "cornerRadius": 8.0,
        # 横向流 + 中居：relayout 才会把文字定位到按钮中心。
        # 只写绝对坐标的话，relayout 会把它当成绝对定位处理，
        # 文字被推到按钮右侧被 clipPath 剪掉。
        "layoutMode": "HORIZONTAL",
        "primaryAxisAlignItems": "CENTER",
        "counterAxisAlignItems": "CENTER",
        "absoluteBoundingBox": {"x": bx, "y": y, "width": width, "height": 44},
        "children": [text_node("按钮文字", label,
                               bx + (width - text_w) / 2, y + 13, size, text_color,
                               width=text_w)],
    }



# ---------------------------------------------------------------------------
# 状态图标与钥匙选择器（v2 新增）
# ---------------------------------------------------------------------------
# 找到 / 缺失两种状态的语义色。取自 UI 设计规范里的强调色与危险色。
OK_COLOR = {"r": 0.0784313753247261, "g": 0.47058823704719543, "b": 0.4274509847164154, "a": 1.0}
ALERT_COLOR = {"r": 0.6980392336845398, "g": 0.27843138575553894, "b": 0.27843138575553894, "a": 1.0}
WARN_COLOR = {"r": 0.7098039215686275, "g": 0.5137254901960784, "b": 0.09411764705882353, "a": 1.0}

# 画布按节点 ID 登记，供跨画板克隆原生控件（主密钥选择器）时取用。
# 跨端禁止复制：桌面端与移动端的界面形态不同，各自只克隆**本端**的原生节点。
# 需要在 relayout 之后收紧高度的卡片。relayout 按 Figma 的
# absoluteBoundingBox 重算子节炵，会把插入行后的高度计算绕过去，
# 所以收紧必须留到 relayout 之后。
TIGHTEN: list[dict] = []


def tighten_pending() -> None:
    """收紧已标记卡片的高度。由 build_board 在 relayout 之后调用。"""
    while TIGHTEN:
        _tighten(TIGHTEN.pop())


_CANVAS: dict = {}


def set_canvas(canvas: dict) -> None:
    _CANVAS.clear()
    _CANVAS.update({c["id"]: c for c in canvas.get("children") or []
                    if isinstance(c, dict) and "id" in c})
    load_icon_library(canvas)


_ICON_LIBRARY: dict[str, dict] = {}
_ICON_NAMES = ("circle-check", "circle-alert", "chevron-down", "key-round",
               "eye-off", "shield-check", "lock-keyhole", "file-input",
               "file-output", "files", "settings-2")


def load_icon_library(canvas: dict) -> None:
    """从原始画布收集可复用图标，供各画板 clone。

    图标路径由 Figma 的 vectorNetwork 表达。自行拼 path 会与整套图标的
    线宽、圆角、留白都不一致，因此一律深拷贝原节点，只改 id、位置与描边色。
    """
    found: dict[str, dict] = {}

    def visit(node: dict) -> None:
        name = node.get("name")
        if name in _ICON_NAMES and name not in found and node.get("children"):
            found[name] = node
        for child in node.get("children", []) or []:
            visit(child)

    for child in canvas.get("children", []) or []:
        visit(child)
    _ICON_LIBRARY.update(found)


def recolor_icon(node: dict, color: dict) -> None:
    """改写图标子树里所有描边色。"""
    for child in walk_all(node):
        if child.get("type") == "VECTOR":
            child["strokes"] = _solid(color)


def clone_icon(name: str, x: float, y: float, color: dict | None = None) -> dict | None:
    """把图标库里的某个图标克隆到指定坐标。"""
    source = _ICON_LIBRARY.get(name)
    if source is None:
        return None
    node = copy.deepcopy(source)
    node["id"] = f"sk-ic-{name}-{x:.0f}-{y:.0f}"
    node["name"] = name
    shift(node, x - float(box(node).get("x", x)), y - float(box(node).get("y", y)))
    if color is not None:
        recolor_icon(node, color)
    return node


def set_icon_status(block: dict, found: bool) -> None:
    """把「状态标签」换成找到 / 缺失图标。

    08 与 18 的匹配结果区块原本用「备选」文字标签表达密钥类型，
    但需求已删掉「备选」二字，真正要表达的是
    「这台机器上找没找到这把主密钥」，因此标签位改成绿勾 / 红叉。

    画板里带圆角的节点会被渲染器套上 clipPath；克隆出来的图标 FRAME
    必须清掉圆角，否则整条路径会被 18x18 的裁剪框裁没——而 XML 校验
    与文字数门禁都发现不了，属于静默失败。
    """
    anchor = next((c for c in walk_all(block)
                   if c.get("name") == "状态标签"), None)
    if anchor is None:
        return
    holder = find_parent(block, anchor)
    if holder is None:
        return

    ab = box(anchor)
    icon = clone_icon("circle-check" if found else "circle-alert",
                      float(ab.get("x", 0)),
                      float(ab.get("y", 0)) + (float(ab.get("height", 21)) - 18) / 2,
                      OK_COLOR if found else ALERT_COLOR)
    if icon is None:
        return
    icon["id"] = "sk-status-" + ("ok" if found else "alert")
    icon["cornerRadius"] = 0
    for child in walk_all(icon):
        child["cornerRadius"] = 0
    holder["children"] = [icon if c is anchor else c for c in holder["children"]]


def replace_child(parent: dict, old: dict, new: dict) -> None:
    """用 new 替换 parent 的子节点 old（原地改 children 列表）。"""
    for index, child in enumerate(parent.get("children", []) or []):
        if child is old:
            parent["children"][index] = new
            return


# 桌面端双栏工作区里，子节点右边缘距父级右边缘不足这个数即视为右对齐元素。
RIGHT_EDGE_SLACK = 24.0


def _h_align(node: dict) -> str:
    """返回节点的水平对齐方式（MIN / CENTER / MAX）。

    auto-layout 的两个对齐属性是按「主轴 / 交叉轴」命名的，与方向无关：
    HORIZONTAL 布局的主轴是水平，VERTICAL 布局的主轴是竖直。
    直接拿 counterAxisAlignItems 当水平对齐用，会把竖排容器里
    「整体水平居中」的子节点误判成左对齐。
    """
    align = (node.get("counterAxisAlignItems") if node.get("layoutMode") == "VERTICAL"
             else node.get("primaryAxisAlignItems")) or "MIN"
    return align


def _widen(node: dict, delta: float) -> None:
    """把一个容器子树从旧宽撑到新宽。

    Figma 用 auto-layout 表达「撑满」「居中」「贴右」，渲染器只读绝对坐标，
    所以这些都得在这里按轴补算出来：

    - 居中：子节点宽度不变，x 重算到父箱中心；
    - 贴右：子节点右边缘重新对齐父箱右边缘；
    - 左对齐：FILL 子节点跟着变长；原本贴父箱右边缘的元素
      （eye-off、右侧状态标签）额外右移 delta，否则卡片变宽后它们会停在中间。

    判定「贴右」必须用**改宽前**的父箱右边缘：调用方已把本层宽度改大，
    拿新宽度去比会把所有子节点都判成「没贴右」而漏掉位移。

    递归时只有「自己也变宽了」的子节点才继续下传 delta，
    否则它的宽度未变，再传就是无意义的坐标漂移。
    """
    parent = box(node)
    parent_left = float(parent.get("x", 0))
    parent_width = float(parent.get("width", 0))
    parent_right = parent_left + parent_width
    align = _h_align(node)

    for child in node.get("children", []) or []:
        cb = box(child)
        if not cb:
            continue
        child_w = float(cb.get("width", 0))
        fills = (child.get("layoutSizingHorizontal") == "FILL"
                 or float(child.get("layoutGrow") or 0) > 0)
        child_right = float(cb.get("x", 0)) + child_w

        if align == "CENTER":
            cb["x"] = parent_left + parent_width / 2 - child_w / 2
        elif align == "MAX":
            cb["x"] = parent_right - child_w
        elif fills:
            cb["width"] = child_w + delta
        elif child_right >= parent_right - delta - RIGHT_EDGE_SLACK:
            shift(child, delta, 0)

        _widen(child, delta if (fills and align == "MIN") else 0.0)


def widen_workspace(root: dict, area_name: str, pad: float = 24.0) -> None:
    """删掉双栏工作区的右栏，并把主卡片拉到通栏。

    桌面端双栏固定是「主卡片 580 + gap 24 + 右栏 324」，总宽 928。
    删掉右栏后主卡片必须补上这 348，否则页面右侧会空掉一大块。

    主卡片永远是工作区的第一个子节点，右栏在其后——所以按位置删除，
    不能按 name：03 生成主密钥的两个栏都叫「内容卡片」。
    """
    area = find(root, area_name)
    if area is None:
        return
    children = area.get("children", []) or []
    if len(children) < 2:
        return

    area["children"] = children[:1]
    card = children[0]
    area_w = float(box(area).get("width", 0))
    card_w = float(box(card).get("width", 0))
    if area_w <= card_w:
        return

    box(card)["width"] = area_w
    card["layoutSizingHorizontal"] = "FILL"
    _widen(card, area_w - card_w)


# ---------------------------------------------------------------------------
# 布局重排：移动端画板没有 auto-layout 兜底，删块 / 插块后必须显式重算
# ---------------------------------------------------------------------------
def place(parent: dict, child: dict, x: float | None = None,
          y: float | None = None, w: float | None = None,
          h: float | None = None) -> None:
    """给子节点写绝对坐标（只写传入的字段，其余保持原值）。"""
    cb = box(child)
    if x is not None:
        cb["x"] = x
    if y is not None:
        cb["y"] = y
    if w is not None:
        cb["width"] = w
    if h is not None:
        cb["height"] = h


def _child_stretch_needed(node: dict) -> bool:
    """判断容器是否真的需要重算子节点宽度。

    判据：所有 STRETCH 子节点原本的宽度都一致（说明它们本就被设计为等宽撑满），
    但重排后按容器宽度推算的值与之相差超过 1px。差得多才认为是容器被改宽过。
    """
    children = node.get("children") or []
    stretched = [box(c) for c in children if c.get("layoutAlign") == "STRETCH"]
    if not stretched:
        return False
    widths = {round(float(b.get("width", 0)), 2) for b in stretched}
    if len(widths) != 1:
        return False
    target = widths.pop()
    pad_l = float(node.get("paddingLeft") or 0)
    pad_r = float(node.get("paddingRight") or 0)
    pad_t = float(node.get("paddingTop") or 0)
    pad_b = float(node.get("paddingBottom") or 0)
    nb = box(node)
    vertical = node.get("layoutMode") == "VERTICAL"
    span = (float(nb.get("width", 0)) - pad_l - pad_r if vertical
            else float(nb.get("height", 0)) - pad_t - pad_b)
    return abs(span - target) > 1.0


def relayout(node: dict) -> None:
    """按 auto-layout 规则重排子节点坐标。

    Figma REST 返回的 absoluteBoundingBox 是导出时算好的快照。补丁改了某个
    子节点的尺寸（加宽按钮、插入新块、删掉右栏）之后，它后面的兄弟节点坐标
    不会自动重算——渲染出来就是文字压框、按钮飞出容器。

    这里按 layoutMode / itemSpacing / padding / primaryAxisAlignItems /
    counterAxisAlignItems / layoutGrow 复刻一遍 Figma 的排版算法，
    让补丁后的坐标自洽。垂直与水平两个方向都处理。

    只重排坐标，不改尺寸：尺寸由各 patch 显式给出，重排只负责把兄弟节点
    摆到正确的位置上。
    """
    children = node.get("children") or []
    if not children:
        return
    mode = node.get("layoutMode")
    if not mode:
        for child in children:
            relayout(child)
        return

    nb = box(node)
    pad_l = float(node.get("paddingLeft") or 0)
    pad_r = float(node.get("paddingRight") or 0)
    pad_t = float(node.get("paddingTop") or 0)
    pad_b = float(node.get("paddingBottom") or 0)
    gap = float(node.get("itemSpacing") or 0)
    vertical = mode == "VERTICAL"
    inner_x = float(nb.get("x", 0)) + pad_l
    inner_y = float(nb.get("y", 0)) + pad_t
    inner_w = max(float(nb.get("width", 0)) - pad_l - pad_r, 0.0)
    inner_h = max(float(nb.get("height", 0)) - pad_t - pad_b, 0.0)

    # 交叉轴 STRETCH：只在容器本身被 patch 显式加宽过时才重算子节点宽度，
    # 否则原始设计里 padding 推算出来的细微差值会被放大成整块溢出。
    for child in children:
        cb = box(child)
        if child.get("layoutAlign") == "STRETCH":
            if vertical:
                cb["width"] = inner_w
            else:
                cb["height"] = inner_h

    # 主轴上「吃掉剩余空间」只有一种判定：layoutGrow == 1。
    # layoutSizing 的 FILL 是**交叉轴**撑满，由交叉轴对齐负责，
    # 不能混进主轴剩余空间的分配——混了会把该子节点的宽度算成 0
    # （07 新增机密信息的选择器就是这样被压扁的）。
    #
    # HUG 轴（layoutSizingVertical=HUG 且 VERTICAL）表示高度由内容决定，
    # 这时即使 grow 也不该按 share 定高，否则容器一进 relayout 就被压成 0。
    hug_axis = "layoutSizingVertical" if vertical else "layoutSizingHorizontal"
    grown = [c for c in children
             if c.get("layoutGrow") == 1.0 and c.get(hug_axis) != "HUG"]
    fixed = [c for c in children if c not in grown]
    free = (inner_w if not vertical else inner_h) - gap * (len(children) - 1)
    used = sum((float(box(c).get("width", 0)) if not vertical
                else float(box(c).get("height", 0))) for c in fixed)
    share = max((free - used) / len(grown), 0.0) if grown else 0.0
    if len(grown) == 1 and free - used > 0:
        # 只有一个 grow 成员时，它独占全部剩余空间
        cb = box(grown[0])
        if vertical:
            cb["height"] = share
        else:
            cb["width"] = share

    # ABSOLUTE 子节点脱离 auto-layout 流：既不占主轴位置也不被对齐属性搬动，
    # 贴右元素必须标成 ABSOLUTE，否则 SPACE_BETWEEN 会把它当流内成员一起排。
    # 但它自身的子树坐标仍是相对父容器的，必须整体平移到新位置。
    absolutes = [c for c in children if c.get("layoutPositioning") == "ABSOLUTE"]
    for absolute in absolutes:
        # ABSOLUTE 只是让父容器不参与排布，子树坐标本就是画布绝对坐标，
        # 这里不需要再平移——重复位移会把贴右元素甩到画板外。
        # 但绝对定位块整体挪过之后，图标素材里遗留的 Vector 旧坐标会露出来，
        # 所以递归完还要把 Vector 拉回 FRAME 原点。
        relayout(absolute)
        align_icon_vector(absolute)
    children = [c for c in children if c.get("layoutPositioning") != "ABSOLUTE"]
    if not children:
        return

    cursor = inner_y if vertical else inner_x
    span = inner_h if vertical else inner_w
    sizes = [float(box(c).get("height", 0)) if vertical
             else float(box(c).get("width", 0)) for c in children]
    consumed = sum(sizes) + gap * (len(children) - 1)
    align = node.get("primaryAxisAlignItems") or "MIN"
    if align == "CENTER":
        cursor += max((span - consumed) / 2, 0.0)
    elif align == "MAX":
        cursor += max(span - consumed, 0.0)
    elif align == "SPACE_BETWEEN" and len(children) > 1:
        # Figma 的 SPACE_BETWEEN 把剩余空间均分到**相邻两项之间**，
        # 不是把游标一次推到末尾。缺了这一支，最后一个子节点会被甩到容器外。
        slack = max(span - consumed, 0.0) / (len(children) - 1)
        gap += slack

    cross_align = node.get("counterAxisAlignItems") or "MIN"
    for child in children:
        cb = box(child)
        if vertical:
            cb["x"] = inner_x
            cb["y"] = cursor
            cursor += float(cb.get("height", 0)) + gap
        else:
            cb["y"] = inner_y
            cb["x"] = cursor
            cursor += float(cb.get("width", 0)) + gap
        cross = inner_w if vertical else inner_h
        size = float(cb.get("width", 0)) if vertical else float(cb.get("height", 0))
        child_align = child.get("layoutAlign") or child.get("counterAxisAlignSelf") or "MIN"
        if child_align == "STRETCH":
            pass
        elif cross_align == "CENTER":
            if vertical:
                cb["x"] = inner_x + (inner_w - size) / 2
            else:
                cb["y"] = inner_y + (inner_h - size) / 2
        elif cross_align == "MAX":
            if vertical:
                cb["x"] = inner_x + inner_w - size
            else:
                cb["y"] = inner_y + inner_h - size
        elif child_align == "CENTER":
            if vertical:
                cb["x"] = inner_x + (inner_w - size) / 2
            else:
                cb["y"] = inner_y + (inner_h - size) / 2
        elif child_align == "MAX":
            if vertical:
                cb["x"] = inner_x + inner_w - size
            else:
                cb["y"] = inner_y + inner_h - size
        relayout(child)


def reflow_mobile(root: dict, bottom: float, tail: tuple[str, ...]) -> None:
    """把移动端画板的底部固定块下移到新内容底部之下。

    移动端画板自上而下固定为：系统状态栏 / 页面导航栏 / 页面内容 /
    底部导航 / 系统手势区域。后三块贴着画板底边，页面内容一变高就得跟着挪。
    """
    content = find(root, "页面内容")
    if content is None:
        return
    cb = box(content)
    content_h = float(cb.get("height", 0))
    content_bottom = float(cb.get("y", 0)) + content_h

    for name in tail:
        node = find(root, name)
        if node is None:
            continue
        nb = box(node)
        nb["y"] = bottom - float(nb.get("height", 0))
    cb["height"] = max(content_h, bottom - float(cb.get("y", 0)))


# ---------------------------------------------------------------------------
# 全局文案
# ---------------------------------------------------------------------------
_BEIXUAN_REWRITES = (
    ("备选主密钥", "主密钥"),
    ("当前默认主密钥", "当前主密钥"),
    ("默认主密钥", "主密钥"),
    ("备选密钥", "该密钥"),
    ("选择备选密钥需密码", "选择该密钥需密码"),
    ("本地默认主密钥", "本地主密钥"),
    ("备选", ""),
)


def rewrite_beixuan(root: dict) -> None:
    """全局删掉「备选」二字。

    所有主密钥都是主密钥，不存在「主密钥 / 备选主密钥」之分，
    因此设计上不该再出现「备选」。替换表按长度降序排，避免「备选主密钥」
    先被短词吃掉一半。
    """
    for node in walk_all(root):
        if node.get("type") != "TEXT":
            continue
        chars = node.get("characters", "")
        if "备选" not in chars:
            continue
        for old, new in _BEIXUAN_REWRITES:
            chars = chars.replace(old, new)
        set_text(node, chars)


def strip_footer(root: dict) -> None:
    """删掉「● 完全离线 v0.0.1」与「本地安全保存…」两处页脚。

    页脚只是在重复「完全离线」这一实现选择，对用户没有操作价值；
    锁定状态由顶栏「立即锁定」按钮与导航高亮表达。

    两处页脚嵌在「导航底部」「主内容」里而不是它们的直属子节点，
    所以要递归删，不能只删一层。
    """
    prune(root, "本地安全状态")
    prune(root, "离线状态")

    bottom = find(root, "导航底部")
    if bottom is None:
        return
    bb = box(bottom)
    bb["height"] = 40.0
    bb["y"] = float(bb.get("y", 0)) + 29.0


def prune(node: dict, name: str) -> bool:
    """递归删除所有名为 name 的后代节点，返回是否删掉了东西。"""
    children = node.get("children") or []
    kept = [c for c in children if c is not None and c.get("name") != name]
    removed = len(kept) != len(children)
    for child in kept:
        if prune(child, name):
            removed = True
    node["children"] = kept
    return removed


def align_icon_vector(icon: dict) -> None:
    """把图标 FRAME 内的 Vector 拉回 FRAME 的局部原点。

    Figma 导出的图标里，Vector 的 absoluteBoundingBox 有时是素材里
    遗留的旧坐标，与外层 FRAME 不同步（例如 file-output 的 Vector 比
    FRAME 偏左 37px）。渲染器按 Vector 自己的绝对坐标绘制，
    于是图标会跑到按钮文字上。图标 path 是 0..18 的局部坐标，
    把 Vector 的原点挪到 FRAME 左上角即可对齐。
    """
    fb = box(icon)
    fx, fy = float(fb.get("x", 0)), float(fb.get("y", 0))
    fw, fh = float(fb.get("width", 0)), float(fb.get("height", 0))
    for vector in walk_all(icon):
        if vector.get("type") != "VECTOR":
            continue
        vb = box(vector)
        shift(vector, fx - float(vb.get("x", fx)), fy - float(vb.get("y", fy)))


# 图标内 Vector 偏离图标框这个阀值。有意内缩的图标（如 ios-signal）
# 偏离不超过它，不动；搬运与加宽造成的大幅偏离才拉回来。
STALE_ICON_SLACK = 8.0


def center_button_labels(root: dict) -> None:
    """把「按钮文字」回到按钮框正中。

    手搭坐标的按钮在 relayout 后可能被推到框外，被 clipPath 静默剪掉。
    这里不论按钮是否带 auto-layout，统一按字号真实宽度居中；
    带图标的按钮则按「图标 + 间距 + 文字」整组居中。
    """
    for button in walk_all(root):
        if button.get("name") != "操作按钮":
            continue
        label = next((n for n in (button.get("children", []) or [])
                      if n.get("name") == "按钮文字"), None)
        if label is None:
            continue
        bb = box(button)
        lb = box(label)
        if not bb or not lb:
            continue
        chars = str(label.get("characters", ""))
        size = float((label.get("style") or {}).get("fontSize", 14))
        text_w = len(chars) * size

        icon = next((c for c in (button.get("children", []) or [])
                     if c.get("type") == "FRAME"
                     and find(c, "Vector") is not None), None)
        icon_w = float(box(icon).get("width", 0)) if icon else 0.0
        gap = 10.0 if icon else 0.0
        content_w = icon_w + gap + text_w
        if content_w <= 0 or content_w > float(bb.get("width", 0)):
            # 文字比按钮还宽——这是布局错误，不能静默修饰
            continue

        left = float(bb.get("x", 0)) + (float(bb.get("width", 0)) - content_w) / 2
        lb["x"] = left + icon_w + gap
        lb["width"] = text_w
        lb["y"] = float(bb.get("y", 0)) + (float(bb.get("height", 44))
                                            - float(lb.get("height", 0))) / 2
        if icon is not None:
            ib = box(icon)
            ib["x"] = left
            ib["y"] = float(bb.get("y", 0)) + (float(bb.get("height", 44))
                                                - float(ib.get("height", 18))) / 2
            align_icon_vector(icon)


def align_stale_icons(root: dict) -> None:
    """底层统一图标对齐。

    若干补丁直接改写图标框的绝对坐标（如 set_button_label 重排按钮），
    框内的 Vector 不会跟着走，图标就会飞到按钮文字上。
    这里做一遍全树归一化作为底网；渲染器只读绝对坐标，
    所以这一步不会影响布局。
    """
    for node in walk_all(root):
        if node.get("type") != "FRAME":
            continue
        fb = box(node)
        if not fb:
            continue
        fx, fy = float(fb.get("x", 0)), float(fb.get("y", 0))
        for vector in node.get("children", []) or []:
            if vector.get("type") != "VECTOR":
                continue
            vb = box(vector)
            dx = float(vb.get("x", fx)) - fx
            dy = float(vb.get("y", fy)) - fy
            if abs(dx) > STALE_ICON_SLACK or abs(dy) > STALE_ICON_SLACK:
                shift(vector, -dx, -dy)


def set_button_label(root: dict, container: str | None, old: str, new: str,
                     min_width: float | None = None) -> bool:
    """改按钮文案；必要时加宽按钮并把「图标 + 文字」整体重新居中。

    按钮宽度在设计稿里是 HUG（跟随文字），但 absoluteBoundingBox 是定值。
    文案变长后必须手动加宽，否则文字会溢出边框。

    加宽后不能只把文字挪半个差值：带图标的主按钮（如「导出主密钥」）里，
    图标与文字是一组，只挪文字会让两者叠在一起。这里按
    「图标宽 + 间距 + 新文字宽」算出内容总宽，再整体居中到新按钮里。
    """
    if container is None:
        buttons = [n for n in walk_all(root)
                   if n.get("name") == "操作按钮"]
    else:
        holder = find(root, container)
        if holder is None:
            return False
        buttons = [c for c in holder.get("children", []) or []
                   if c.get("name") == "操作按钮"]
    target = next((b for b in buttons
                   if (find(b, "按钮文字") or {}).get("characters") == old), None)
    if target is None:
        return False

    holder = find_parent(root, target)
    if holder is None:
        holder = {"children": buttons, "absoluteBoundingBox": dict(box(root))}

    label = find(target, "按钮文字")
    if label is None:
        return False

    tb = box(target)
    style = label.get("style") or {}
    size = float(style.get("fontSize", 13))
    text_w = len(new) * size

    icon = next((c for c in target.get("children", []) or []
                 if c.get("type") == "FRAME" and find(c, "Vector") is not None), None)
    icon_w = float(box(icon).get("width", 0)) if icon else 0.0
    gap = 10.0 if icon else 0.0
    pad = 20.0

    content_w = icon_w + gap + text_w
    new_w = max(float(tb.get("width", 0)),
                min_width or 0.0,
                content_w + pad)

    old_left = float(tb.get("x", 0))
    extra = new_w - float(tb.get("width", 0))
    tb["width"] = new_w

    # 同行后续按钮一并右移，间距才不变
    siblings = holder.get("children", []) or []
    if target in siblings:
        for sibling in siblings[siblings.index(target) + 1:]:
            shift(sibling, extra, 0)

    # 内容整体居中
    # 内容整体居中：图标在最左，文字紧随其后，两者之间留 gap。
    content_x = old_left + (new_w - content_w) / 2
    button_y = float(tb.get("y", 0))
    button_h = float(tb.get("height", 44))
    if icon is not None:
        ib = box(icon)
        ib["x"] = content_x
        ib["y"] = button_y + (button_h - float(ib.get("height", 18))) / 2
        # 带图标的按钮加宽后，图标内部的 Vector 必须重新对齐到图标框：
        # Figma 导出的 Vector 是素材里的旧坐标，不跟随图标框走。
        align_icon_vector(icon)
    lb = box(label)
    lb["x"] = content_x + icon_w + gap
    lb["width"] = text_w
    lb["y"] = button_y + (button_h - float(lb.get("height", 17))) / 2

    hb = box(holder)
    hb["width"] = float(hb.get("width", 0)) + extra

    set_text(label, new)
    return True


def rename_text(root: dict, container: str, node_name: str, old: str, new: str) -> bool:
    """把容器内某个 TEXT 节点的文案从 old 改成 new。"""
    holder = find(root, container)
    if holder is None:
        return False
    for node in walk_all(holder):
        if node.get("type") == "TEXT" and node.get("name") == node_name \
                and node.get("characters") == old:
            set_text(node, new)
            return True
    return False


# ---------------------------------------------------------------------------
# 桌面端逐板
# ---------------------------------------------------------------------------
def patch_desktop_common(root: dict, active: str) -> None:
    """桌面九张画板共用的改动：侧栏三项菜单、删页脚、删「备选」。"""
    patch_navigation(root, active)
    strip_footer(root)
    rewrite_beixuan(root)


def _selector_text(board: dict, node_name: str) -> str:
    holder = find(board, "主密钥选择")
    if holder is None:
        return ""
    return next((n.get("characters", "") for n in walk_all(holder)
                 if n.get("type") == "TEXT" and n.get("name") == node_name), "")


def _fit_width(selector: dict, holder: dict, delta: float) -> None:
    """把克隆来的选择器收进 / 撑到宿主容器该有的宽度。

    克隆块带着源画板的绝对坐标，而两个宿主的可用宽度不同
    （解锁页 302、卡片 350），不校正就会溢出画板。
    """
    if not delta:
        return
    hb = box(holder)
    pad_l = float(holder.get("paddingLeft") or 0)
    pad_r = float(holder.get("paddingRight") or 0)
    target = max(float(hb.get("width", 0)) - pad_l - pad_r, 0.0)
    sb = box(selector)
    sb["width"] = target
    shift(selector, float(hb.get("x", 0)) + pad_l - float(sb.get("x", 0)), 0)
    # 克隆块整棵子树的坐标都带着源画布的绝对偏移，改宽后必须按 auto-layout
    # 重排，否则状态标签与箭头会停在源画布的位置上（可差上千像素）。
    relayout(selector)


def clone_key_selector(source_board: dict, label_text: str, key_name: str,
                       mkid: str) -> dict | None:
    """克隆**同端**原生画板里的「主密钥选择」区块。

    桌面端与移动端界面形态本就不同（桌面 880 宽双行文字，移动端窄屏更紧凑），
    选择器不能跨端复制。桌面端从 07 新增机密信息克隆，移动端从移动端自己的
    画板克隆——调用方传入对应端的源画板，函数内部不区分端。
    """
    holder = find(source_board, "主密钥选择")
    if holder is None:
        return None
    selector = clone_frame(holder, "主密钥选择")
    # 源画板是「下拉已展开」状态，解锁页要的是收起态：
    # 只保留标签 + 当前选中行，删掉整个可选列表，容器高度随之收回。
    drop(selector, "可选主密钥")
    row = find(selector, "当前选择")
    label = find(selector, "字段标签")
    if row is not None and label is not None:
        # 删掉列表后容器高度要**收回去**：grow_to_fit 只增不减，
        # 这里按剩余两个子节点直接定高。
        top = float(box(label).get("y", 0))
        bottom = float(box(row).get("y", 0)) + float(box(row).get("height", 0))
        box(selector)["height"] = bottom - top
        gap = float(selector.get("itemSpacing") or 0)
        for child in selector["children"]:
            if float(box(child).get("y", 0)) > bottom:
                shift(child, 0, bottom + gap - float(box(child).get("y", 0)))
    for node_name, value in (("字段标签", label_text), ("名称", key_name),
                             ("主密钥 ID", mkid)):
        for node in walk_all(selector):
            if node.get("type") == "TEXT" and node.get("name") == node_name:
                set_text(node, value)
                break
    return selector


def patch_unlock_picker(root: dict) -> None:
    """01 解锁与锁定：默认主密钥改为可选主密钥。

    顺带删掉页脚的「基础版 / v0.0.1 / 文件备份」与锁定说明——
    版本号在安全设置页已经能查到，锁定说明讲的是隐藏明文与密钥
    轮换这些实现细节，不该出现在用户面前。
    """
    patch_desktop_common(root, "主密钥管理")
    patch_unlock_lock_note(root)
    for node in walk_all(root):
        if node.get("type") != "TEXT":
            continue
        if node.get("name") == "版本信息":
            set_text(node, "v0.0.1")

    for node in walk_all(root):
        if node.get("type") != "TEXT":
            continue
        chars = node.get("characters", "")
        if chars == "输入默认主密钥密码，解锁本机的密匣。":
            set_text(node, "选择主密钥，输入主密钥密码解锁。")
        elif chars == "默认主密钥密码":
            set_text(node, "主密钥密码")

    old = find(root, "默认主密钥")
    if old is not None:
        holder = find_parent(root, old)
        selector = clone_key_selector(_CANVAS["3:27146"], "主密钥",
                                      "工作密钥", "MK-7A21")
        if holder is not None and selector is not None:
            ob, sb = box(old), box(selector)
            shift(selector, float(ob.get("x", 0)) - float(sb.get("x", 0)),
                  float(ob.get("y", 0)) - float(sb.get("y", 0)))
            _widen(selector, float(ob.get("width", 0)) - float(sb.get("width", 0)))
            replace_child(holder, old, selector)


def patch_master_key_page(root: dict) -> None:
    """02 主密钥管理与详情：「导出备份」改为「导出主密钥」。

    详情卡的说明文字（33 字）在 476 宽的框里放不下，会被 clipsContent
    裁掉尾巴。这里把两栏宽度按 380 + 24 + 524 重新分配成 420 + 24 + 484，
    让最长的一行说明能完整显示。
    """
    patch_desktop_common(root, "主密钥管理")

    area = find(root, "主密钥工作区")
    if area is None:
        return
    children = area.get("children", []) or []
    listing = next((c for c in children if c.get("name") == "主密钥列表"), None)
    # 「内容卡片」这名字在工作区里只有右栏那一个（列表内部的卡片嵌在
    # 主密钥列表里，不是工作区的直属子节点），按直属子节点取即可。
    detail = next((c for c in children if c.get("name") == "内容卡片"), None)
    if listing is None or detail is None:
        return

    area_w = float(box(area).get("width", 0))
    # 详情卡要能放下 33 字的最长说明（约 430px）再加内边距，故给到 520。
    detail_w = 520.0
    list_w = area_w - detail_w - 24.0

    lb, db = box(listing), box(detail)
    shift(detail, float(lb.get("x", 0)) + list_w + 24.0 - float(db.get("x", 0)), 0)
    lb["width"] = list_w
    db["width"] = detail_w
    _widen(listing, list_w - 380.0)
    _widen(detail, detail_w - 524.0)
    # 「详情操作」本身没有 auto-layout，_widen 不会把宽度传下去，
    # 这里手动对齐到卡片内边距，保证按钮不会溢出。
    actions = find(detail, "详情操作")
    if actions is not None:
        ab = box(actions)
        ab["x"] = float(db.get("x", 0)) + 24.0
        ab["width"] = detail_w - 48.0

    # 这句 33 字说明在 472 宽内放不下（会被 clipsContent 裁掉尾巴），
    # 删掉「密码丢失将无法解密」——安全设置页已有同样的提醒。
    for node in walk_all(detail):
        if node.get("type") == "TEXT" and node.get("characters", "").startswith(
                "随机生成的主密钥，由主密钥密码派生的 KEK"):
            set_text(node, "随机生成的主密钥，由主密钥密码派生的 KEK 加密后保存在本地。")

    # 按钮文案最后再改：详情卡加宽会挪动「详情操作」的位置，
    # 先改文案再挪位置会让图标与文字的相对关系错位。
    set_button_label(root, "详情操作", "导出备份", "导出主密钥", min_width=132.0)


def patch_generate_page(root: dict) -> None:
    """03 生成主密钥：删右栏通栏，按钮改「保存主密钥」。"""
    patch_desktop_common(root, "主密钥管理")
    widen_workspace(root, "生成工作区")
    set_button_label(root, "生成操作", "生成并保存", "保存主密钥", min_width=132.0)


def patch_import_page(root: dict) -> None:
    """04 导入主密钥：密码框对齐 05 样式、按钮改名、删右栏。

    顺序不能乱：先删右栏并把卡片拉到 928 通栏，再用卡片的实际宽度
    重排子节点。反过来执行的话，当时按 532 算出来的密码框会被
    _widen 再次压缩回半宽。
    """
    patch_desktop_common(root, "主密钥管理")
    widen_workspace(root, "导入工作区")
    _align_pwd_fields(root)
    set_button_label(root, "导入操作", "导入为备选主密钥", "导入主密钥", min_width=132.0)


def patch_export_page(root: dict) -> None:
    """05 主密钥文件导出：补主密钥原密码框、删右栏、提示移到按钮上方、按钮改名。"""
    patch_desktop_common(root, "主密钥管理")
    patch_export_original_password(root)
    widen_workspace(root, "导出工作区")
    # 归位必须放在 widen_workspace 之后：新插入的原密码字段是在加宽前
    # 按 532 宽算的辅助行坐标，卡片通栏后它不会自动跟上。
    for field in walk_all(find(root, "导出工作区") or {}):
        if field.get("name") == "表单字段":
            _fix_field_hint_width(field)
    _move_export_tip(root)
    set_button_label(root, "导出操作", "导出加密主密钥", "导出主密钥", min_width=132.0)


def _move_export_tip(root: dict) -> None:
    """05：在主栏「导出主密钥」按钮上方补绿色提示。

    需求要求把原本在右栏的「导出不会删除原内容」提示移到按钮上方，
    但右栏整栏被删后提示也跟着消失。此时卡片已经没有这个节点了，
    所以直接按 06 已确认的提示块样式新建一个。

    提示块独立成块放在按钮行上方，不塞进按钮行：按钮行是 SPACE_BETWEEN
    布局，多一个成员就会把按钮推到容器之外。
    """
    card = find(find(root, "导出工作区"), "内容卡片")
    if card is None:
        return
    actions = next((c for c in (card.get("children", []) or [])
                    if c.get("name") == "导出操作"), None)
    if actions is None:
        return

    cb = box(card)
    tip_w = float(cb.get("width", 0)) - 48.0
    tip_x = float(cb.get("x", 0)) + 24.0
    ab = box(actions)

    tip = {
        # 卡片里本来就有一条黄色提示（保护密码丢失后无法导入），
        # 新增的绿色提示与其各占一行，不能因为名称重名就跳过。
        "id": "sk-export-tip",
        "name": "安全提示", "type": "FRAME",
        "fills": _solid({"r": 0.902, "g": 0.965, "b": 0.945, "a": 1.0}),
        "cornerRadius": 8.0,
        "layoutMode": "HORIZONTAL", "itemSpacing": 12.0,
        "counterAxisAlignItems": "CENTER",
        "paddingLeft": 16.0, "paddingRight": 16.0,
        "paddingTop": 14.0, "paddingBottom": 14.0,
        "absoluteBoundingBox": {"x": tip_x, "y": 0.0, "width": tip_w, "height": 50.0},
        "children": [],
    }
    icon = clone_icon("shield-check", tip_x + 16.0, 0.0, OK_COLOR)
    if icon is not None:
        icon["id"] = "sk-export-tip-icon"
        icon["cornerRadius"] = 0
        for child in walk_all(icon):
            child["cornerRadius"] = 0
        tip["children"].append(icon)
    tx = tip_x + 16.0 + (26.0 if icon is not None else 0.0)
    tip["children"].append(text_node(
        "提示内容", "导出不会删除本机的主密钥。",
        tx, 0.0, 13, INK, width=tip_w - (tx - tip_x) - 16.0))
    grow_to_fit(tip, pad=0.0, min_h=50.0)

    tb = box(tip)
    tb["y"] = float(ab.get("y", 0)) - float(tb.get("height", 50.0)) - 12.0
    for icon_node in walk_all(tip):
        if icon_node.get("name") == "shield-check":
            ib = box(icon_node)
            ib["y"] = float(tb.get("y", 0)) + (float(tb.get("height", 50.0))
                                        - float(ib.get("height", 18))) / 2
            align_icon_vector(icon_node)
    for text in tip.get("children", []) or []:
        if text.get("type") == "TEXT":
            box(text)["y"] = float(tb.get("y", 0)) + (float(tb.get("height", 50.0))
                                       - float(box(text).get("height", 18))) / 2

    children = card.get("children", []) or []
    card["children"] = [tip if c is actions else c for c in children]
    grow_to_fit(card, pad=0.0)


def _make_pwd_field(x: float, y: float, w: float, label: str, hint: str) -> dict:
    """构造一个与 05 导出页完全同款的密码字段。

    04 原来的密码框是手搭的：输入区没有描边、没有 eye-off 图标、
    输入内容贴着边框，跟界面上其它密码框不是一个样式。这里直接按
    05 的结构（标签 / 带描边的输入区 / 掩码 + eye-off / 辅助说明）重建，
    保证两个页面的密码框看起来是同一套控件。
    """
    label_h = 21.0
    input_h = 46.0
    hint_h = 19.0
    total = label_h + 10.0 + input_h + 8.0 + hint_h

    field = rect_node("表单字段", x, y, w, total, None, 0.0)
    field["layoutMode"] = "VERTICAL"
    field["itemSpacing"] = 10.0
    field["children"] = [
        text_node("字段标签", label, x, y, 13.0, INK, width=w),
    ]

    input_box = rect_node("输入区域", x, y + label_h + 10.0, w, input_h,
                          WHITE, 8.0, stroke=BORDER)
    input_box["layoutMode"] = "HORIZONTAL"
    input_box["counterAxisAlignItems"] = "CENTER"
    input_box["paddingLeft"] = 14.0
    input_box["paddingRight"] = 14.0
    inner_w = w - 14.0 * 2
    masked = text_node("输入内容", "\u2022" * 11, x + 14.0, y + label_h + 22.0,
                       15.0, INK, width=inner_w - 26.0)
    eye = clone_icon("eye-off", x + w - 14.0 - 18.0, y + label_h + 24.0, MUTED)
    children = [masked]
    if eye is not None:
        eye["cornerRadius"] = 0
        for sub in walk_all(eye):
            sub["cornerRadius"] = 0
        children.append(eye)
    input_box["children"] = children

    hint_y = y + label_h + 10.0 + input_h + 8.0
    field["children"].append(input_box)
    field["children"].append(text_node("字段提示", hint, x, hint_y, 12.0, MUTED, width=w))
    return field


def _align_pwd_fields(root: dict) -> None:
    """把 04 重排为：文件选择 -> 两个密码框 -> 操作按钮，并通栏。

    build_import_master_key 生成的子节点沿用 05 窄栏（580）的坐标，
    通栏后 x 与宽度都要重算；纵向则统一用一个游标排下去，
    避免「按钮压住最后一个字段」这类重叠。
    """
    card = find(find(root, "导入工作区"), "内容卡片")
    if card is None:
        return

    card_x = float(box(card).get("x", 0)) + 24.0
    card_w = float(box(card).get("width", 0)) - 48.0
    slot = next((c for c in (card.get("children", []) or [])
                 if c.get("name") == "文件选择"), None)
    fields = [c for c in (card.get("children", []) or [])
              if c.get("name") == "表单字段"]
    actions = next((c for c in (card.get("children", []) or [])
                    if c.get("name") == "导入操作"), None)
    if slot is None or len(fields) != 2 or actions is None:
        return

    top = float(box(card).get("y", 0)) + 24.0
    cursor = top

    # 「文件选择」是手搭的绝对定位块，没有 auto-layout，_widen 对它无效，
    # 宽度与右侧按钮要自己算。
    sb = box(slot)
    shift(slot, card_x - float(sb.get("x", 0)), cursor - float(sb.get("y", 0)))
    sb["width"] = card_w
    pick = next((c for c in slot.get("children", []) or []
                 if c.get("name") == "操作按钮"), None)
    if pick is not None:
        # 只加宽文件信息文本，按钮是 HUG 宽度，不能跟着撑开。
        for text_node in slot.get("children", []) or []:
            if text_node.get("type") == "TEXT":
                box(text_node)["width"] = card_w - 200.0
        pb = box(pick)
        shift(pick, card_x + card_w - float(pb.get("x", 0)) - float(pb.get("width", 0)), 0)
    cursor += float(sb.get("height", 0)) + 24.0

    specs = (("文件保护密码", "导出时为这份备份设定的密码。"),
             ("新的主密钥密码", "为本机保存的主密钥新设密码。"))
    for target, (label, hint) in zip(fields, specs):
        field = _make_pwd_field(card_x, cursor, card_w, label, hint)
        replace_child(card, target, field)
        cursor += float(box(field).get("height", 0)) + 20.0

    ab = box(actions)
    shift(actions, card_x - float(ab.get("x", 0)), cursor - float(ab.get("y", 0)))
    ab["width"] = card_w
    cursor += float(ab.get("height", 0)) + 24.0

    grow_to_fit(card, pad=0.0, min_h=cursor - float(box(card).get("y", 0)))


def patch_secret_add_page(root: dict) -> None:
    """07 新增机密信息：删右栏通栏，按钮改「保存」。"""
    patch_desktop_common(root, "机密信息管理")
    widen_workspace(root, "新增工作区")
    set_button_label(root, "保存操作", "加密并保存", "保存", min_width=96.0)


def patch_secret_import_page(root: dict) -> None:
    """08 导入机密信息：删右栏、文案改「关联的主密钥」、绿勾、按钮改名。"""
    patch_desktop_common(root, "机密信息管理")
    widen_workspace(root, "导入工作区")

    block = find(root, "找到的主密钥")
    if block is not None:
        for child in block.get("children", []) or []:
            if child.get("name") == "验证状态":
                for text in child.get("children", []) or []:
                    if text.get("name") == "说明":
                        set_text(text, "匹配的主密钥")
        set_icon_status(block, True)

    set_button_label(root, "导入操作", "验证并导入", "导入", min_width=96.0)


def _align_page_actions(root: dict) -> None:
    """把「页面标题」行右侧的操作按钮组贴到卡片右边缘。

    标题行是 counterAxis=CENTER 的横向布局：左边的标题说明会按 grow 撑开，
    右侧的操作组因此被推到容器之外。设计意图是「标题左、操作右」，
    所以这里把操作组改成绝对定位并贴右。
    """
    header = find(root, "页面标题")
    if header is None:
        return
    actions = next((c for c in (header.get("children") or [])
                    if c.get("name") == "页面操作"), None)
    if actions is None:
        return
    hb = box(header)
    ab = box(actions)
    actions["layoutPositioning"] = "ABSOLUTE"
    ab["x"] = float(hb.get("width", 0)) - float(ab.get("width", 0))
    ab["y"] = 8.5


def patch_secret_page_board(root: dict) -> None:
    """06 机密信息管理：走共用的侧栏 / 页脚 / 文案修改。"""
    patch_desktop_common(root, "机密信息管理")
    patch_secret_page(root)
    _align_page_actions(root)
    # 列表下方的「ID 后的黄色 ? …」说明在 928 宽里放不下，
    # 缩短为一行能完整显示的版本。
    # 详情卡内两条说明在 340 宽里放不下，会被 clipsContent 裁掉尾巴。
    for node in walk_all(root):
        if node.get("type") != "TEXT":
            continue
        chars = node.get("characters", "")
        if chars.startswith("ID 后的黄色"):
            set_text(node, "ID 后的黄色 ? 表示主密钥缺失；名称为空不代表缺失。")
        elif chars.startswith("默认隐藏明文"):
            set_text(node, "默认隐藏明文；离开窗口或切换界面时重新上锁。")
        elif chars.startswith("备主密钥加密的信息"):
            set_text(node, "查看或复制前需验证对应主密钥密码。")


def patch_security_page(root: dict) -> None:
    """09 安全设置：删右栏通栏。"""
    patch_desktop_common(root, "安全设置")
    widen_workspace(root, "设置工作区")


# ---------------------------------------------------------------------------
# 移动端逐板
# ---------------------------------------------------------------------------
def patch_mobile_common(root: dict) -> None:
    """移动端画板共用：删「备选」。"""
    rewrite_beixuan(root)


def patch_mobile_unlock(root: dict) -> None:
    """10 移动端解锁：改为可选主密钥解锁。"""
    patch_mobile_common(root)

    for node in walk_all(root):
        if node.get("type") != "TEXT":
            continue
        chars = node.get("characters", "")
        if chars == "输入默认主密钥密码，解锁本机的密匣":
            set_text(node, "选择主密钥，输入主密钥密码解锁")
        elif chars == "默认主密钥密码":
            set_text(node, "主密钥密码")
    drop_text(root, "明文已隐藏")
    drop_text(root, "内存密钥缓存已清空")

    old = find(root, "默认主密钥")
    if old is not None:
        holder = find_parent(root, old)
        selector = clone_key_selector(_CANVAS["3:28077"], "主密钥",
                                      "工作密钥", "MK-7A21")
        if holder is not None and selector is not None:
            ob, sb = box(old), box(selector)
            shift(selector, float(ob.get("x", 0)) - float(sb.get("x", 0)),
                  float(ob.get("y", 0)) - float(sb.get("y", 0)))
            _fit_width(selector, holder,
                       float(ob.get("width", 0)) - float(sb.get("width", 0)))
            replace_child(holder, old, selector)


def patch_mobile_master_keys(root: dict) -> None:
    """11 移动端主密钥管理。

    三处调整：
    - 卡片内的「导出备份」「导出」是同一操作，统一成「导出主密钥」；
    - 底部「生成 / 导入主密钥」单按钮拆成两枚，移到容量行上方；
    - 「容量已满…」提示改文案并放到两枚按钮下方。
    """
    patch_mobile_common(root)

    for holder_name in ("密钥操作",):
        for holder in [n for n in walk_all(root) if n.get("name") == holder_name]:
            _unify_export_buttons(holder)

    content = find(root, "页面内容")
    if content is not None:
        _split_create_import_buttons(root, content)


def _unify_export_buttons(holder: dict) -> None:
    """把同一卡片里的导出按钮文案统一成「导出主密钥」。

    第一张卡里叫「导出备份」（因为并列的另一个是「删除」），
    第二张卡里叫「导出」。两者都是导出这把主密钥，文案统一即可。
    """
    buttons = [c for c in (holder.get("children", []) or [])
               if c.get("name") == "操作按钮"]
    for button in buttons:
        label = find(button, "按钮文字")
        if label is None:
            continue
        if label.get("characters") in ("导出备份", "导出"):
            set_text(label, "导出主密钥")
            lb = box(label)
            style = label.get("style") or {}
            size = float(style.get("fontSize", 13))
            lb["width"] = len("导出主密钥") * size
            bb = box(button)
            bb["width"] = max(float(bb.get("width", 0)), lb["width"] + 24.0)
            lb["x"] = float(bb.get("x", 0)) + (float(bb.get("width", 0))
                                               - float(lb.get("width", 0))) / 2
            for icon in button.get("children", []) or []:
                if icon.get("type") == "FRAME" and find(icon, "Vector") is not None:
                    ib = box(icon)
                    ib["x"] = float(lb.get("x", 0)) - 26.0
    # 后面的按钮跟着右移
    for index in range(1, len(buttons)):
        prev = box(buttons[index - 1])
        cur = box(buttons[index])
        gap = float(box(holder).get("x", 0)) + float(box(holder).get("width", 0)) \
            - (float(prev.get("x", 0)) + float(prev.get("width", 0)))
        shift(buttons[index], float(prev.get("x", 0)) + float(prev.get("width", 0))
              + gap - float(cur.get("x", 0)), 0)


def _split_create_import_buttons(root: dict, content: dict) -> None:
    """把「生成 / 导入主密钥」拆成两枚按钮并上移到容量行下方。"""
    children = content.get("children", []) or []
    combined = next((c for c in children if c.get("name") == "操作按钮"), None)
    capacity = next((c for c in children if c.get("name") == "容量状态"), None)
    tip = next((c for c in children if c.get("name") == "安全提示"), None)
    if combined is None or capacity is None:
        return

    cb = box(capacity)
    inner_x = float(cb.get("x", 0)) + 20.0
    inner_w = float(cb.get("width", 0)) - 40.0
    gap = 8.0
    btn_w = (inner_w - gap) / 2
    btn_h = 40.0

    buttons = []
    for index, (icon_name, label) in enumerate(
            (("plus", "生成主密钥"), ("file-input", "导入主密钥"))):
        button = rect_node("操作按钮", inner_x + index * (btn_w + gap),
                           float(cb.get("y", 0)) + 28.0, btn_w, btn_h,
                           WHITE, 8.0, stroke=BORDER)
        button["layoutMode"] = "HORIZONTAL"
        button["primaryAxisAlignItems"] = "CENTER"
        button["counterAxisAlignItems"] = "CENTER"
        size = 13.0
        text_w = len(label) * size
        text_x = inner_x + index * (btn_w + gap) + (btn_w - text_w - 18.0 - 6.0) / 2
        text = text_node("按钮文字", label, text_x,
                         float(cb.get("y", 0)) + 28.0 + (btn_h - 17.0) / 2,
                         size, ACCENT, width=text_w)
        children_nodes = []
        icon = clone_icon(icon_name, text_x + text_w + 6.0,
                          float(cb.get("y", 0)) + 28.0 + (btn_h - 18.0) / 2, ACCENT)
        if icon is not None:
            icon["cornerRadius"] = 0
            for sub in walk_all(icon):
                sub["cornerRadius"] = 0
            align_icon_vector(icon)
            children_nodes.append(icon)
        children_nodes.append(text)
        button["children"] = children_nodes
        buttons.append(button)

    # 两枚按钮要并排，页面内容是竖排流，必须包一层横向容器；
    # 直接平铺进竖排流会被排成上下两行。
    row = rect_node("操作按钮组", float(cb.get("x", 0)),
                    float(cb.get("y", 0)) + 28.0,
                    float(cb.get("width", 0)), btn_h, None, 0.0)
    row["layoutMode"] = "HORIZONTAL"
    row["primaryAxisSizingMode"] = "FIXED"
    row["counterAxisSizingMode"] = "FIXED"
    row["itemSpacing"] = gap
    row["fills"] = []
    row["children"] = []
    row_y = float(cb.get("y", 0)) + 28.0
    for button in buttons:
        button["layoutPositioning"] = "ABSOLUTE"
        box(button)["y"] = row_y
        row["children"].append(button)

    content["children"] = [c for c in children if c is not combined]
    insert_at = content["children"].index(capacity) + 1
    content["children"][insert_at:insert_at] = [row]

    if tip is not None:
        note = next((n for n in walk_all(tip)
                     if n.get("type") == "TEXT" and n.get("characters")), None)
        if note is not None:
            set_text(note, "主密钥数量已满，先导出并删除一把主密钥，才能新增。")
            nb = box(note)
            nb["width"] = float(box(tip).get("width", 350)) - 52.0
        tb = box(tip)
        buttons_bottom = max(float(box(b).get("y", 0)) + float(box(b).get("height", 0))
                            for b in buttons)
        shift(tip, 0, buttons_bottom + 16.0 - float(tb.get("y", 0)))

    bottom = max(float(box(c).get("y", 0)) + float(box(c).get("height", 0))
                 for c in content.get("children", []) or [])
    grow_to_fit(content, pad=16.0)
    reflow_mobile(root, bottom + 16.0 + 72.0, ("系统手势区域", "底部导航"))


def strip_tabs(root: dict) -> float:
    """删掉「添加方式」TAB 条，返回回收的高度。"""
    tabs = find(root, "添加方式")
    if tabs is None:
        return 0.0
    holder = find_parent(root, tabs)
    tb = box(tabs)
    freed = float(tb.get("height", 0)) + 16.0
    y = float(tb.get("y", 0))

    if holder is not None:
        shift_tabs(tabs, holder, y)
    drop(root, "添加方式")
    return freed


def shift_tabs(tabs: dict, holder: dict, y: float) -> None:
    """TAB 条被删后，把它下方同级的块整体上移。"""
    hb = box(holder)
    for sibling in holder.get("children", []) or []:
        if sibling is tabs:
            continue
        sb = box(sibling)
        if float(sb.get("y", 0)) >= y:
            shift(sibling, 0, -freed_of(holder, y))


def freed_of(holder: dict, y: float) -> float:
    """TAB 条及其间距的总高度。"""
    return 64.0


def patch_mobile_generate_key(root: dict) -> None:
    """12 移动端生成主密钥：去 TAB，按钮改「生成主密钥」。"""
    patch_mobile_common(root)
    strip_tabs(root)
    set_button_label(root, "生成操作", "生成并保存", "生成主密钥", min_width=132.0)


def patch_mobile_import_key(root: dict) -> None:
    """13 移动端导入主密钥：去 TAB，按钮改「导入主密钥」。"""
    patch_mobile_common(root)
    strip_tabs(root)
    set_button_label(root, None, "导入为备选主密钥", "导入主密钥", min_width=132.0)


def patch_mobile_detail(root: dict, found: bool) -> None:
    """15 / 16 移动端机密信息详情：区块重排 + 主密钥状态图标。"""
    patch_mobile_common(root)
    _relayout_detail(root, found)
    if not found:
        _drop_missing_export(root)


def _tighten(card: dict, pad: float = 20.0) -> None:
    """按实际内容收紧卡片高度。

    grow_to_fit 只增不减，插入行据会把卡片里的块顺序顺得很正常，
    但卡片自身仍是原高度，底部的按钮就会被 clipsContent 静默剪掉。
    """
    cb = box(card)
    if not cb:
        return
    top = float(cb.get("y", 0))
    bottom = top
    for child in card.get("children", []) or []:
        child_b = box(child)
        if child_b:
            bottom = max(bottom, float(child_b.get("y", 0))
                         + float(child_b.get("height", 0)))
    cb["height"] = max(60.0, bottom - top + pad)


def _drop_missing_export(root: dict) -> None:
    """16 缺失态：删掉「导出原加密文件」按钮。

    没有主密钥就无法验证密码，而导出机密信息必须验证密码，
    所以缺失态不应出现这个按钮。此处只在 found=False 时调用。
    """
    content = find(root, "页面内容")
    if content is None:
        return
    for button in list(walk_all(content)):
        if button.get("name") != "操作按钮":
            continue
        label = find(button, "按钮文字")
        if label is None or label.get("characters") != "导出原加密文件":
            continue
        holder = find_parent(content, button)
        if holder is not None and button in (holder.get("children", []) or []):
            holder["children"].remove(button)


def _relayout_detail(root: dict, found: bool) -> None:
    """15 / 16 移动端机密信息详情：两个区块的资信重排。

    需求要求：
    - 第一区块改「关联主密钥」，只留 ID / 名称两行，
      标题行最右侧加图标表示主密钥是否找到；
    - 第二区块改「机密信息」，把第一区块的「标题」行移过来置顶，
      原「机密信息」标签改「机密信息内容」；
    - 缺失态在主密钥名称行下方加黄色提示。

    Figma 里两个块都名「内容卡片」，只能按位置区分；块内的行名是
    「详情字段」，而不是「详情区块 / 密钥区块」——
    旧版本按错名字取块子，所以整个重排一直是空跑。
    """
    content = find(root, "页面内容")
    if content is None:
        return
    blocks = [c for c in (content.get("children", []) or [])
              if c.get("name") == "内容卡片"]
    if len(blocks) < 2:
        return

    key_block, body_block = blocks[0], blocks[1]

    # 第一区块的标题行：「详情标题」里被状态标签（已验证 / 缺失）占位。
    # 标题行没有「区块标题」这个名，就位于区块标题右侧。
    head = next((c for c in (key_block.get("children", []) or [])
                 if c.get("name") == "详情标题"), None)
    if head is not None:
        head_name = next((n for n in walk_all(head)
                          if n.get("name") == "标题"), None)
        if head_name is not None:
            set_text(head_name, "关联主密钥")
        # 图标在标题行最右侧，不是状态标签原位（那里在标题上方）。
        _place_head_icon(head, found)

    attrs = next((c for c in (key_block.get("children", []) or [])
                  if c.get("name") == "详情属性"), None)
    moved: list[dict] = []
    if attrs is not None:
        rows = [c for c in (attrs.get("children", []) or [])
                if c.get("name") == "详情字段"]
        # 【标题】行移给第二区块的「机密信息内容」上方。
        # 行内存在两个 TEXT：左边的字段标签写的就是「标题」，
        # 不能只看行名（行名三行都叫「详情字段」）。
        title_row = next((r for r in rows
                          if any(n.get("name") == "字段标签"
                                 and n.get("characters") == "标题"
                                 for n in walk_all(r))), None)
        if title_row is not None:
            moved.append(title_row)
            attrs["children"].remove(title_row)

    if not found:
        _append_warn(key_block)

    # 第二区块：左侧的「机密信息」标签改文案，
    # 并把移过来的「标题」行放在它上方。
    # 15 的标签在「明文工具栏」里，16 的则是卡片直接子节点，
    # 两种结构都要能匹配到。
    label = next((n for n in walk_all(body_block)
                  if n.get("type") == "TEXT"
                  and n.get("characters") == "机密信息"), None)
    if label is not None:
        set_text(label, "机密信息内容")

    # 「标题」行要在「机密信息内容」标签的上方，而不是嵌进
    # 「明文工具栏」行内（那里右侧还有「隐藏」按钮，会挤掉标签）。
    # 底层容器要与标签同级，否则 relayout 会把它挤到下一行。
    if moved and label is not None:
        holder = find_parent(body_block, label)
        target = find_parent(body_block, holder) if holder is not None else None
        if target is None:
            target = body_block
            holder = label

        # 卡片里多了一行 30px，卡片外的后续块全部下移相同量，
        # 否则安全提示会压在卡片的复制 / 导出按钮上。
        old_bottom = float(box(body_block).get("y", 0)) \
            + float(box(body_block).get("height", 0))
        card_row_h = float(box(holder).get("height", 34))

        for row in moved:
            for parent in (target, holder, body_block):
                if row in (parent.get("children", []) or []):
                    parent["children"].remove(row)
                    break
            target["children"].insert(target["children"].index(holder), row)
            row["layoutSizingHorizontal"] = "FILL"

        grow_to_fit(body_block, pad=0.0)
        TIGHTEN.append(body_block)

        new_bottom = float(box(body_block).get("y", 0)) \
            + float(box(body_block).get("height", 0))
        delta = new_bottom - old_bottom
        if abs(delta) < 0.5:
            delta = card_row_h
        holder_root = find_parent(body_block, body_block) or body_block
        for node in (holder_root.get("children", []) or []):
            if node is body_block:
                continue
            if float(box(node).get("y", 0)) >= old_bottom:
                shift(node, 0, delta)
        grow_to_fit(holder_root, pad=0.0)
        TIGHTEN.append(holder_root)


def _place_head_icon(head: dict, found: bool) -> None:
    """把找到 / 缺失图标放到区块标题行的最右侧。

    set_icon_status 是就地替换「状态标签」，坐标在标题行上方；
    详情页的状态表达在标题行右端，需要单独定位。
    """
    for node in walk_all(head):
        if node.get("name") in ("状态标签", "circle-check", "circle-alert"):
            break
    else:
        return

    icon = clone_icon("circle-check" if found else "circle-alert", 0.0, 0.0,
                      OK_COLOR if found else ALERT_COLOR)
    if icon is None:
        return
    icon["id"] = "sk-head-" + ("ok" if found else "alert")
    icon["cornerRadius"] = 0
    for child in walk_all(icon):
        child["cornerRadius"] = 0

    icon["layoutPositioning"] = "ABSOLUTE"
    hb = box(head)
    ib = box(icon)
    ib["x"] = float(hb.get("x", 0)) + float(hb.get("width", 0)) \
        - float(ib.get("width", 18))
    ib["y"] = float(hb.get("y", 0)) + (float(hb.get("height", 60))
                                        - float(ib.get("height", 18))) / 2

    drop(head, "状态标签")
    head["children"] = [c for c in (head.get("children", []) or [])
                        if c.get("name") not in ("circle-check", "circle-alert")]
    # 图标只占位不宽度，标题文字不要让出它，否则标题会被推到下一行。
    title = next((n for n in walk_all(head) if n.get("name") == "标题"), None)
    if title is not None:
        title["layoutSizingHorizontal"] = "HUG"
        head["itemSpacing"] = 0.0
    head["children"].append(icon)


def _append_warn(key_block: dict) -> None:
    """在缺失态的主密钥区块内追加黄色提示行。

    提示行属于「详情属性」的最后一行之后（主密钥名称行下方），
    不是卡片的直接子节炵。
    """
    holder = next((c for c in (key_block.get("children", []) or [])
                   if c.get("name") == "详情属性"), None)
    if holder is None:
        return
    rows = [c for c in (holder.get("children", []) or [])
            if c.get("name") == "详情字段"]
    if not rows:
        return
    rb = box(rows[-1])
    text = text_node("缺失提示", "主密钥缺失，请先导入主密钥",
                     float(rb.get("x", 0)), float(rb.get("y", 0)) + 32.0,
                     12.0, WARN_COLOR,
                     width=float(box(holder).get("width", 260)))
    holder["children"].append(text)
    grow_to_fit(holder, pad=0.0)
    grow_to_fit(key_block, pad=0.0)


def patch_mobile_secret_add(root: dict) -> None:
    """17 移动端新增机密信息：删开发提示，按钮改「保存」。"""
    patch_mobile_common(root)
    drop(root, "安全提示")
    set_button_label(root, None, "加密并保存", "保存", min_width=96.0)


def patch_mobile_secret_import(root: dict) -> None:
    """18 移动端导入机密信息：标题改「主密钥」、绿勾、按钮改「导入」。

    匹配结果区的容器名是「匹配结果」（它在一张同名为「内容卡片」的
    卡片里），旧版按「已找到的本机主密钥」取节点，永远取不到。
    """
    patch_mobile_common(root)

    block = find(root, "匹配结果")
    if block is not None:
        # 标题在「匹配结果」自身（不在被替换的状态标签里），
        # 先改文案再用绿勾占位。
        title = next((n for n in (block.get("children", []) or [])
                      if n.get("type") == "TEXT"), None)
        if title is not None:
            set_text(title, "主密钥")
        set_icon_status(block, True)
    set_button_label(root, None, "验证并导入", "导入", min_width=96.0)


def patch_mobile_export_delete(root: dict) -> None:
    """19 移动端导出与删除：导出区独立密码框，按钮简化。"""
    patch_mobile_common(root)
    _insert_mobile_export_password(root)
    set_button_label(root, None, "导出加密文件", "导出", min_width=96.0)
    set_button_label(root, None, "验证并删除", "删除", min_width=96.0)
    # 导出区插了密码框，页面高度变了，底部固定块必须跟着下移。
    _shrink_board(root)


# 19 导出卡片插入密码框前的高度快照。
_EXPORT_CARD_H: list[float] = []


def _insert_mobile_export_password(root: dict) -> None:
    """在导出区补一个主密钥密码框——导出同样需要验证密码。"""
    content = find(root, "页面内容")
    if content is None:
        return
    # 导出卡片在前、删除卡片在后；两者都名「内容卡片」。
    blocks = [c for c in (content.get("children", []) or [])
              if c.get("name") == "内容卡片"]
    if len(blocks) < 2:
        return
    export_block, delete_block = blocks[0], blocks[1]

    donor = next((c for c in delete_block.get("children", []) or []
                  if c.get("name") == "表单字段"), None)
    actions = next((c for c in export_block.get("children", []) or []
                    if c.get("name") == "操作按钮"), None)
    if donor is None or actions is None:
        return

    _EXPORT_CARD_H.append(float(box(export_block).get("height", 0)))

    field = copy.deepcopy(donor)
    field["id"] = "sk-export-pwd"
    hint = next((n for n in walk_all(field)
                 if n.get("name") == "字段提示"), None)
    for node in walk_all(field):
        if node.get("name") == "字段标签":
            set_text(node, "MK-9C10 的主密钥密码")
        elif hint is not None and node is hint:
            set_text(node, "导出需验证该主密钥当前密码。")

    # 插到导出按钮上方：先按导出卡片内部宽度对齐 x，
    # 再用按钮顶边凍推到位置，两者的间距才是 12。
    eb = box(export_block)
    cb_x = float(eb.get("x", 0)) + 18.0
    shift(field, cb_x - float(box(field).get("x", 0)), 0)
    fb = box(field)
    fb["width"] = float(eb.get("width", 0)) - 36.0
    _widen(field, float(fb["width"]) - float(box(donor).get("width", 0)))
    ab = box(actions)
    shift(field, 0, float(ab.get("y", 0)) - 12.0
          - float(fb.get("y", 0)) - float(fb.get("height", 0)))
    export_block["children"].insert(
        export_block["children"].index(actions), field)

    # 插入了一个 102 高的密码框。导出卡片自身先收紧，
    # 再把后面的删除卡片与底部说明整体下移一个密码框的高度。
    # 用新旧底边差做别的它会失效：新宽度下靠灰带里的原坐标比起点真实需要的大。
    # 插入了一个高 102 的密码框，导出卡片体积增尽这一小片。
    # 卡片是纵向流，卡片内部流会把下一张卡片推上来，
    # 所以后续块必须按「字段高 + 间距」下移。
    # 不能用 grow_to_fit 的新旧差：它只增不减，且 pad 会把差值算偏小。
    field_h = float(box(field).get("height", 102.0))
    page = find(root, "页面内容")
    siblings = (page.get("children", []) or []) if page else []
    idx = siblings.index(export_block) if export_block in siblings else -1

    # 原有的底部说明行与按钮行之间的间距也被挤掉了，一并计入。
    tail = next((c for c in (export_block.get("children", []) or [])
                 if c.get("type") == "TEXT"), None)
    tail = None
    for child in export_block.get("children", []) or []:
        if child is not actions and child is not field:
            tail = child
    if tail is not None:
        shift(tail, 0, field_h + 12.0)
        delta = field_h + 12.0
    else:
        delta = field_h + 12.0

    grow_to_fit(export_block, pad=18.0)
    for node in siblings[idx + 1:]:
        shift(node, 0, delta)
    if page is not None:
        grow_to_fit(page, pad=0.0)
    TIGHTEN.append(export_block)
    if page is not None:
        TIGHTEN.append(page)


def patch_mobile_security(root: dict) -> None:
    """20 移动端安全设置：删底部说明卡片。

    页面里有两张同名的「内容卡片」：上面一张是设置项，
    下面一张是「完全离线 / 版本 / 后续版本」说明，后者要删。
    按 name 删会一张都删掉，因此按位置：只保留第一张。
    """
    patch_mobile_common(root)
    content = find(root, "页面内容")
    if content is None:
        return
    cards = [c for c in (content.get("children", []) or [])
             if c.get("name") == "内容卡片"]
    if len(cards) > 1:
        content["children"] = [c for c in (content.get("children", []) or [])
                               if c is not cards[-1]]
    drop(root, "安全提示")

# ---------------------------------------------------------------------------
# \u5206\u53d1
# ---------------------------------------------------------------------------
# \u6bcf\u5f20\u753b\u677f\u9700\u8981\u7684\u4fee\u6539\uff1a\u952e\u662f\u753b\u677f\u7684\u89d2\u8272\uff08\u54ea\u4e2a\u9875\u9762\u6b63\u5728\u88ab\u67e5\u770b\uff09\u3002
# 每张画板需要的修改：键是画板编号（哪张页面正在被查看）。
#
# 必须按编号而不是节点 ID——04 导入主密钥与 05 导出主密钥在 Figma 里
# 共用同一个源节点 3:27603，两者要的 patch 完全不同。
_PATCHES: dict[str, list[Callable[[dict], None]]] = {
    # \u684c\u9762\u7aef
    "01": [patch_unlock_picker],
    "02": [patch_master_key_page],
    "03": [patch_generate_page],
    "04": [patch_import_page],
    "05": [patch_export_page],
    "06": [lambda r: patch_secret_page_board(r)],
    "07": [patch_secret_add_page],
    "08": [patch_secret_import_page],
    "09": [patch_security_page],
    # \u79fb\u52a8\u7aef
    "10": [patch_mobile_unlock],
    "11": [patch_mobile_master_keys],
    "13": [patch_mobile_import_key],
    "14": [patch_mobile_secret_list],
    "15": [lambda r: patch_mobile_detail(r, True)],
    "16": [lambda r: patch_mobile_detail(r, False)],
    "17": [patch_mobile_secret_add],
    "18": [patch_mobile_secret_import],
    "19": [patch_mobile_export_delete],
    "20": [patch_mobile_security],
    # 说明板
    "21": [rewrite_beixuan],
}

_DERIVED = {
    # \u5bfc\u5165\u4e3b\u5bc6\u94a5\u753b\u677f\u4ee5\u300c\u5bfc\u51fa\u4e3b\u5bc6\u94a5\u300d\u4e3a\u5143\u67c4
    "04": build_import_master_key,
    # \u79fb\u52a8\u7aef\u300c\u751f\u6210\u4e3b\u5bc6\u94a5\u300d\u4e0d\u5e26 TAB\uff0c\u5355\u72ec\u6784\u5efa
    "12": build_mobile_generate_key,
}


def find_top(canvas: dict, node_id: str) -> dict:
    """\u6309\u8282\u70b9 ID \u53d6\u9876\u5c42\u753b\u677f\u3002"""
    for child in canvas.get("children", []) or []:
        if child.get("id") == node_id:
            return child
    raise KeyError(f"\u672a\u627e\u5230\u9876\u5c42\u753b\u677f {node_id}")


def build_board(canvas: dict, number: str, node_id: str, apply_patches: bool = True) -> dict:
    """\u8fd4\u56de\u5e94\u8be5\u753b\u677f\u6e32\u67d3\u7684\u8282\u70b9\u6811\u3002

    \u753b\u677f\u7f16\u53f7 number \u552f\u4e00\uff0c\u56e0\u6b64\u4e24\u5f20\u6d3e\u751f\u753b\u677f\u53ef\u4ee5\u5171\u7528\u540c\u4e00\u4e2a\u6e90\u8282\u70b9\uff0c
    \u5206\u53d1\u5fc5\u987b\u5148\u770b\u7f16\u53f7\u518d\u770b\u8282\u70b9 ID\u3002
    """
    if not apply_patches:
        return copy.deepcopy(find_top(canvas, node_id))

    # 部分控件（主密钥选择器）需要克隆**同端**另一张画板里的原生节点，
    # 先把画布按节点 ID 登记进 _CANVAS，patch 才能取到。
    set_canvas(canvas)
    node = copy.deepcopy(find_top(canvas, node_id))
    if number in _DERIVED:
        node = _DERIVED[number](node)
    for patch in _PATCHES.get(number, []):
        patch(node)
    relayout(node)
    tighten_pending()
    center_button_labels(node)
    align_stale_icons(node)
    return node
