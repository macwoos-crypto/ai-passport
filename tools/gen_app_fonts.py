#!/usr/bin/env python3
"""Generate the LVGL font subsets used by the pinyin quiz application.

The interface shows Chinese words *and* toned pinyin (ā á ǎ à, ǖ ǘ ǚ ǜ). Neither is
covered by the bundled Montserrat fonts, so the application ships its own subset of
Noto Sans SC (SIL Open Font License 1.1).

The character inventory is derived from the C string literals of every source file
that main/CMakeLists.txt actually compiles -- not from a hand-maintained list. That
matters: a hand-written list once missed pinyin_quiz.c, so the verdict strings
("这些音你全拿住了") rendered as missing-glyph boxes.

    python3 tools/gen_app_fonts.py            # regenerate
    python3 tools/gen_app_fonts.py --check    # verify coverage (used by the gate)

--check re-derives the inventory and asserts every code point really exists in all
three generated fonts. It does *not* just confirm the files are present: comparing
the inventory against a font generated from that same inventory proves nothing.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_FONT = ROOT / "assets" / "fonts" / "NotoSansSC-Regular.otf"
OUT_DIR = ROOT / "assets" / "fonts"
COMPONENT_CMAKE = ROOT / "main" / "CMakeLists.txt"
SIZES = (16, 22, 32)
FONT_CONVERTER = "lv_font_conv"

STRING_LITERAL_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
# lv_font_conv writes one "/* U+XXXX ... */" comment per included glyph.
GLYPH_COMMENT_RE = re.compile(r"U\+([0-9A-Fa-f]{4,6})")
# Always present: printable ASCII covers digits, the progress counter and punctuation.
BASE_RANGE = range(0x20, 0x7F)


def compiled_sources() -> list[Path]:
    """Every .c that main/CMakeLists.txt compiles, in declaration order.

    The CMake file also mentions the generated font sources; those do not live under
    main/, so is_file() filters them out.
    """
    text = COMPONENT_CMAKE.read_text(encoding="utf-8")
    found: list[Path] = []
    for name in re.findall(r'"([\w./-]+\.c)"', text):
        path = ROOT / "main" / name
        if path.is_file() and path not in found:
            found.append(path)
    return found


def strip_comments(text: str) -> str:
    """Drop C comments so Chinese prose in them does not inflate the inventory."""
    return COMMENT_RE.sub("", text)


def collect_codepoints(sources: list[Path]) -> list[int]:
    chars: set[int] = set(BASE_RANGE)
    for path in sources:
        text = strip_comments(path.read_text(encoding="utf-8"))
        for match in STRING_LITERAL_RE.finditer(text):
            for ch in match.group(1):
                if ch != "\\":
                    chars.add(ord(ch))
    # Space is always required by label layout code paths.
    chars.add(0x20)
    return sorted(chars)


def generated_codepoints(size: int) -> set[int]:
    body = (OUT_DIR / c_source_name(size)).read_text(encoding="utf-8")
    return {int(hex_value, 16) for hex_value in GLYPH_COMMENT_RE.findall(body)}


def c_source_name(size: int) -> str:
    return f"pz_font_{size}.c"


def font_symbol(size: int) -> str:
    return f"pz_font_{size}"


def run_converter(size: int, symbols: str, output: Path) -> None:
    cmd = [
        FONT_CONVERTER,
        "--font", str(SOURCE_FONT),
        "--symbols", symbols,
        "--size", str(size),
        "--bpp", "4",
        "--no-compress",
        "--format", "lvgl",
        "--lv-include", "lvgl.h",
        "--lv-font-name", font_symbol(size),
        "--output", str(output),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"lv_font_conv failed for size {size}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify every inventory code point really exists in the generated fonts",
    )
    args = parser.parse_args()

    if not SOURCE_FONT.is_file():
        print(f"ERROR: missing source font {SOURCE_FONT}", file=sys.stderr)
        return 1

    sources = compiled_sources()
    if not sources:
        print(f"ERROR: no compiled sources found in {COMPONENT_CMAKE}", file=sys.stderr)
        return 1

    codepoints = collect_codepoints(sources)
    symbols = "".join(chr(cp) for cp in codepoints)
    print(f"inventory: {len(codepoints)} code points from {len(sources)} compiled sources")

    if args.check:
        missing_files = [
            name for name in (c_source_name(size) for size in SIZES)
            if not (OUT_DIR / name).is_file()
        ]
        if missing_files:
            print("ERROR: missing generated font(s): " + ", ".join(missing_files),
                  file=sys.stderr)
            return 1

        failed = False
        wanted = set(codepoints)
        for size in SIZES:
            gaps = sorted(wanted - generated_codepoints(size))
            if gaps:
                failed = True
                preview = " ".join(f"U+{cp:04X}" for cp in gaps[:12])
                more = "" if len(gaps) <= 12 else f" (+{len(gaps) - 12} more)"
                print(
                    f"ERROR: {c_source_name(size)} is missing {len(gaps)} code point(s) "
                    f"used by the sources: {preview}{more}\n"
                    f"       re-run: python3 tools/gen_app_fonts.py",
                    file=sys.stderr,
                )
        if failed:
            return 1
        print("generated fonts cover the inventory: OK")
        return 0

    if shutil.which(FONT_CONVERTER) is None:
        print(
            f"ERROR: {FONT_CONVERTER} not found. Install it with "
            "`npm install -g lv_font_conv@1.5.3`",
            file=sys.stderr,
        )
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for size in SIZES:
            target = Path(tmp) / c_source_name(size)
            run_converter(size, symbols, target)
            shutil.copyfile(target, OUT_DIR / c_source_name(size))
            print(f"generated {c_source_name(size)} ({(OUT_DIR / c_source_name(size)).stat().st_size // 1024} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
