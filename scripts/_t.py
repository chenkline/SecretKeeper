# -*- coding: utf-8 -*-
import io, sys
sys.stdout.reconfigure(encoding="utf-8")
PATH = "scripts/ui_patches.py"
NL = chr(10)
src = io.open(PATH, encoding="utf-8").read()

start = src.index("def build_mobile_generate_key(source: dict) -> dict:")
end = src.index("def _mobile_pwd(")

new = '''def build_mobile_generate_key(source: dict) -> dict:
    """生成不带 TAB 的「生成主密钥」画板。

    需求把移动端的生成与导入拆成两个独立界面，各自不该再带
    「生成主密钥 / 从文件导入」这组切换标签——页面标题已经说明了
    这是哪一步，留着标签只会让人以为还能切回去。
    """
    board = copy.deepcopy(source)
    board["id"] = "sk-mobile-generate-key"
    board["name"] = "\\u79fb\\u52a8\\u7aef \\u00b7 \\u751f\\u6210\\u4e3b\\u5bc6\\u94a5"

    content = find(board, "\\u9875\\u9762\\u5185\\u5bb9")
    if content is None:
        return board
    drop(board, "\\u6dfb\\u52a0\\u65b9\\u5f0f")

    x = float(box(content).get("x", 0)) + 20
    w = float(box(content).get("width", 390)) - 40
    top = 268.0

    title = text_node("\\u6807\\u9898", "\\u751f\\u6210\\u4e3b\\u5bc6\\u94a5", x, top - 56.0, 20, INK, width=w)
    subtitle = text_node("\\u8bf4\\u660e", "\\u5728\\u672c\\u673a\\u751f\\u6210\\u4e00\\u628a\\u968f\\u673a RSA-2048 \\u4e3b\\u5bc6\\u94a5\\u3002",
                         x, top, 13, MUTED, width=w)
    fields = [
        _mobile_pwd(x, top + 35.0, w, "\\u4e3b\\u5bc6\\u94a5\\u540d\\u79f0\\uff08\\u53ef\\u9009\\uff09", "\\u53ef\\u4ee5\\u7559\\u7a7a",
                    "\\u540d\\u79f0\\u4ec5\\u7528\\u4e8e\\u8fa8\\u8bc6\\uff0c\\u4e0d\\u53c2\\u4e0e\\u52a0\\u5bc6\\u3002"),
        _mobile_pwd(x, top + 132.0, w, "\\u4e3b\\u5bc6\\u94a5\\u5bc6\\u7801", "\\u2022" * 11, None),
        _mobile_pwd(x, top + 229.0, w, "\\u786e\\u8ba4\\u4e3b\\u5bc6\\u94a5\\u5bc6\\u7801", "\\u2022" * 11, None),
    ]
    warning = _mobile_notice(x, top + 326.0, w,
        "\\u8bf7\\u59a5\\u5584\\u8bb0\\u4f4f\\u4e3b\\u5bc6\\u94a5\\u5bc6\\u7801",
        "\\u5bc6\\u7801\\u4e22\\u5931\\u65e0\\u6cd5\\u89e3\\u5bc6\\uff0c\\u5bc6\\u5323\\u65e0\\u6cd5\\u4e3a\\u4f60\\u91cd\\u7f6e\\u5bc6\\u7801\\u3002")
    action = _mobile_button(x, top + 402.0, w, "\\u751f\\u6210\\u4e3b\\u5bc6\\u94a5", ACCENT, WHITE, 168)

    content["children"] = [title, subtitle] + fields + [warning, action]
    cb = box(content)
    cb["height"] = 268.0 + 402.0 + 44.0 - float(cb.get("y", 0))
    _shrink_board(board)
    return board


def _shrink_board(board: dict) -> None:
    """画板高度按内容收紧，并让底部固定块贴住新底边。"""
    content = find(board, "\\u9875\\u9762\\u5185\\u5bb9")
    if content is None:
        return
    cb = box(content)
    bottom = float(cb.get("y", 0)) + float(cb.get("height", 0))
    reflow_mobile(board, bottom + 16.0 + 72.0, ("\\u7cfb\\u7edf\\u624b\\u52bf\\u533a\\u57df", "\\u5e95\\u90e8\\u5bfc\\u822a"))
    bb = box(board)
    nav = find(board, "\\u9875\\u9762\\u5bfc\\u822a")
    top = float(box(nav).get("y", 0)) if nav is not None else float(bb.get("y", 0))
    bb["height"] = float(bb.get("y", 0)) + 844.0 - float(bb.get("y", 0))


'''
src = src[:start] + new + src[end:]
io.open(PATH, "w", encoding="utf-8", newline=NL).write(src)
print("ok")
