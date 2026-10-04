import json, io, sys
sys.stdout.reconfigure(encoding="utf-8")
sys.path.insert(0, "scripts")
import ui_patches as up
canvas = json.load(io.open("docs/ui/.figma-export/Page_1_0-1.json", encoding="utf-8"))
up.load_icon_library(canvas)
node = up.build_board(canvas, "04", "3:27603")
b = up.box(node); ox = float(b["x"])
def chain(n, target, path):
    if n is target: return list(path)
    for c in n.get("children", []) or []:
        r = chain(c, target, path + [n.get("name")])
        if r: return r
    return None
for n in up.walk_all(node):
    nb = up.box(n)
    if nb and float(nb.get("x",0))-ox+float(nb.get("width",0)) > 1200:
        print(chain(n, n, [])[-4:], n.get("name"), nb, repr(n.get("characters")))
