#!/usr/bin/env python3
"""SecretKeeper - 分层依赖门禁。

断言 core 四层与 UI 层的依赖方向不被回潮：

    UI(app)  ->  core/service
    core/service -> {core/store, core/crypto}
    core/store   ->  core/serialize
    core/serialize 与 core/crypto 互不依赖

输出必须全 ASCII（AGENTS.md 9.1：CI 上控制台代码页不保证 UTF-8）。
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# UI 层唯一可见的 core 头文件。platform.h 与 view_model.h 不属于 core。
UI_ALLOWED_CORE_HEADERS = {"core/service.h"}

# 每个 core 源文件允许包含的 core 头文件。
ALLOWED = {
    "src/common/core/crypto.cpp": {"core/crypto.h"},
    "src/common/core/serialize.cpp": {"core/serialize.h"},
    "src/common/core/store.cpp": {"core/store.h"},
    "src/common/core/service.cpp": {
        "core/service.h",
        "core/_error.h",
        "core/backoff.h",
        "core/crypto.h",
        "core/kek_cache.h",
        "core/serialize.h",
        "core/store.h",
        "core/_text.h",
    },
    "src/common/core/_error.cpp": {"core/_error.h", "core/service.h"},
    "src/common/core/_text.cpp": {"core/_text.h"},
    "src/common/core/_hex.cpp": {"core/store.h"},
    "src/common/core/_file_store.cpp": {"core/store.h"},
    "src/common/core/kek_cache.cpp": {"core/kek_cache.h", "core/crypto.h"},
    "src/common/core/backoff.cpp": {"core/backoff.h"},
}

UI_SOURCES = ["src/app/app.cpp", "src/app/view_model.cpp"]

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)

failures = []


def includes_of(path):
    with io.open(path, "r", encoding="utf-8") as handle:
        return INCLUDE_RE.findall(handle.read())


def main():
    for path in sorted(set(list(ALLOWED) + UI_SOURCES)):
        full = os.path.join(ROOT, path)
        if not os.path.isfile(full):
            failures.append("%s: missing" % path)
            continue
        allowed = ALLOWED.get(path, UI_ALLOWED_CORE_HEADERS if path in UI_SOURCES else set())
        for inc in includes_of(full):
            if not inc.startswith("core/"):
                continue
            if inc not in allowed:
                failures.append("%s includes %s (allowed: %s)"
                                % (path, inc, ", ".join(sorted(allowed)) or "none"))

    # crypto and serialize must not know about each other, in either direction.
    for left, right in (("crypto", "serialize"), ("serialize", "crypto")):
        src = os.path.join(ROOT, "src/common/core/%s.cpp" % left)
        with io.open(src, "r", encoding="utf-8") as handle:
            body = handle.read()
        if ("core/%s.h" % right) in body:
            failures.append("core/%s.cpp references core/%s.h" % (left, right))

    for problem in failures:
        sys.stdout.write("  FAIL  %s\n" % problem)
    sys.stdout.write("\n%d violations\n" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())