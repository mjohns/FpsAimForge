#!/usr/bin/env python3
"""Second pass: wrap HelpMarker, HelpTooltip, set_label, std::format nav strings."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = list((ROOT / "src/aim").rglob("*.cc")) + [ROOT / "src/main.cc"]

def add_tr_to_quoted_call(text: str, func: str) -> str:
    pattern = rf'{func}\("((?:\\.|[^"\\])*)"\)'
    def repl(m):
        s = m.group(1)
        if s.startswith("##") or s.startswith("%"):
            return m.group(0)
        # look back for existing Tr(
        start = m.start()
        if start >= 3 and text[start - 3:start] == "Tr(":
            return m.group(0)
        return f'{func}(Tr("{s}"))'
    return re.sub(pattern, repl, text)

def fix_format_nav(text: str) -> str:
    pairs = [
        ("Playlists", "icons::kList"),
        ("Scenarios", "icons::kCenterFocusWeak"),
        ("Bundles", "icons::kAutoAwesomeMotion"),
        ("Last run", "icons::kAssignment"),
        ("Start new run", "icons::kPlayArrow"),
    ]
    for word, icon in pairs:
        old = 'std::format("{} ' + word + '", ' + icon + ')'
        new = 'std::format("{} {}", ' + icon + ', Tr("' + word + '"))'
        text = text.replace(old, new)
    return text

def fix_imgui_text_format(text: str) -> str:
    # ImGui::Text("fps: %d", ...) -> ImGui::Text(Tr("fps: %d"), ...)
    pattern = r'ImGui::Text\("((?:[^"\\]|\\.)*%[^"]*)"\s*,'
    def repl(m):
        s = m.group(1)
        if "Tr(" in m.group(0):
            return m.group(0)
        return f'ImGui::Text(Tr("{s}"),'
    return re.sub(pattern, repl, text)

def fix_buttons(text: str) -> str:
    for label in ["Save", "Cancel", "Copy", "Add", "English"]:
        old = f'ImGui::Button("{label}"'
        new = f'ImGui::Button(Tr("{label}")'
        if old in text:
            text = text.replace(old, new)
    return text

def process(text: str) -> str:
    for func in ["HelpMarker", "HelpTooltip"]:
        text = add_tr_to_quoted_call(text, func)
    text = fix_format_nav(text)
    text = fix_imgui_text_format(text)
    text = fix_buttons(text)
    # set_label("Foo") -> set_label(Tr("Foo")) but not ## or X/Y/Z
    text = re.sub(
        r'\.set_label\("((?:\\.|[^"\\])*)"\)',
        lambda m: f'.set_label(Tr("{m.group(1)}"))' if not m.group(1).startswith("#") and m.group(1) not in ("X", "Y", "Z", "") else m.group(0),
        text,
    )
    return text

def main():
    for path in TARGETS:
        text = path.read_text()
        new = process(text)
        if new != text:
            path.write_text(new)
            print(path.relative_to(ROOT))

if __name__ == "__main__":
    main()
