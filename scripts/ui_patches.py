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

    x = float(box(content).get("x", 0)) + 20
    w = float(box(content).get("width", 390)) - 40
    top = 268.0

    title = text_node("\u6807\u9898", "\u751f\u6210\u4e3b\u5bc6\u94a5", x, top - 56.0, 20, INK, width=w)
    subtitle = text_node("\u8bf4\u660e", "\u5728\u672c\u673a\u751f\u6210\u4e00\u628a\u968f\u673a RSA-2048 \u4e3b\u5bc6\u94a5\u3002",
                         x, top, 13, MUTED, width=w)
    fields = [
        _mobile_pwd(x, top + 35.0, w, "\u4e3b\u5bc6\u94a5\u540d\u79f0\uff08\u53ef\u9009\uff09", "\u53ef\u4ee5\u7559\u7a7a",
                    "\u540d\u79f0\u4ec5\u7528\u4e8e\u8fa8\u8bc6\uff0c\u4e0d\u53c2\u4e0e\u52a0\u5bc6\u3002"),
        _mobile_pwd(x, top + 132.0, w, "\u4e3b\u5bc6\u94a5\u5bc6\u7801", "\u2022" * 11, None),
        _mobile_pwd(x, top + 229.0, w, "\u786e\u8ba4\u4e3b\u5bc6\u94a5\u5bc6\u7801", "\u2022" * 11, None),
    ]
    warning = _mobile_notice(x, top + 326.0, w,
        "\u8bf7\u59a5\u5584\u8bb0\u4f4f\u4e3b\u5bc6\u94a5\u5bc6\u7801",
        "\u5bc6\u7801\u4e22\u5931\u65e0\u6cd5\u89e3\u5bc6\uff0c\u5bc6\u5323\u65e0\u6cd5\u4e3a\u4f60\u91cd\u7f6e\u5bc6\u7801\u3002")
    action = _mobile_button(x, top + 402.0, w, "\u751f\u6210\u4e3b\u5bc6\u94a5", ACCENT, WHITE, 168)

    content["children"] = [title, subtitle] + fields + [warning, action]
    cb = box(content)
    cb["height"] = 268.0 + 402.0 + 44.0 - float(cb.get("y", 0))
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
    nav = find(board, "\u9875\u9762\u5bfc\u822a")
    top = float(box(nav).get("y", 0)) if nav is not None else float(bb.get("y", 0))
    bb["height"] = float(bb.get("y", 0)) + 844.0 - float(bb.get("y", 0))


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
# 状态图标与钥匙选择器（v2 新增）
# ---------------------------------------------------------------------------
# 找到 / 缺失两种状态的语义色。取自 UI 设计规范里的强调色与危险色。
OK_COLOR = {"r": 0.0784313753247261, "g": 0.47058823704719543, "b": 0.4274509847164154, "a": 1.0}
ALERT_COLOR = {"r": 0.6980392336845398, "g": 0.27843138575553894, "b": 0.27843138575553894, "a": 1.0}
WARN_COLOR = {"r": 0.7098039215686275, "g": 0.5137254901960784, "b": 0.09411764705882353, "a": 1.0}

_ICON_LIBRARY: dict[str, dict] = {}
_ICON_NAMES = ("circle-check", "circle-alert", "chevron-down", "key-round", "eye-off")


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


def build_key_selector(x: float, y: float, w: float, label: str,
                       name: str, mkid: str, tag: str | None) -> dict:
    """构造解锁页的「主密钥选择」区块。

    需求由「输入默认主密钥密码解锁」改为「选择一把主密钥解锁」，
    原来那行只读文字（名称 + ID）不够用，改成与 07 新增机密信息同款的
    下拉选择块：标签行 + 当前选中行（钥匙图标 / 名称 / ID / 状态标签 / 展开箭头）。
    """
    sel_y = y + 29.0
    sel_h = 65.0
    pad = 12.0
    gap = 8.0
    icon_w = 20.0
    chev_w = 16.0
    tag_w = 46.0

    row = rect_node("选中行", x, sel_y, w, sel_h, WHITE, 8.0, stroke=BORDER)
    row["layoutMode"] = "HORIZONTAL"
    row["primaryAxisSizingMode"] = "FIXED"
    row["counterAxisSizingMode"] = "CENTER"
    row["itemSpacing"] = gap
    row["paddingLeft"] = pad
    row["paddingRight"] = pad
    row["children"] = []

    key_icon = clone_icon("key-round", x + pad, sel_y + (sel_h - 18) / 2, MUTED)
    if key_icon is not None:
        key_icon["cornerRadius"] = 0
        for child in walk_all(key_icon):
            child["cornerRadius"] = 0
        row["children"].append(key_icon)
        icon_w = 26.0

    text_x = x + pad + icon_w + gap
    text_w = w - pad - icon_w - gap - chev_w - gap
    name_node = text_node("主密钥名称", name, text_x, sel_y + 14.0, 14.0, INK, width=text_w)
    name_node["style"] = dict(name_node.get("style") or {})
    name_node["style"]["fontSize"] = 14.0
    name_node["absoluteBoundingBox"]["width"] = text_w
    name_node["absoluteBoundingBox"]["height"] = 20.0
    id_node = text_node("主密钥 ID", mkid, text_x, sel_y + 37.0, 12.0, MUTED, width=text_w)
    row["children"].append(name_node)
    row["children"].append(id_node)

    right_x = x + w - pad
    if tag:
        tag_box = rect_node("状态标签", right_x - tag_w, sel_y + (sel_h - 21) / 2,
                            tag_w, 21.0, SURFACE, 6.0)
        tag_text = text_node("状态", tag, right_x - tag_w + 9.0,
                             sel_y + (sel_h - 21) / 2 + 4.0, 11.0, ACCENT, width=tag_w - 18)
        tag_box["children"] = [tag_text]
        row["children"].append(tag_box)
        right_x -= tag_w + gap

    chev = clone_icon("chevron-down", right_x - chev_w, sel_y + (sel_h - 16) / 2, MUTED)
    if chev is not None:
        chev["cornerRadius"] = 0
        for child in walk_all(chev):
            child["cornerRadius"] = 0
        row["children"].append(chev)

    block = rect_node("主密钥选择", x, y, w, sel_y + sel_h - y, None, 0.0)
    block["layoutMode"] = "VERTICAL"
    block["itemSpacing"] = 8.0
    block["fills"] = []
    label_node = text_node("字段标签", label, x, y, 13.0, MUTED, width=w)
    block["children"] = [label_node, row]
    return block


def replace_child(parent: dict, old: dict, new: dict) -> None:
    """用 new 替换 parent 的子节点 old。"""
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
    """
    drop(root, "本地安全状态")
    drop_text(root, "离线状态")

    bottom = find(root, "导航底部")
    if bottom is None:
        return
    bb = box(bottom)
    bb["height"] = 40.0
    bb["y"] = float(bb.get("y", 0)) + 29.0


def set_button_label(root: dict, container: str, old: str, new: str,
                     min_width: float | None = None) -> bool:
    """改按钮文案；必要时加宽按钮并让文字重新居中。

    按钮宽度在设计稿里是 HUG（跟随文字），但 absoluteBoundingBox 是定值。
    文案变长后必须手动加宽，否则文字会溢出按钮边框。
    同行的后续按钮要一并右移，原间距才不变。
    """
    holder = find(root, container)
    if holder is None:
        return False

    buttons = [c for c in holder.get("children", []) or []
               if c.get("name") == "操作按钮"]
    target = next((b for b in buttons
                   if (find(b, "按钮文字") or {}).get("characters") == old), None)
    if target is None:
        return False

    label = find(target, "按钮文字")
    tb = box(target)
    style = (label or {}).get("style") or {}
    size = float(style.get("fontSize", 13))
    text_w = len(new) * size

    if min_width is not None and float(tb.get("width", 0)) < min_width:
        extra = min_width - float(tb.get("width", 0))
        tb["width"] = min_width
        if label is not None:
            lb = box(label)
            lb["x"] = float(lb.get("x", 0)) + extra / 2
            lb["width"] = text_w
        index = holder["children"].index(target)
        for sibling in holder["children"][index + 1:]:
            shift(sibling, extra, 0)
        hb = box(holder)
        hb["width"] = float(hb.get("width", 0)) + extra
    elif label is not None:
        lb = box(label)
        old_w = len(old) * size
        if text_w > old_w:
            lb["x"] = float(lb.get("x", 0)) - (text_w - old_w) / 2
        lb["width"] = text_w

    if label is not None:
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
def patch_desktop_common(root: dict) -> None:
    """桌面九张画板共用的改动：删页脚、删「备选」。"""
    strip_footer(root)
    rewrite_beixuan(root)


def patch_unlock_picker(root: dict) -> None:
    """01 解锁与锁定：默认主密钥改为可选主密钥。"""
    patch_desktop_common(root)

    title = find(root, "标题")
    if title is not None:
        set_text(title, "解锁本机密匣")
    for node in walk_all(root):
        if node.get("type") != "TEXT":
            continue
        chars = node.get("characters", "")
        if chars == "输入默认主密钥密码，解锁本机的密匣":
            set_text(node, "选择主密钥，输入主密钥密码解锁")
        elif chars == "默认主密钥密码":
            set_text(node, "主密钥密码")

    old = find(root, "默认主密钥")
    if old is not None:
        holder = find_parent(root, old)
        if holder is not None:
            ob = box(old)
            selector = build_key_selector(float(ob.get("x", 0)), float(ob.get("y", 0)),
                                          float(ob.get("width", 0)),
                                          "主密钥", "工作密钥", "MK-7A21", "默认")
            replace_child(holder, old, selector)


def patch_master_key_page(root: dict) -> None:
    """02 主密钥管理与详情：「导出备份」改为「导出主密钥」。"""
    patch_desktop_common(root)
    set_button_label(root, "详情操作", "导出备份", "导出主密钥", min_width=132.0)


def patch_generate_page(root: dict) -> None:
    """03 生成主密钥：删右栏通栏，按钮改「保存主密钥」。"""
    patch_desktop_common(root)
    widen_workspace(root, "生成工作区")
    set_button_label(root, "生成操作", "生成并保存", "保存主密钥", min_width=132.0)


def patch_import_page(root: dict) -> None:
    """04 导入主密钥：密码框对齐 05 样式、按钮改名、删右栏。"""
    patch_desktop_common(root)
    _align_pwd_fields(root)
    widen_workspace(root, "导入工作区")
    set_button_label(root, "导入操作", "导入为备选主密钥", "导入主密钥", min_width=132.0)


def patch_export_page(root: dict) -> None:
    """05 主密钥文件导出：删右栏，绿色提示移到按钮上方，按钮改名。"""
    patch_desktop_common(root)
    widen_workspace(root, "导出工作区")
    _move_export_tip(root)
    set_button_label(root, "导出操作", "导出加密主密钥", "导出主密钥", min_width=132.0)


def _move_export_tip(root: dict) -> None:
    """把右栏的绿色「导出不会删除原内容」提示移到主栏按钮上方。

    提示本身要留（说明导出是无损的），只是原先挂在右栏辅助说明里，
    右栏删除后会一起消失，所以先摘出来再插回主栏。
    """
    area = find(root, "导出工作区")
    if area is None:
        return
    tail = find(area, "安全提示")
    if tail is None:
        return

    actions = find(area, "导出操作")
    if actions is None:
        return
    ab = box(actions)
    tb = box(tail)
    shift(tail, 0, -(float(ab.get("y", 0)) - float(tb.get("y", 0))) + 60.0)
    new_y = float(ab.get("y", 0)) - float(tb.get("height", 68)) - 12.0
    place(actions, tail, y=new_y)
    actions["children"] = [tail] + [c for c in actions.get("children", []) or []
                                    if c is not tail]
    grow_to_fit(find(area, "内容卡片") or actions, pad=24.0)


def _align_pwd_fields(root: dict) -> None:
    """把 04 的密码框改成与 05 一致的样式。

    04 的密码框原先是手搭的：宽度按 580 算（扣掉卡片内边距应为 532）、
    缺 eye-off 图标、输入内容与边框贴边。05 是正规 auto-layout 结构，
    直接复用它的字段节点最省事，也不会引入手工拼坐标的偏差。
    """
    area = find(root, "导入工作区")
    if area is None:
        return
    donor_area = find(root, "导出工作区")
    if donor_area is None:
        return

    fields = [c for c in (area.get("children", []) or [])
              if c.get("name") == "表单字段"]
    donors = [c for c in (donor_area.get("children", []) or [])
              if c.get("name") == "表单字段"]
    if not fields or len(donors) < len(fields):
        return

    card = area["children"][0]
    card_w = float(box(card).get("width", 0))
    for field, donor in zip(fields, donors[1:] if len(donors) > len(fields) else donors):
        replacement = copy.deepcopy(donor)
        place(card, replacement, x=float(box(card).get("x", 0)) + 24.0)
        _widen(replacement, card_w - 24.0 * 2 - float(box(replacement).get("width", 0)))
        replace_child(card, field, replacement)


def patch_secret_add_page(root: dict) -> None:
    """07 新增机密信息：删右栏通栏，按钮改「保存」。"""
    patch_desktop_common(root)
    widen_workspace(root, "新增工作区")
    set_button_label(root, "保存操作", "加密并保存", "保存", min_width=96.0)


def patch_secret_import_page(root: dict) -> None:
    """08 导入机密信息：删右栏、文案改「关联的主密钥」、绿勾、按钮改名。"""
    patch_desktop_common(root)
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


def patch_security_page(root: dict) -> None:
    """09 安全设置：删右栏通栏。"""
    patch_desktop_common(root)
    widen_workspace(root, "设置工作区")


# ---------------------------------------------------------------------------
# 移动端逐板
# ---------------------------------------------------------------------------
def patch_mobile_common(root: dict) -> None:
    """移动端画板共用：删「备选」。"""
    rewrite_beixuan(root)


def patch_mobile_nav(root: dict, active: str) -> None:
    """移动端底部导航重排为主密钥 / 机密信息 / 安全设置三项。"""
    nav = find(root, "底部导航")
    if nav is None:
        return
    items = [c for c in (nav.get("children", []) or [])
             if c.get("name") == "导航项"]
    if not items:
        return

    source = items[0]
    rebuilt = []
    for index, (icon, label) in enumerate(
            (("key-round", "主密钥"), ("files", "机密信息"), ("settings-2", "安全设置"))):
        node = copy.deepcopy(source)
        node["id"] = f"sk-mnav-{index}"
        node["layoutAlign"] = "STRETCH" if index == 0 else "INHERIT"
        node["layoutGrow"] = 1.0 if index == 0 else 0.0
        sb = box(source)
        nb = box(node)
        nb["x"] = float(sb.get("x", 0)) + index * float(sb.get("width", 0))
        nb["width"] = float(sb.get("width", 0))
        inner = f"{icon}"
        for sub in walk_all(node):
            if sub.get("name") == inner:
                inner_node = sub
        icon_frame = next((c for c in node.get("children", []) or []
                           if c.get("name") == inner), None)
        if icon_frame is None:
            replacement = clone_icon(icon, float(box(node).get("x", 0)) + 12.0,
                                    float(box(node).get("y", 0)) + 13.0, MUTED)
            if replacement is not None:
                replacement["cornerRadius"] = 0
                for sub in walk_all(replacement):
                    sub["cornerRadius"] = 0
                replace_child(node, node["children"][0], replacement)
        name_node = next((c for c in node.get("children", []) or []
                          if c.get("name") == "导航名称"), None)
        if name_node is not None:
            set_text(name_node, label)
        if active == label:
            node["name"] = "导航项-激活"
        rebuilt.append(node)

    nav["children"] = rebuilt


def patch_mobile_unlock(root: dict) -> None:
    """10 移动端解锁：改为可选主密钥解锁。"""
    patch_mobile_common(root)
    patch_mobile_nav(root, "主密钥")

    for node in walk_all(root):
        if node.get("type") != "TEXT":
            continue
        chars = node.get("characters", "")
        if chars == "输入默认主密钥密码，解锁本机的密匣":
            set_text(node, "选择主密钥，输入主密钥密码解锁")
        elif chars == "默认主密钥密码":
            set_text(node, "主密钥密码")
    drop_text(root, "明文已隐藏，内存密钥缓存已清空。")

    old = find(root, "默认主密钥")
    if old is not None:
        holder = find_parent(root, old)
        if holder is not None:
            ob = box(old)
            selector = build_key_selector(float(ob.get("x", 0)), float(ob.get("y", 0)),
                                          float(ob.get("width", 0)),
                                          "主密钥", "工作密钥", "MK-7A21", "默认")
            replace_child(holder, old, selector)


def patch_mobile_master_keys(root: dict) -> None:
    """11 移动端主密钥管理：拆分生成/导入按钮，容量提示下移。"""
    patch_mobile_common(root)
    patch_mobile_nav(root, "主密钥")
    _rebuild_mobile_key_actions(root, find(root, "页面内容"))


def _rebuild_mobile_key_actions(root: dict, content: dict) -> None:
    """把「生成 / 导入主密钥」单按钮拆成两枚并移到容量行上方。"""
    if content is None:
        return
    children = content.get("children", []) or []
    combined = next((c for c in children
                     if "生成" in (c.get("name") or "") and "导入" in (c.get("name") or "")), None)
    capacity = next((c for c in children if c.get("name") == "容量状态"), None)
    if combined is None or capacity is None:
        return

    cb = box(capacity)
    inner = cb["x"] + 24.0
    inner_w = float(cb.get("width", 0)) - 48.0
    gap = 8.0
    btn_w = (inner_w - gap) / 2
    btn_y = float(cb.get("y", 0)) - 44.0

    buttons = []
    for index, label in enumerate(("生成主密钥", "导入主密钥")):
        node = rect_node(f"{label}按钮", inner + index * (btn_w + gap), btn_y,
                         btn_w, 36.0, WHITE, 8.0, stroke=BORDER)
        node["layoutMode"] = "HORIZONTAL"
        node["primaryAxisAlignItems"] = "CENTER"
        node["counterAxisAlignItems"] = "CENTER"
        node["children"] = [text_node("按钮文字", label, 0.0, 0.0, 13.0, ACCENT, width=0.0)]
        buttons.append(node)

    capacity["children"] = buttons + [c for c in capacity.get("children", []) or []]
    content["children"] = [c for c in children if c is not combined]

    note = next((n for n in walk_all(capacity)
                 if n.get("type") == "TEXT" and "容量" in n.get("characters", "")), None)
    if note is not None:
        set_text(note, "主密钥数量已满，先导出并删除一把主密钥，才能新增。")


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
    patch_mobile_nav(root, "主密钥")
    strip_tabs(root)
    set_button_label(root, "生成操作", "生成并保存", "生成主密钥", min_width=132.0)


def patch_mobile_import_key(root: dict) -> None:
    """13 移动端导入主密钥：去 TAB，按钮改「导入主密钥」。"""
    patch_mobile_common(root)
    patch_mobile_nav(root, "主密钥")
    strip_tabs(root)
    set_button_label(root, "导入操作", "导入为备选主密钥", "导入主密钥", min_width=132.0)


def patch_mobile_detail(root: dict, found: bool) -> None:
    """15 / 16 移动端机密信息详情：区块重排 + 主密钥状态图标。"""
    patch_mobile_common(root)
    patch_mobile_nav(root, "机密信息")
    _relayout_detail(root, found)


def _relayout_detail(root: dict, found: bool) -> None:
    content = find(root, "页面内容")
    if content is None:
        return
    children = content.get("children", []) or []
    blocks = [c for c in children if c.get("name") in ("详情区块", "密钥区块", "信息区块")]
    if len(blocks) < 2:
        return

    key_block, body_block = blocks[0], blocks[1]
    rows = [c for c in (key_block.get("children", []) or [])
            if c.get("name") == "详情字段"]

    title = next((n for n in key_block.get("children", []) or []
                  if n.get("name") == "区块标题"), None)
    if title is not None:
        set_text(title, "关联主密钥")

    if rows:
        # 第一区块只保留 ID 与名称两行，其余（标题行）迁到第二区块。
        for extra in rows[2:]:
            key_block["children"].remove(extra)
        if found:
            set_icon_status(key_block, True)
        else:
            holder = find_parent(key_block, title) if title else None
            for node in walk_all(key_block):
                if node.get("type") == "TEXT" and node.get("name") == "状态标签":
                    pass
            _append_warn(key_block)

    body_title = next((n for n in body_block.get("children", []) or []
                       if n.get("name") == "区块标题"), None)
    if body_title is not None:
        set_text(body_title, "机密信息")

    label = next((n for n in body_block.get("children", []) or []
                  if n.get("type") == "TEXT" and n.get("characters") == "机密信息"), None)
    if label is not None:
        set_text(label, "机密信息内容")


def _append_warn(key_block: dict) -> None:
    """在缺失态的主密钥区块内追加黄色提示行。"""
    rows = [c for c in (key_block.get("children", []) or [])
            if c.get("name") == "详情字段"]
    if not rows:
        return
    rb = box(rows[-1])
    text = text_node("缺失提示", "主密钥缺失，请先导入主密钥",
                     float(rb.get("x", 0)), float(rb.get("y", 0)) + 34.0,
                     12.0, WARN_COLOR, width=260.0)
    key_block["children"].append(text)
    grow_to_fit(key_block, pad=14.0)


def patch_mobile_secret_add(root: dict) -> None:
    """17 移动端新增机密信息：删开发提示，按钮改「保存」。"""
    patch_mobile_common(root)
    patch_mobile_nav(root, "机密信息")
    drop(root, "安全提示")
    drop_text(root, "默认选用当前默认主密钥加密保存，不需要重复选择。")
    set_button_label(root, "保存操作", "加密并保存", "保存", min_width=96.0)


def patch_mobile_secret_import(root: dict) -> None:
    """18 移动端导入机密信息：标题改「主密钥」、绿勾、按钮改「导入」。"""
    patch_mobile_common(root)
    patch_mobile_nav(root, "机密信息")

    block = find(root, "已找到的本机主密钥") or find(root, "主密钥区块")
    if block is not None:
        for node in walk_all(block):
            if node.get("type") == "TEXT" and node.get("characters") == "已找到的本机主密钥":
                set_text(node, "主密钥")
        set_icon_status(block, True)
    drop_text(root, "名称未填写，主密钥在本机可用。")
    set_button_label(root, "导入操作", "验证并导入", "导入", min_width=96.0)


def patch_mobile_export_delete(root: dict) -> None:
    """19 移动端导出与删除：导出区独立密码框，按钮简化。"""
    patch_mobile_common(root)
    patch_mobile_nav(root, "机密信息")
    _insert_mobile_export_password(root)
    set_button_label(root, "导出操作", "导出加密文件", "导出", min_width=96.0)
    set_button_label(root, "删除操作", "验证并删除", "删除", min_width=96.0)


def _insert_mobile_export_password(root: dict) -> None:
    """在导出区补一个主密钥密码框——导出同样需要验证密码。"""
    content = find(root, "页面内容")
    if content is None:
        return
    blocks = [c for c in (content.get("children", []) or [])
              if c.get("name") in ("导出区块", "删除区块")]
    if len(blocks) < 2:
        return
    export_block, delete_block = blocks[0], blocks[1]

    donor = next((c for c in delete_block.get("children", []) or []
                  if c.get("name") == "表单字段"), None)
    if donor is None:
        return

    field = copy.deepcopy(donor)
    for node in walk_all(field):
        if node.get("type") != "TEXT":
            continue
        chars = node.get("characters", "")
        if chars:
            set_text(node, "MK-9C10 的主密钥密码")

    actions = next((c for c in export_block.get("children", []) or []
                    if c.get("name") == "操作按钮"), None)
    if actions is None:
        return
    ab = box(actions)
    fb = box(field)
    shift(field, 0, float(ab.get("y", 0)) - float(fb.get("height", 0)) - 12.0 - float(fb.get("y", 0)))
    export_block["children"].append(field)
    grow_to_fit(export_block, pad=14.0)


def patch_mobile_security(root: dict) -> None:
    """20 移动端安全设置：删底部说明卡片。"""
    patch_mobile_common(root)
    patch_mobile_nav(root, "安全设置")
    drop(root, "内容卡片")
    drop(root, "安全提示")

# ---------------------------------------------------------------------------
# \u5206\u53d1
# ---------------------------------------------------------------------------
# \u6bcf\u5f20\u753b\u677f\u9700\u8981\u7684\u4fee\u6539\uff1a\u952e\u662f\u753b\u677f\u7684\u89d2\u8272\uff08\u54ea\u4e2a\u9875\u9762\u6b63\u5728\u88ab\u67e5\u770b\uff09\u3002
_PATCHES: dict[str, list[Callable[[dict], None]]] = {
    # \u684c\u9762\u7aef
    "3:26936": [patch_unlock_picker],                              # 01 \u89e3\u9501\u4e0e\u9501\u5b9a
    "3:27258": [patch_master_key_page],                             # 02 \u4e3b\u5bc6\u94a5\u7ba1\u7406
    "3:27385": [patch_generate_page],                               # 03 \u751f\u6210\u4e3b\u5bc6\u94a5
    "3:27603": [patch_export_page],                                 # 05 \u5bfc\u51fa\u4e3b\u5bc6\u94a5
    "3:26990": [patch_secret_page],                                 # 06 \u673a\u5bc6\u4fe1\u606f\u7ba1\u7406
    "3:27146": [patch_secret_add_page],                             # 07 \u65b0\u589e\u673a\u5bc6\u4fe1\u606f
    "3:27493": [patch_secret_import_page],                          # 08 \u5bfc\u5165\u673a\u5bc6\u4fe1\u606f
    "3:27713": [patch_security_page],                                # 09 \u5b89\u5168\u8bbe\u7f6e
    # \u79fb\u52a8\u7aef
    "3:27821": [patch_mobile_unlock],                               # 10 \u89e3\u9501
    "3:28135": [patch_mobile_master_keys],                          # 11 \u4e3b\u5bc6\u94a5\u7ba1\u7406
    "3:27863": [patch_mobile_secret_list],
    "3:27971": [lambda r: patch_mobile_detail(r, True)],            # 15 \u8be6\u60c5
    "3:28025": [lambda r: patch_mobile_detail(r, False)],           # 16 \u7f3a\u5931\u4e3b\u5bc6\u94a5\u8be6\u60c5
    "3:28077": [patch_mobile_secret_add],                           # 17 \u65b0\u589e\u673a\u5bc6\u4fe1\u606f
    "3:28252": [patch_mobile_secret_import],                        # 18 \u5bfc\u5165\u673a\u5bc6\u4fe1\u606f
    "3:28300": [patch_mobile_export_delete],                        # 19 \u5bfc\u51fa\u4e0e\u5220\u9664
    "3:28359": [patch_mobile_security],                             # 20 \u5b89\u5168\u8bbe\u7f6e
}

_DERIVED = {
    # \u5bfc\u5165\u4e3b\u5bc6\u94a5\u753b\u677f\u4ee5\u300c\u5bfc\u51fa\u4e3b\u5bc6\u94a5\u300d\u4e3a\u5143\u67c4
    "04": ("3:27603", build_import_master_key),
    # \u79fb\u52a8\u7aef\u300c\u751f\u6210\u4e3b\u5bc6\u94a5\u300d\u4e0d\u5e26 TAB\uff0c\u5355\u72ec\u6784\u5efa
    "12": ("3:28205", build_mobile_generate_key),
    "13": ("3:28205", None),
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

    node = copy.deepcopy(find_top(canvas, node_id))
    if number in _DERIVED:
        builder = _DERIVED[number][1]
        if builder is not None:
            node = builder(node)
    for patch in _PATCHES.get(node_id, []):
        patch(node)
    return node
