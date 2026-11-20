#!/usr/bin/env python3
"""从源码里抽取 CSM_TR("...") 的文案，更新 resources/translations 下的 .ts。

为什么不用 lupdate：lupdate 只认 tr() / QCoreApplication::translate() 这些固定
写法，认不出我们的 CSM_TR 宏（它没有对应的 -tr-function-alias 可用）。
这个脚本按同一条约定（CSM_TR("英文原文")）抽取，简单可预测。

用法：
    python3 scripts/update-translations.py          # 更新并写回
    python3 scripts/update-translations.py --check   # 只检查有没有漏翻

行为：
  * 已存在的条目保留原译文（按 source 文本匹配）
  * 新出现的条目加进去，译文留空（界面会退回英文）
  * 源码里已删掉的条目从 .ts 移除
  * 同时生成 circuit-master_en.ts 作为「源语言」对照表
"""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
TS_DIR = ROOT / "resources" / "translations"
CONTEXT = "csm"

# CSM_TR("a") / CSM_TR("a" "b" "c") —— 相邻字面量在预处理阶段会拼成一个
CALL = re.compile(r"CSM_TR\(\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+)\)", re.S)
LITERAL = re.compile(r"\"((?:[^\"\\]|\\.)*)\"")


def unescape(text: str) -> str:
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "\\" and i + 1 < len(text):
            nxt = text[i + 1]
            out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\", "'": "'"}.get(nxt, nxt))
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def collect() -> dict[str, list[str]]:
    """返回 {英文原文: [出现位置, ...]}"""
    found: dict[str, list[str]] = {}
    for path in sorted(list(SRC.rglob("*.cpp")) + list(SRC.rglob("*.h"))):
        text = path.read_text(encoding="utf-8")
        for match in CALL.finditer(text):
            parts = [unescape(p) for p in LITERAL.findall(match.group(1))]
            if not parts:
                continue
            source = "".join(parts)
            found.setdefault(source, []).append(str(path.relative_to(ROOT)))
    return found


def load_ts(path: Path) -> tuple[dict[str, str], str]:
    """读出现有 .ts：{source: translation} 与 language 属性"""
    if not path.exists():
        return {}, "zh_CN"
    tree = ET.parse(path)
    root = tree.getroot()
    messages: dict[str, str] = {}
    for ctx in root.findall("context"):
        for msg in ctx.findall("message"):
            src = msg.findtext("source") or ""
            tr = msg.find("translation")
            messages[src] = (tr.text or "") if tr is not None else ""
    return messages, root.get("language", "zh_CN")


def write_ts(path: Path, language: str, messages: dict[str, str], header: str) -> None:
    # XML 注释里不许出现连续两个短横线，否则整份 .ts 解析不了
    header = header.replace("--", "－").replace("－?-", "－") if False else header
    lines = [header.rstrip(), f'<TS version="2.1" language="{language}" sourcelanguage="en">',
             "<context>", f"    <name>{CONTEXT}</name>"]
    for source in sorted(messages):
        translation = messages[source]
        lines.append("    <message>")
        lines.append(f"        <source>{escape(source)}</source>")
        lines.append(f"        <translation>{escape(translation)}</translation>")
        lines.append("    </message>")
    lines += ["</context>", "</TS>", ""]
    path.write_text("\n".join(lines), encoding="utf-8")


def escape(text: str) -> str:
    return (text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


ZH_HEADER = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<!--
  中文翻译表。源码里的文案是英文（CSM_TR("Open File")），这里是中文。

  改完不用手动编译：构建时 lrelease 会把它编成 .qm 嵌进资源。
  源码里新增文案后跑一次：
      python3 scripts/update-translations.py
  （或构建 update_translations 目标）
  新条目会以空译文加入，界面在这些地方退回英文。
-->"""

EN_HEADER = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<!--
  英文对照表。英文就是源码里的原文，所以译文与原文相同 ——
  这个文件主要用于：核对有没有漏抽的文案、给其他语言做参照。

  与 circuit-master_zh_CN.ts 一样由 scripts/update-translations.py 生成。
-->"""


def main() -> int:
    TS_DIR.mkdir(parents=True, exist_ok=True)
    found = collect()

    zh_path = TS_DIR / "circuit-master_zh_CN.ts"
    existing, _ = load_ts(zh_path)

    kept = {src: existing[src] for src in found if src in existing}
    added = [src for src in found if src not in existing]
    dropped = [src for src in existing if src not in found]
    untranslated = [src for src, tr in kept.items() if not tr.strip()]

    if "--check" in sys.argv:
        # 顺带核对两份 CHANGELOG 的版本小节是否对齐
        def headings(path: Path) -> list[str]:
            text = path.read_text(encoding="utf-8") if path.exists() else ""
            # 只比版本小节；「未发布 / Unreleased」各自用母语写，不参与比较
            return [line.split("]")[0] + "]" for line in text.splitlines()
                    if line.startswith("## [v")]

        zh_heads = headings(ROOT / "CHANGELOG.md")
        en_heads = headings(ROOT / "CHANGELOG.en.md")
        if zh_heads != en_heads:
            print("CHANGELOG.md 与 CHANGELOG.en.md 的版本小节不一致：")
            print("  中文：", zh_heads)
            print("  英文：", en_heads)
            return 1

        print(f"文案 {len(found)} 条：已译 {len(kept) - len(untranslated)}，"
              f"未译 {len(untranslated)}，源码里有但表里没有 {len(added)}")
        for src in added:
            print("  新增未录入：", src)
        return 1 if (added or untranslated) else 0

    messages = {src: kept.get(src, "") for src in found}
    write_ts(zh_path, "zh_CN", messages, ZH_HEADER)

    # 英文对照表：源语言，译文即原文
    write_ts(TS_DIR / "circuit-master_en.ts", "en", {src: src for src in found}, EN_HEADER)

    print(f"文案 {len(found)} 条")
    print(f"  保留译文 {len(kept) - len(untranslated)} 条，未译 {len(untranslated)} 条")
    if added:
        print(f"  新增 {len(added)} 条（译文留空）：")
        for src in added:
            print("    ", src.split("\n")[0][:70])
    if dropped:
        print(f"  移除已删除的 {len(dropped)} 条")
    return 0


if __name__ == "__main__":
    sys.exit(main())
