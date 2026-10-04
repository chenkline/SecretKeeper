import io, sys
sys.stdout.reconfigure(encoding="utf-8")
PATH = "scripts/ui_patches.py"; NL = chr(10)
src = io.open(PATH, encoding="utf-8").read()

old = '''    patch_export_original_password(root)
    widen_workspace(root, "导出工作区")'''
new = '''    patch_export_original_password(root)
    widen_workspace(root, "导出工作区")
    # 归位必须放在 widen_workspace 之后：新插入的原密码字段是在加宽前
    # 按 532 宽算的辅助行坐标，卡片通栏后它不会自动跟上。
    for field in walk_all(find(root, "导出工作区") or {}):
        if field.get("name") == "表单字段":
            _fix_field_hint_width(field)'''
assert src.count(old) == 1
io.open(PATH, "w", encoding="utf-8", newline=NL).write(src.replace(old, new))
print("ok")
