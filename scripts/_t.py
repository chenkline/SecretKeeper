import io, sys
sys.stdout.reconfigure(encoding="utf-8")
PATH = "scripts/ui_patches.py"; NL = chr(10)
src = io.open(PATH, encoding="utf-8").read()
# 把 set_button_label 的调用移到详情卡加宽之后
old = '''    patch_desktop_common(root, "主密钥管理")
    set_button_label(root, "详情操作", "导出备份", "导出主密钥", min_width=132.0)

    area = find(root, "主密钥工作区")'''
new = '''    patch_desktop_common(root, "主密钥管理")

    area = find(root, "主密钥工作区")'''
assert src.count(old) == 1
src = src.replace(old, new)

old2 = '''    for node in walk_all(detail):
        if node.get("type") == "TEXT" and node.get("characters", "").startswith(
                "随机生成的主密钥，由主密钥密码派生的 KEK"):
            set_text(node, "随机生成的主密钥，由主密钥密码派生的 KEK 加密后保存在本地。")'''
new2 = '''    for node in walk_all(detail):
        if node.get("type") == "TEXT" and node.get("characters", "").startswith(
                "随机生成的主密钥，由主密钥密码派生的 KEK"):
            set_text(node, "随机生成的主密钥，由主密钥密码派生的 KEK 加密后保存在本地。")

    # 按钮文案最后再改：详情卡加宽会挪动「详情操作」的位置，
    # 先改文案再挪位置会让图标与文字的相对关系错位。
    set_button_label(root, "详情操作", "导出备份", "导出主密钥", min_width=132.0)'''
assert src.count(old2) == 1
src = src.replace(old2, new2)
io.open(PATH, "w", encoding="utf-8", newline=NL).write(src)
print("ok")
