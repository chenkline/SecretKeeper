import io, sys
sys.stdout.reconfigure(encoding="utf-8")
PATH = "scripts/ui_patches.py"; NL = chr(10)
src = io.open(PATH, encoding="utf-8").read()
old = '''    for sub in walk_all(slot):
        if sub.get("type") == "TEXT":
            box(sub)["width"] = card_w - 200.0
    pick = next((c for c in slot.get("children", []) or []
                 if c.get("name") == "操作按钮"), None)
    if pick is not None:
        pb = box(pick)
        shift(pick, card_x + card_w - float(pb.get("x", 0)) - float(pb.get("width", 0)), 0)
'''
new = '''    pick = next((c for c in slot.get("children", []) or []
                 if c.get("name") == "操作按钮"), None)
    if pick is not None:
        # 只加宽文件信息文本，按钮是 HUG 宽度，不能跟着撑开。
        for text_node in slot.get("children", []) or []:
            if text_node.get("type") == "TEXT":
                box(text_node)["width"] = card_w - 200.0
        pb = box(pick)
        shift(pick, card_x + card_w - float(pb.get("x", 0)) - float(pb.get("width", 0)), 0)
'''
assert src.count(old) == 1
io.open(PATH, "w", encoding="utf-8", newline=NL).write(src.replace(old, new))
print("ok")
