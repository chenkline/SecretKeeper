#!/usr/bin/env python3
"""同步 vendor/argon2 下的 Argon2 参考实现。

仅在需要升级 Argon2 版本时使用。日常开发不需要运行本脚本。

用法：
    python scripts/vendor-argon2.py --version 20190702

脚本行为：
    1. 从 GitHub 下载指定 tag 的源码包
    2. 只挑选需要的文件（不引入 Makefile、测试、文档、latex 等）
    3. 保留 opt.c 与 ref.c 两者，由各平台构建脚本二选一

不会修改已有文件的内容，只做整体替换。替换后必须重新生成黄金向量并
人工确认，见 vendor/argon2/README.md。
"""

import argparse
import io
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request

REPO = "P-H-C/phc-winner-argon2"
DEST = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "vendor", "argon2",
)

# (上游相对路径, 本地相对路径)
FILES = [
    ("include/argon2.h", "include/argon2.h"),
    ("src/argon2.c", "src/argon2.c"),
    ("src/core.c", "src/core.c"),
    ("src/core.h", "src/core.h"),
    ("src/encoding.c", "src/encoding.c"),
    ("src/encoding.h", "src/encoding.h"),
    ("src/thread.c", "src/thread.c"),
    ("src/thread.h", "src/thread.h"),
    ("src/ref.c", "src/ref.c"),
    ("src/opt.c", "src/opt.c"),
    ("src/blake2/blake2.h", "src/blake2/blake2.h"),
    ("src/blake2/blake2-impl.h", "src/blake2/blake2-impl.h"),
    ("src/blake2/blake2b.c", "src/blake2/blake2b.c"),
    ("src/blake2/blamka-round-ref.h", "src/blake2/blamka-round-ref.h"),
    ("src/blake2/blamka-round-opt.h", "src/blake2/blamka-round-opt.h"),
    ("LICENSE", "LICENSE"),
]


def download(version: str) -> bytes:
    url = f"https://github.com/{REPO}/archive/refs/tags/{version}.tar.gz"
    print(f"下载 {url}")
    with urllib.request.urlopen(url, timeout=120) as resp:
        return resp.read()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--version", required=True, help="上游 tag，例如 20190702")
    args = ap.parse_args()

    blob = download(args.version)
    root = f"phc-winner-argon2-{args.version}"

    with tempfile.TemporaryDirectory() as tmp:
        with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tf:
            members = {}
            for m in tf.getmembers():
                rel = os.path.relpath(m.name, root)
                if rel.startswith("..") or m.isdir():
                    continue
                members[rel.replace("\\", "/")] = m
            for src, _ in FILES:
                if src not in members:
                    print(f"错误：上游包中缺少 {src}", file=sys.stderr)
                    return 1
            for src, dst in FILES:
                m = members[src]
                data = tf.extractfile(m).read()
                out = os.path.join(DEST, dst)
                os.makedirs(os.path.dirname(out), exist_ok=True)
                # 统一为 LF 行尾与 UTF-8，避免平台间产生无谓差异
                data = data.replace(b"\r\n", b"\n")
                with open(out, "wb") as f:
                    f.write(data)
                print(f"  写入 {dst}（{len(data)} 字节）")

    print()
    print("完成。请按 vendor/argon2/README.md 的要求重新生成并核对黄金向量。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
