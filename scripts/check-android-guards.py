#!/usr/bin/env python3
"""检查 Android 专属代码块里用到的成员，在对应头文件里确实声明过。

为什么需要这个：`#if defined(Q_OS_ANDROID)` 里的代码在桌面构建时根本不编译，
所以「成员只在桌面那边声明」这种错误本地跑 ctest 完全发现不了 —— 只能在
Android CI 上炸一次才知道（m_mobileBar 就栽在这上面）。

做法很简单：把每个 .cpp 里 Android 区块的内容抠出来，找出所有 m_xxx 之类的
成员名，逐个到同名 .h 里找声明。宁可粗糙，也比等 CI 强。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# 只看这两个方向的区块：Android 专属代码
OPEN_ANDROID = re.compile(r"^#\s*if\s+defined\(Q_OS_ANDROID\)")
ANY_IF = re.compile(r"^\s*#\s*if")
ENDIF = re.compile(r"^\s*#\s*endif")
MEMBER = re.compile(r"\b(m_[A-Za-z0-9_]+)\b")
# 注释里的名字不算声明：先把 // 和 /* */ 去掉再找
LINE_COMMENT = re.compile(r"//[^\n]*")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)


def strip_literals(text: str) -> str:
    """去掉字符串与字符字面量，免得里面的花括号影响计数。"""
    text = re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)
    return re.sub(r"'(?:[^'\\]|\\.)*'", "''", text)


def brace_balance(text: str) -> int:
    text = strip_literals(strip_comments(text))
    return text.count("{") - text.count("}")


def strip_comments(text: str) -> str:
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def android_blocks(text: str) -> list[str]:
    """把 `#if defined(Q_OS_ANDROID)` 到配对的 #endif 之间的内容抠出来。"""
    blocks: list[str] = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        if OPEN_ANDROID.match(lines[i].lstrip()):
            depth = 1
            j = i + 1
            chunk: list[str] = []
            while j < len(lines) and depth > 0:
                if ANY_IF.match(lines[j]):
                    depth += 1
                elif ENDIF.match(lines[j]):
                    depth -= 1
                    if depth == 0:
                        break
                chunk.append(lines[j])
                j += 1
            blocks.append("\n".join(chunk))
            i = j
        i += 1
    return blocks


def main() -> int:
    problems: list[str] = []
    for cpp in sorted((ROOT / "src").rglob("*.cpp")):
        header = cpp.with_suffix(".h")
        if not header.exists():
            continue
        header_text = strip_comments(header.read_text(encoding="utf-8"))
        source = cpp.read_text(encoding="utf-8")
        for block in android_blocks(source):
            # 花括号必须配平：这段代码桌面构建时被预处理掉，桌面编译器看不见它，
            # 多一个 } 会让它提前关掉 namespace，报一堆「MainWindow 未声明」
            # 头文件不许放进 Android 区块：那样桌面编译看不见，只在 Android 炸
            # （m_mobileBar、QButtonGroup 两次都是这么栽的）
            for line in block.splitlines():
                if line.lstrip().startswith("#include"):
                    problems.append(f"{cpp.relative_to(ROOT)}: Android 区块里出现 "
                                    f"{line.strip()}，头文件应当放到文件顶部")

            balance = brace_balance(block)
            if balance != 0:
                problems.append(f"{cpp.relative_to(ROOT)}: Android 区块的花括号不配平"
                                f"（多了 {abs(balance)} 个 {'}' if balance < 0 else '{'}）")
            for name in sorted(set(MEMBER.findall(block))):
                if not re.search(rf"\b{name}\b", header_text):
                    problems.append(f"{cpp.relative_to(ROOT)}: Android 区块用到了 {name}，"
                                    f"但 {header.name} 里没有声明")

    if problems:
        print("Android 专属代码有问题：")
        for item in problems:
            print("  " + item)
        print("\n这类错误桌面构建不会报（那段代码不编译），只会在 Android CI 上炸。")
        return 1
    print("Android 专属代码块的成员引用检查通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
