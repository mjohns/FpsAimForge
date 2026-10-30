#!/usr/bin/env python3
"""Wrap common ImGui user-facing string literals with Tr()."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGET_DIRS = [
    ROOT / "src/aim/ui",
    ROOT / "src/aim/editor",
    ROOT / "src/aim/scenario",
    ROOT / "src",
]

INCLUDE = '#include "aim/i18n/i18n.h"\n'

SKIP_SUBSTR = ("##", "icons::", "kAim", "SDL_", "ImGui", "PROTO_", "ObjectType", "%", "{}")

TEXT_RE = re.compile(r'ImGui::Text\("([^"\\]+)"\)')
TEXT_FMT_RE = re.compile(r'ImGui::TextFmt\("([^"\\]+)"')
BUTTON_RE = re.compile(r'ImGui::Button\("([^"\\]+)"\)')
SELECTABLE_RE = re.compile(r'ImGui::Selectable\("([^"\\]+)"')
TAB_RE = re.compile(r'ImGui::BeginTabItem\("([^"\\]+)"\)')
LABEL_RE = re.compile(r'\.set_label\("([^"\\]+)"\)')
HELP_MARKER_RE = re.compile(r'ImGui::HelpMarker\("([^"\\]+)"\)')
HELP_TOOLTIP_RE = re.compile(r'ImGui::HelpTooltip\("([^"\\]+)"\)')


def needs_skip(s: str) -> bool:
    if not s or not re.search(r"[A-Za-z]", s):
        return True
    return any(x in s for x in SKIP_SUBSTR)


def wrap(m: re.Match, fmt: str) -> str:
    s = m.group(1)
    if needs_skip(s):
        return m.group(0)
    return fmt.format(s)


def process(content: str) -> str:
    if "aim/i18n/i18n.h" not in content:
        # insert after last #include block
        lines = content.splitlines(keepends=True)
        last_inc = 0
        for i, line in enumerate(lines):
            if line.startswith("#include"):
                last_inc = i + 1
        lines.insert(last_inc, INCLUDE)
        content = "".join(lines)

    content = TEXT_RE.sub(lambda m: wrap(m, 'ImGui::Text("%s", Tr("{}"))'), content)
    content = TEXT_FMT_RE.sub(lambda m: wrap(m, 'ImGui::TextFmt(Tr("{}")'), content)
    content = BUTTON_RE.sub(lambda m: wrap(m, 'ImGui::Button(Tr("{}"))'), content)
    content = SELECTABLE_RE.sub(lambda m: wrap(m, 'ImGui::Selectable(Tr("{}"))'), content)
    content = TAB_RE.sub(lambda m: wrap(m, 'ImGui::BeginTabItem(Tr("{}"))'), content)
    content = LABEL_RE.sub(lambda m: wrap(m, '.set_label(Tr("{}"))'), content)
    content = HELP_MARKER_RE.sub(lambda m: wrap(m, 'ImGui::HelpMarker(Tr("{}"))'), content)
    content = HELP_TOOLTIP_RE.sub(lambda m: wrap(m, 'ImGui::HelpTooltip(Tr("{}"))'), content)

    # migrate old i18n_key usage
    content = re.sub(r'Tr\(i18n_key::k(\w+)\)', lambda m: f'Tr("PLACEHOLDER_{m.group(1)}")', content)
    return content


def main():
    for d in TARGET_DIRS:
        if not d.exists():
            continue
        for path in d.rglob("*.cc"):
            if "i18n/" in str(path):
                continue
            text = path.read_text()
            new = process(text)
            if new != text:
                path.write_text(new)
                print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
