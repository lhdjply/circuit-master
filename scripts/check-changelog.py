#!/usr/bin/env python3
"""检查更新日志：已经发布过的版本段落不允许再被改动。

为什么需要这个脚本：记录新改动时很容易「插到第一个 ### 修复 下面」，结果写进了
已经发布的版本。这类错误在 diff 里几乎看不出来，只有回头翻更新日志才会发现
（本项目已经踩过三次）。

判定办法：对 CHANGELOG.md 里每个 `## [vX.Y.Z]` 段落，找到同名 git tag，把工作区
里的段落与 tag 那一版逐字比对。tag 还不存在的版本（刚写、还没发）跳过。

用法：
    python3 scripts/check-changelog.py          # 检查
    python3 scripts/check-changelog.py --list    # 顺便列出各段落状态

退出码：0 = 没问题，1 = 有已发布段落被改动 / 缺「未发布」段落
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

FILES = ("CHANGELOG.md", "CHANGELOG.en.md")
UNRELEASED = ("未发布", "Unreleased")

# 已知的历史例外：这些版本发过 tag 之后又改过文案（当年改描述留下的）。
# 只承认这几条，新改动一律不要往已发布段落里写。
KNOWN_EDITED = {"v0.2.0"}


def git(*args: str) -> str:
    return subprocess.run(["git", *args], capture_output=True, text=True).stdout


def sections(text: str) -> list[tuple[str, str]]:
    """按 `## [xxx]` 切段落，返回 [(名字, 段落正文)]"""
    marks = [(m.start(), m.group(1)) for m in re.finditer(r"^## \[([^\]]+)\]", text, re.M)]
    out = []
    for i, (pos, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(text)
        out.append((name, text[pos:end]))
    return out


def normalise(body: str) -> str:
    """比对前统一空白：行尾空格、结尾空行都不算改动"""
    lines = [line.rstrip() for line in body.strip().split("\n")]
    while lines and not lines[-1]:
        lines.pop()
    return "\n".join(lines)


def first_difference(a: str, b: str) -> str:
    """给一句能看懂的差异说明"""
    la, lb = a.split("\n"), b.split("\n")
    for i in range(min(len(la), len(lb))):
        if la[i] != lb[i]:
            return f"第 {i + 1} 行不同：\n      现在：{la[i][:70]}\n      发布：{lb[i][:70]}"
    if len(la) > len(lb):
        return f"多出 {len(la) - len(lb)} 行，第一行是：{la[len(lb)][:70]}"
    return f"少了 {len(lb) - len(la)} 行"


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    list_only = "--list" in sys.argv
    failures = 0

    for name in FILES:
        path = root / name
        if not path.exists():
            print(f"{name}：找不到这个文件", file=sys.stderr)
            failures += 1
            continue

        current = sections(path.read_text())
        if not any(key in UNRELEASED for key, _ in current):
            print(f"{name}：缺少「## [未发布]」段落", file=sys.stderr)
            failures += 1

        for key, body in current:
            if key in UNRELEASED:
                continue
            tag = key if key.startswith("v") else f"v{key}"
            if not git("rev-parse", "-q", "--verify", f"refs/tags/{tag}").strip():
                if list_only:
                    print(f"  {name}  [{key}] 没有 tag {tag}，跳过")
                continue

            released = git("show", f"{tag}:{name}")
            if not released.strip():
                # 那个 tag 里还没有这份文件（英文版是后来才加的），无从比对
                continue
            released_body = dict(sections(released)).get(key)
            if released_body is None:
                # 段落名与 tag 当时不同（例如后来拆分/改名），也跳过
                continue

            if tag in KNOWN_EDITED:
                if list_only:
                    print(f"  {name}  [{key}] 已知的历史例外，跳过")
                continue

            if normalise(body) != normalise(released_body):
                print(f"{name}  [{key}] 与 tag {tag} 发布时不一致：", file=sys.stderr)
                print("      " + first_difference(normalise(body), normalise(released_body)),
                      file=sys.stderr)
                failures += 1
            elif list_only:
                print(f"  {name}  [{key}] 未改动 ✓")

    if failures:
        print(f"\n发现 {failures} 处问题：已发布的版本段落不该再改，"
              f"新改动请写到「## [未发布]」下面。", file=sys.stderr)
        return 1
    print("更新日志：已发布段落未改动 ✓")
    return 0


if __name__ == "__main__":
    sys.exit(main())
