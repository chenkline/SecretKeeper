import io, sys
sys.stdout.reconfigure(encoding="utf-8")
PATH = "scripts/ui_patches.py"; NL = chr(10)
src = io.open(PATH, encoding="utf-8").read()
old = '''    "06": [lambda r: patch_secret_page_board(r)],
'''
new = '''    "06": [lambda r: patch_secret_page_board(r)],
    "07": [patch_secret_add_page],
    "08": [patch_secret_import_page],
    "09": [patch_security_page],
'''
assert src.count(old) == 1
io.open(PATH, "w", encoding="utf-8", newline=NL).write(src.replace(old, new))
print("ok")
