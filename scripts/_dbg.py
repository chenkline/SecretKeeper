import json, io, sys
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, "scripts")
import ui_patches as up
canvas = json.load(io.open("docs/ui/.figma-export/Page_1_0-1.json", encoding="utf-8"))
up.load_icon_library(canvas)
node = up.build_board(canvas, "05", "3:27603")
card = up.find(up.find(node, "导出工作区"), "内容卡片")
for f in [c for c in card["children"] if c.get("name")=="表单字段"]:
    fb = up.box(f)
    print("FIELD", fb)
    for c in f["children"]:
        b = up.box(c)
        print("   ", c.get("name"), c.get("type"), b)
