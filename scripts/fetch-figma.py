#!/usr/bin/env python3
"""从 Figma REST API 拉取 SecretKeeper UI 设计稿，导出到 docs/ui/。

凭据从 .local/context.md 读取（已被 .gitignore 忽略），不写入仓库、不打印明文。

用法:
    python scripts/fetch-figma.py                 # 导出全部页面
    python scripts/fetch-figma.py 0:1 3:27821     # 只导出指定节点
    python scripts/fetch-figma.py --list-pages    # 只列出页面与顶层 Frame
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONTEXT_MD = REPO_ROOT / ".local" / "context.md"
OUT_DIR = REPO_ROOT / "docs" / "ui"
API_ROOT = "https://api.figma.com/v1"
FILE_KEY = "bgrtolHhG4Ll1gzTS7XI3r"
DEFAULT_NODE = "3:27821"

# Figma 对 /images 端点限流较紧，请求间隔必须给足，避免 429 后长时间不可用。
IMAGE_INTERVAL_SECONDS = 30


def load_token() -> str:
    if not CONTEXT_MD.exists():
        sys.exit("缺少 .local/context.md，无法读取 FIGMA_TOKEN")
    text = CONTEXT_MD.read_text(encoding="utf-8")
    match = re.search(r"^\|\s*FIGMA_TOKEN\s*\|\s*(\S*)\s*\|\s*$", text, re.MULTILINE)
    if not match:
        sys.exit(".local/context.md 中未找到 FIGMA_TOKEN 条目")
    token = match.group(1).strip()
    if not token:
        sys.exit("FIGMA_TOKEN 为空，请在 .local/context.md 中填写")
    return token


def load_field(name: str, default: str = "") -> str:
    if not CONTEXT_MD.exists():
        return default
    text = CONTEXT_MD.read_text(encoding="utf-8")
    match = re.search(r"^\|\s*%s\s*\|\s*(\S*)\s*\|\s*$" % re.escape(name), text, re.MULTILINE)
    return match.group(1).strip() if match else default


def api_get(path: str, token: str, retries: int = 8) -> dict:
    url = f"{API_ROOT}{path}"
    for attempt in range(retries):
        request = urllib.request.Request(url, headers={"X-Figma-Token": token})
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace")[:400]
            if exc.code == 429 and attempt < retries - 1:
                wait = 60 * (attempt + 1)
                print(f"  触发限流，等待 {wait} 秒后重试 ({attempt + 1}/{retries - 1})")
                time.sleep(wait)
                continue
            sys.exit(f"Figma API 返回 {exc.code}: {detail}")
        except urllib.error.URLError as exc:
            sys.exit(f"无法连接 Figma API: {exc.reason}")
    sys.exit("重试次数耗尽")


def api_get_bytes(url: str, retries: int = 8) -> bytes:
    for attempt in range(retries):
        request = urllib.request.Request(url, headers={"User-Agent": "SecretKeeper-doc-fetch"})
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                return response.read()
        except urllib.error.HTTPError as exc:
            if exc.code == 429 and attempt < retries - 1:
                wait = 60 * (attempt + 1)
                print(f"  图片下载限流，等待 {wait} 秒后重试 ({attempt + 1}/{retries - 1})", flush=True)
                time.sleep(wait)
                continue
            raise
    raise RuntimeError("图片下载重试次数耗尽")


def walk(node: dict, depth: int = 0):
    yield node, depth
    for child in node.get("children", []) or []:
        yield from walk(child, depth + 1)


def collect_geometry(nodes) -> dict:
    """统计每种可见属性的出现频次，用于归纳设计规范。"""
    tally: dict[str, dict] = {}

    def bump(key: str, value) -> None:
        if value in (None, "", 0):
            return
        entry = tally.setdefault(key, {"value": value, "count": 0})
        if entry["value"] != value:
            entry["value"] = value
            entry["count"] = 0
        entry["count"] += 1

    for node, _depth in nodes:
        for key in (
            "absoluteBoundingBox",
            "constraints",
            "layoutMode",
            "itemSpacing",
            "paddingLeft",
            "paddingRight",
            "paddingTop",
            "paddingBottom",
            "cornerRadius",
            "primaryAxisAlignItems",
            "counterAxisAlignItems",
            "opacity",
            "fontSize",
            "fontWeight",
            "lineHeightPx",
            "letterSpacing",
            "textAlignHorizontal",
            "textAlignVertical",
        ):
            value = node.get(key)
            if key in ("absoluteBoundingBox", "constraints"):
                if isinstance(value, dict):
                    for sub, sub_value in value.items():
                        bump(f"{key}.{sub}", sub_value)
            else:
                bump(key, value)

        style = node.get("style") or {}
        for key in (
            "fontFamily",
            "fontPostScriptName",
            "fontWeight",
            "fontSize",
            "lineHeightPx",
            "letterSpacing",
            "textAlignHorizontal",
            "textAlignVertical",
            "fills",
        ):
            value = style.get(key)
            if key == "fills" and isinstance(value, list):
                for fill in value:
                    if fill.get("visible", True) and fill.get("type") == "SOLID":
                        bump("style.fills.color", format_color(fill.get("color")))
            else:
                bump(f"style.{key}", value)

        for fill in node.get("fills", []) or []:
            if fill.get("visible", True) and fill.get("type") == "SOLID":
                bump("fills.color", format_color(fill.get("color")))
        for stroke in node.get("strokes", []) or []:
            if stroke.get("visible", True) and stroke.get("type") == "SOLID":
                bump("strokes.color", format_color(stroke.get("color")))

        effects = node.get("effects", []) or []
        for effect in effects:
            if not effect.get("visible", True):
                continue
            if effect.get("type") == "DROP_SHADOW":
                bump("effects.shadow.color", format_color(effect.get("color")))
                bump("effects.shadow.radius", effect.get("radius"))
                bump("effects.shadow.offset", json.dumps(effect.get("offset"), sort_keys=True))
            elif effect.get("type") == "LAYER_BLUR":
                bump("effects.layerBlur.radius", effect.get("radius"))
            elif effect.get("type") == "BACKGROUND_BLUR":
                bump("effects.backgroundBlur.radius", effect.get("radius"))

    return tally


def format_color(color) -> str:
    if not isinstance(color, dict):
        return None
    red = round(float(color.get("r", 0)) * 255)
    green = round(float(color.get("g", 0)) * 255)
    blue = round(float(color.get("b", 0)) * 255)
    alpha = color.get("a", 1)
    hex_value = f"#{red:02X}{green:02X}{blue:02X}"
    if alpha is not None and abs(float(alpha) - 1.0) > 0.001:
        hex_value += f" / {round(float(alpha) * 100)}%"
    return hex_value


def describe(node: dict) -> str:
    node_type = node.get("type", "?")
    name = node.get("name", "")
    chars = node.get("characters", "")
    detail = ""
    box = node.get("absoluteBoundingBox") or {}
    if box:
        detail = f"{round(box.get('width', 0))}x{round(box.get('height', 0))}"
    text = f"[{node_type}] {name}"
    if detail:
        text += f"  ({detail})"
    if chars:
        text += f"  \"{chars}\""
    return text


def dump_tree(node: dict, lines: list[str], depth: int = 0, max_depth: int = 4) -> None:
    lines.append("  " * depth + describe(node))
    if depth >= max_depth:
        children = node.get("children") or []
        if children:
            lines.append("  " * (depth + 1) + f"... 另有 {len(children)} 个子节点")
        return
    for child in node.get("children") or []:
        if child.get("visible", True) is False:
            continue
        dump_tree(child, lines, depth + 1, max_depth)


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    list_pages = "--list-pages" in sys.argv
    images_only = "--images-only" in sys.argv
    token = load_token()
    file_key = load_field("FIGMA_FILE_KEY", FILE_KEY) or FILE_KEY

    if images_only:
        export_dir = OUT_DIR / ".figma-export"
        screens_dir = OUT_DIR / "screens"
        screens_dir.mkdir(parents=True, exist_ok=True)
        cached = sorted(export_dir.glob("*.json"))
        if not cached:
            sys.exit("未找到缓存 JSON，请先完整拉取一次节点数据")
        pending: list[tuple[str, str]] = []
        for json_path in cached:
            if json_path.name == "manifest.json":
                continue
            document = json.loads(json_path.read_text(encoding="utf-8"))
            for node, depth in walk(document):
                if node.get("type") in ("FRAME", "COMPONENT", "COMPONENT_SET") and depth <= 1:
                    render_id = node.get("id", "").replace(":", "-")
                    render_name = re.sub(r"[^0-9A-Za-z\u4e00-\u9fff_-]+", "_", node.get("name", "frame"))
                    image_path = screens_dir / f"{render_name}_{render_id}.png"
                    if image_path.exists() and image_path.stat().st_size > 0:
                        continue
                    pending.append((node["id"], str(image_path)))
        print(f"待补截图 {len(pending)} 张")
        for index, (node_id, image_path) in enumerate(pending, 1):
            try:
                image = api_get(f"/images/{file_key}?ids={node_id}&format=png&scale=2", token)
                url = (image.get("images") or {}).get(node_id)
                if not url:
                    print(f"  [{index}/{len(pending)}] {node_id} 无图片 URL，跳过")
                    continue
                with open(image_path, "wb") as handle:
                    handle.write(api_get_bytes(url))
                print(f"  [{index}/{len(pending)}] 已保存 {Path(image_path).name}")
            except Exception as exc:  # noqa: BLE001
                print(f"  [{index}/{len(pending)}] 失败 {node_id}: {exc}")
            time.sleep(IMAGE_INTERVAL_SECONDS)
        return 0

    if list_pages:
        data = api_get(f"/files/{file_key}?depth=2", token)
        for page in data.get("document", {}).get("children", []):
            print(f"PAGE  {page['id']}  {page['name']}")
            for frame in page.get("children", []) or []:
                box = frame.get("absoluteBoundingBox") or {}
                print(
                    f"   FRAME {frame['id']:>10}  {frame.get('name', '')}"
                    f"  ({round(box.get('width', 0))}x{round(box.get('height', 0))})"
                )
        return 0

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    screens_dir = OUT_DIR / "screens"
    screens_dir.mkdir(parents=True, exist_ok=True)
    export_dir = OUT_DIR / ".figma-export"
    export_dir.mkdir(parents=True, exist_ok=True)

    targets = args or [DEFAULT_NODE]
    manifest = []

    for node_id in targets:
        print(f"拉取节点 {node_id} ...")
        data = api_get(f"/files/{file_key}/nodes?ids={node_id.replace(chr(32), chr(44))}", token)
        nodes = (data.get("nodes") or {})
        if not nodes:
            sys.exit(f"节点 {node_id} 未返回内容（文件或节点不存在，或 Token 无权访问）")
        for returned_id, wrapper in nodes.items():
            document = wrapper.get("document")
            if document is None:
                print(f"  跳过 {returned_id}：无 document 字段（无权访问？）")
                continue

            safe_name = re.sub(r"[^0-9A-Za-z一-鿿_-]+", "_", document.get("name", "node"))
            base = f"{safe_name}_{returned_id.replace(':', '-')}"
            print(f"  节点名: {document.get('name')}  顶层类型: {document.get('type')}")

            tree_lines: list[str] = []
            dump_tree(document, tree_lines)
            (export_dir / f"{base}.tree.txt").write_text(
                "\n".join(tree_lines) + "\n", encoding="utf-8"
            )

            (export_dir / f"{base}.json").write_text(
                json.dumps(document, ensure_ascii=False, indent=2), encoding="utf-8"
            )

            for node, depth in walk(document):
                if node.get("type") in ("FRAME", "COMPONENT", "COMPONENT_SET") and depth <= 1:
                    render_id = node.get("id", "").replace(":", "-")
                    render_name = re.sub(
                        r"[^0-9A-Za-z一-鿿_-]+", "_", node.get("name", "frame")
                    )
                    image_path = screens_dir / f"{render_name}_{render_id}.png"
                    try:
                        image = api_get(
                            f"/images/{file_key}?ids={node['id']}&format=png&scale=2",
                            token,
                        )
                        url = (image.get("images") or {}).get(node["id"])
                        if not url:
                            continue
                        image_path.write_bytes(api_get_bytes(url))
                        print(f"  已保存截图 {image_path.name}  ({image_path.stat().st_size} 字节)")
                    except Exception as exc:  # noqa: BLE001
                        print(f"  截图失败 {node['id']}: {exc}")

            tally = collect_geometry(walk(document))
            tally_lines = []
            for key in sorted(tally):
                entry = tally[key]
                shown = entry["value"]
                if isinstance(shown, float) and shown == int(shown):
                    shown = int(shown)
                tally_lines.append(f"{key} = {shown}   (出现 {entry['count']} 次)")
            (export_dir / f"{base}.style-tally.txt").write_text(
                "\n".join(tally_lines) + "\n", encoding="utf-8"
            )

            manifest.append(
                {
                    "requested_id": node_id,
                    "returned_id": returned_id,
                    "name": document.get("name"),
                    "type": document.get("type"),
                }
            )

    (export_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(f"完成，导出目录: {export_dir.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())