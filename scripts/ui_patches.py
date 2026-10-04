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
    if node.get("name") == name:
        return node
    for child in node.get("children", []) or []:
        found = find(child, name)
        if found is not None:
            return found
    return None


def walk_all(node: dict):
    """\u9012\u5f52\u904d\u5386\u6240\u6709\u8282\u70b9\u3002"""
    yield node
    for child in node.get("children", []) or []:
        yield from walk_all(child)


def find_parent(node: dict, target: dict) -> dict | None:
    """\u627e\u5230 target \u7684\u7236\u8282\u70b9\u3002"""
    for child in node.get("children", []) or []:
        if child.get("id") == target.get("id"):
            return node
        found = find_parent(child, target)
        if found is not None:
            return found
    return None


def box(node: dict) -> dict:
    return node.setdefault("absoluteBoundingBox", {})


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
    w = width if width is not None else max(len(chars) * size * 0.62, size)
    return {
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
    }


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
    return node


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
    before = len(parent.get("children", []) or [])
    parent["children"] = [c for c in parent.get("children", []) or []
                          if needle not in str(c.get("characters", ""))]
    return before - len(parent["children"])


def drop(parent: dict, name: str) -> None:
    """\u5220\u9664\u7236\u8282\u70b9\u4e0b\u6307\u5b9a\u540d\u79f0\u7684\u5b50\u8282\u70b9\u3002"""
    parent["children"] = [c for c in parent.get("children", []) if c.get("name") != name]


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
def patch_mobile_unlock(root: dict) -> None:
    note = find(root, "\u672c\u5730\u5b89\u5168\u8bf4\u660e")
    if note is None:
        return
    # \u8fd9\u5757\u539f\u6765\u6709\u4e24\u884c\uff1a\u4e00\u884c\u662f\u201c\u660e\u6587\u5df2\u9690\u85cf\uff0c\u5185\u5b58\u5bc6\u94a5\u7f13\u5b58\u5df2\u6e05\u7a7a\u201d\uff08\u6280\u672f\u7ec6\u8282\uff0c\u4e0d\u7ed9\u7528\u6237\u770b\uff09\uff0c
    # \u4e00\u884c\u662f\u7248\u672c\u53f7\u3002\u5f53\u524d\u5b9e\u73b0\u662f\u628a\u7b2c\u4e00\u884c**\u6539\u5199**\u6210\u7248\u672c\u53f7\u5185\u5bb9\uff0c\u7ed3\u679c\u4e24\u884c\u5b8c\u5168\u76f8\u540c\u3002
    # \u6b63\u786e\u505a\u6cd5\u662f\u5220\u6389\u7b2c\u4e00\u884c\uff08\u6574\u4e2a\u5b50\u8282\u70b9\uff09\uff0c\u53ea\u7559\u7248\u672c\u53f7\u90a3\u884c\u3002
    drop_text(note, "\u660e\u6587\u5df2\u9690\u85cf")


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


def build_mobile_generate_key(source: dict) -> dict:
    """\u4ee5\u79fb\u52a8\u7aef\u300c\u751f\u6210\u4e0e\u5bfc\u5165\u4e3b\u5bc6\u94a5\u300d\u4e3a\u5143\u67a4\uff0c\u751f\u6210\u300c\u751f\u6210\u4e3b\u5bc6\u94a5\u300dTAB \u6001\u753b\u677f\u3002"""
    board = copy.deepcopy(source)
    board["id"] = "sk-mobile-generate-key"
    board["name"] = "\u79fb\u52a8\u7aef \u00b7 \u751f\u6210\u4e3b\u5bc6\u94a5"

    content = find(board, "\u9875\u9762\u5185\u5bb9")
    if content is None:
        return board

    # TAB \u9009\u4e2d\u6001\u8f6c\u5230\u300c\u751f\u6210\u4e3b\u5bc6\u94a5\u300d\u3002
    # \u6ce8\u610f\uff1a\u5148\u524d\u53ea\u6362\u4e86 TAB \u80cc\u666f\u8272\uff0c\u6ca1\u6362\u6807\u7b7e\u6587\u5b57\u989c\u8272\u3002
    # \u7ed3\u679c\u662f\u80cc\u666f\u767d\u7684\u90a3\u4e2a\u6587\u5b57\u5374\u662f\u7070\u8272\u3001\u80cc\u666f\u7070\u7684\u90a3\u4e2a\u5374\u662f\u7eff\u8272\uff0c
    # \u89c6\u89c9\u4e0a\u4ecd\u7136\u50cf\u300c\u4ece\u6587\u4ef6\u5bfc\u5165\u300d\u88ab\u9009\u4e2d\u3002\u4e24\u8005\u5fc5\u987b\u540c\u6b65\u5207\u6362\u3002
    tabs = find(content, "\u6dfb\u52a0\u65b9\u5f0f")
    if tabs is not None:
        for tab in tabs.get("children", []) or []:
            selected = tab.get("name") == "\u751f\u6210\u65b9\u5f0f"
            tab["fills"] = _solid(WHITE if selected else SURFACE)
            label = find(tab, "\u6807\u7b7e")
            if label is not None:
                label["fills"] = _solid(ACCENT if selected else MUTED)

    x = float(box(content).get("x", 0)) + 20
    w = float(box(content).get("width", 390)) - 40

    # \u5269\u4f59\u5185\u5bb9\u6574\u4f53\u91cd\u5efa\uff1a\u8bf4\u660e\u6587\u5b57 + \u4e09\u4e2a\u5b57\u6bb5 + \u63d0\u793a + \u4e3b\u6309\u94ae + \u5c3e\u6ce8
    subtitle = text_node("\u8bf4\u660e", "\u5728\u672c\u673a\u751f\u6210\u4e00\u628a\u968f\u673a RSA-2048 \u4e3b\u5bc6\u94a5\u3002", x, 2780.0, 13, MUTED, width=w)
    fields = [
        _mobile_pwd(x, 2815.0, w, "\u4e3b\u5bc6\u94a5\u540d\u79f0\uff08\u53ef\u9009\uff09", "\u53ef\u4ee5\u7559\u7a7a",
                    "\u540d\u79f0\u4ec5\u7528\u4e8e\u8fa8\u8bc6\uff0c\u4e0d\u53c2\u4e0e\u52a0\u5bc6\u3002"),
        _mobile_pwd(x, 2912.0, w, "\u4e3b\u5bc6\u94a5\u5bc6\u7801", "\u2022" * 11, None),
        _mobile_pwd(x, 3009.0, w, "\u786e\u8ba4\u4e3b\u5bc6\u94a5\u5bc6\u7801", "\u2022" * 11, None),
    ]
    warning = _mobile_notice(x, 3106.0, w,
        "\u8bf7\u59a5\u5584\u8bb0\u4f4f\u4e3b\u5bc6\u94a5\u5bc6\u7801",
        "\u5bc6\u7801\u4e22\u5931\u65e0\u6cd5\u89e3\u5bc6\uff0c\u5bc6\u5323\u65e0\u6cd5\u4e3a\u4f60\u91cd\u7f6e\u5bc6\u7801\u3002")
    action = _mobile_button(x, 3182.0, w, "\u751f\u6210\u5e76\u4fdd\u5b58", ACCENT, WHITE, 168)
    footer = text_node("\u8bf4\u660e",
        "\u7b2c\u4e00\u628a\u4e3b\u5bc6\u94a5\u81ea\u52a8\u8bbe\u4e3a\u9ed8\u8ba4\u3002\u5df2\u6709\u9ed8\u8ba4\u4e3b\u5bc6\u94a5\u65f6\uff0c\u65b0\u751f\u6210\u7684\u4e3b\u5bc6\u94a5\u4f5c\u4e3a\u5907\u9009\u4fdd\u5b58\u3002",
        x, 3242.0, 12, MUTED, width=w)

    content["children"] = [tabs, subtitle] + fields + [warning, action, footer]
    content["absoluteBoundingBox"] = {
        "x": box(content).get("x", 0), "y": box(content).get("y", 0),
        "width": box(content).get("width", 390), "height": 574.0,
    }
    return board


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
    return {
        "id": f"sk-b-{label}", "name": "\u64cd\u4f5c\u6309\u94ae", "type": "FRAME",
        "fills": _solid(fill), "cornerRadius": 8.0,
        "absoluteBoundingBox": {"x": x + (w - width) / 2, "y": y, "width": width, "height": 44},
        "children": [text_node("\u6309\u94ae\u6587\u5b57", label, x + (w - width) / 2 + 24, y + 13, 14, text_color)],
    }


# ---------------------------------------------------------------------------
# \u5206\u53d1
# ---------------------------------------------------------------------------
# \u6bcf\u5f20\u753b\u677f\u9700\u8981\u7684\u4fee\u6539\uff1a\u952e\u662f\u753b\u677f\u7684\u89d2\u8272\uff08\u54ea\u4e2a\u9875\u9762\u6b63\u5728\u88ab\u67e5\u770b\uff09\u3002
_PATCHES: dict[str, list[Callable[[dict], None]]] = {
    "3:26936": [patch_unlock_footer, patch_unlock_lock_note],   # 01 解锁与锁定
    "3:27258": [lambda r: patch_navigation(r, "\u4e3b\u5bc6\u94a5\u7ba1\u7406")],  # 02 \u4e3b\u5bc6\u94a5\u7ba1\u7406
    "3:27385": [lambda r: patch_navigation(r, "\u4e3b\u5bc6\u94a5\u7ba1\u7406")],  # 03 \u751f\u6210\u4e3b\u5bc6\u94a5
    "3:27603": [lambda r: patch_navigation(r, "\u4e3b\u5bc6\u94a5\u7ba1\u7406"),
                patch_export_original_password],                      # 05 \u5bfc\u51fa\u4e3b\u5bc6\u94a5
    "3:26990": [lambda r: patch_navigation(r, "\u673a\u5bc6\u4fe1\u606f\u7ba1\u7406"),
                patch_secret_page],                                   # 06 \u673a\u5bc6\u4fe1\u606f\u7ba1\u7406
    "3:27146": [lambda r: patch_navigation(r, "\u673a\u5bc6\u4fe1\u606f\u7ba1\u7406")],  # 07 \u65b0\u589e\u673a\u5bc6\u4fe1\u606f
    "3:27493": [lambda r: patch_navigation(r, "\u673a\u5bc6\u4fe1\u606f\u7ba1\u7406")],  # 08 \u5bfc\u5165\u673a\u5bc6\u4fe1\u606f
    "3:27713": [lambda r: patch_navigation(r, "\u5b89\u5168\u8bbe\u7f6e")],        # 09 \u5b89\u5168\u8bbe\u7f6e
    "3:27821": [patch_mobile_unlock],                                 # 10 \u79fb\u52a8\u7aef\u89e3\u9501
    "3:27863": [patch_mobile_secret_list],                             # 11 \u79fb\u52a8\u7aef\u673a\u5bc6\u4fe1\u606f\u5217\u8868
    "3:28205": [lambda r: None],                                       # 17 \u79fb\u52a8\u7aef\u5bfc\u5165\u4e3b\u5bc6\u94a5\uff08\u65e0\u9700\u4fee\u6539\uff09
}
_DERIVED = {
    # \u5bfc\u5165\u4e3b\u5bc6\u94a5\u753b\u677f\u4ee5\u300c\u5bfc\u51fa\u4e3b\u5bc6\u94a5\u300d\u4e3a\u5143\u67a4
    "04": ("3:27603", build_import_master_key),
    # \u79fb\u52a8\u7aef\u300c\u751f\u6210\u4e3b\u5bc6\u94a5\u300dTAB \u4ee5\u300c\u751f\u6210\u4e0e\u5bfc\u5165\u4e3b\u5bc6\u94a5\u300d\u4e3a\u5143\u67a4
    "16": ("3:28205", build_mobile_generate_key),
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

    if number in _DERIVED:
        source_id, builder = _DERIVED[number]
        return builder(find_top(canvas, source_id))

    node = copy.deepcopy(find_top(canvas, node_id))
    for patch in _PATCHES.get(node_id, []):
        patch(node)
    return node
