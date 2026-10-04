import json, io, sys
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, "scripts")
import ui_patches as up
canvas = json.load(io.open("docs/ui/.figma-export/Page_1_0-1.json", encoding="utf-8"))
up.load_icon_library(canvas)
node = up.build_board(canvas, "02", "3:27258")
d = [c for c in up.find(node, "主密钥工作区")["children"] if c.get("name")=="内容卡片"][0]
a = up.find(d, "详情操作")
for b in a["children"]:
    print("BTN", up.box(b))
    for c in (b.get("children") or []):
        print("   ", repr(c.get("name")), c.get("type"), up.box(c))
        for g in (c.get("children") or []):
            print("        ", repr(g.get("name")), g.get("type"), up.box(g))
