import json, io, sys
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, "scripts")
import ui_patches as up
print("08 in patches:", "08" in up._PATCHES, up._PATCHES.get("08"))
canvas = json.load(io.open("docs/ui/.figma-export/Page_1_0-1.json", encoding="utf-8"))
up.load_icon_library(canvas)
node = up.build_board(canvas, "08", "3:27493")
print("导航项目 count:", len([c for c in (up.find(up.find(node,"侧边导航"),"导航内容") or {}).get("children",[]) or []]))
na = up.find(node, "导航项目")
print("导航项目 children:", [c.get("name") for c in (na or {}).get("children", []) or []])
for c in (na or {}).get("children", []) or []:
    t = up.find(c, "导航名称")
    print("   ", t.get("characters") if t else None)
