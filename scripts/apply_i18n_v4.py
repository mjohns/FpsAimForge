#!/usr/bin/env python3
"""Fourth pass: wrap remaining user-visible strings."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = list((ROOT / "src/aim").rglob("*.cc")) + [ROOT / "src/main.cc"]

# std::format("{} Word", icon) -> std::format("{} {}", icon, Tr("Word"))
ICON_BUTTON_WORDS = [
    "Back", "Save", "Settings", "Home", "Stats", "History", "Replay", "Perf",
    "Create", "Update", "Room", "Description", "Folder", "Bundle", "Copy",
    "Delete", "Scenario", "Playlist", "Crosshair", "Layer", "Theme",
    "Sounds folder", "Fps", "Back to editor", "Set description", "Edit description",
]

TEXTFMT_STRINGS = [
    "score: {}",
    "mouse speed: {}",
    "High score: {}",
    "Target score: {}",
    "Layer {}",
    "Point {}",
    "Copy \"{}\" to",
    "Partial run time: {:.1f} hours ({:.0f}%)",
    "Total time: {:.2f}ms",
    "Process events: {:.2f}ms",
    "Event count: {} (mouse={}, max_seen={})",
    "Update time: {:.2f}ms",
    "Render time: {:.2f}ms",
    "Build draw data: {:.2f}ms",
    "Pack instance data: {:.2f}ms",
    "Upload instance data: {:.2f}ms",
    "Upload instance data (copy pass): {:.2f}ms",
    "Upload instance data (memcpy): {:.2f}ms",
    "Render draw data: {:.2f}ms",
    "Start render: {:.2f}ms",
    "Finish render: {:.2f}ms",
    "{} total runs",
    "1 run ({})",
    "{} runs ({})",
    "({})",
    "{} scenarios",
    "{} playlists",
    "Approximate file size: {:.2f}mb",
    "cm/360: {}",
    "{} (High)",
    "init {:.1f}s",
    "load {:.1f}s",
    "db {:.1f}s",
    "sdl {:.1f}s",
    "audio {:.1f}s",
    "window {:.1f}s",
    "scenario count: {}",
    "{} ({} fps)",
    "0ms - {} ({}+ fps)",
]

INPUT_BOOL_STRINGS = [
    "Reflect",
    "Start on floor",
    "Going left",
    "Start clockwise",
    "Only delay on floor",
    "Unghost instead of remove",
    "No partial kills",
    "Newest target is ghost",
]

ERROR_FORMAT_STRINGS = [
    'Bundle name "{}" already exists',
    'Bundle name "{}" is invalid. Can only contains letters, numbers, and _',
    'Theme "{}" already exists',
    'Unable to save theme "{}"',
    'Crosshair "{}" already exists',
    'Unable to save crosshair "{}"',
    'Playlist with name "{}" already exists',
    'Scenario "{}" does not exist.',
    'Scenario "{}" already exists',
    'Unable to find referenced scenario "{}"',
    'Referenced scenario "{}" is invalid.',
    'Delete "{}"?',
    'Delete history for "{}"?',
    'Bundle "{}" is readonly.',
    'Open "{}"',
    'Could not save theme',
    'At time {:.1f}s. Frame {:L}',
    'Target score: {}',
]

ASSIGN_STRINGS = [
    ("Click after sound", 'message = "Click after sound"'),
    ("Too soon", 'message = "Too soon"'),
    ("Audio", 'audio_label = "Audio"'),
    ("Visual", 'visual_label = "Visual"'),
    ("Previous high score", 'prefix = is_new_high ? "Previous high score" : "Current high score"'),
    ("Current high score", None),  # handled above
    ("Previous high", 'prefix = is_new_high ? "Previous high" : "Current high"'),
    ("Current high", None),
]


def wrap_icon_buttons(text: str) -> str:
    pattern = r'std::format\("\{\} ([^"]+)", (icons::\w+)\)'
    def repl(m):
        word = m.group(1)
        icon = m.group(2)
        if word in ("{}", "##") or word.startswith("##") or "Tr(" in m.group(0):
            return m.group(0)
        return f'std::format("{{}} {{}}", {icon}, Tr("{word}"))'
    return re.sub(pattern, repl, text)


def wrap_textfmt(text: str) -> str:
    for s in sorted(TEXTFMT_STRINGS, key=len, reverse=True):
        old = f'ImGui::TextFmt("{s}"'
        if old in text and 'TrFormat("' not in text:
            text = text.replace(old, f'ImGui::Text("%s", TrFormat("{s}"')
            text = re.sub(
                rf'ImGui::Text\("%s", TrFormat\("{re.escape(s)}"([^)]*)\);',
                rf'ImGui::Text("%s", TrFormat("{s}"\1).c_str());',
                text,
            )
    return text


def wrap_input_bool(text: str) -> str:
    for s in INPUT_BOOL_STRINGS:
        old = f'ImGui::InputBool("{s}"'
        if old in text:
            text = text.replace(old, f'ImGui::InputBool(Tr("{s}")')
    return text


def wrap_error_formats(text: str) -> str:
    for s in sorted(ERROR_FORMAT_STRINGS, key=len, reverse=True):
        esc = s.replace('\\', '\\\\').replace('"', '\\"')
        replacements = [
            (f'std::format("{esc}"', f'TrFormat("{esc}"'),
            (f'ImGui::TextFmt("{esc}"', f'ImGui::Text("%s", TrFormat("{esc}"'),
            (f'ImGui::HelpTooltip(std::format("{esc}"', f'ImGui::HelpTooltip(TrFormat("{esc}"'),
            (f'*error_message = std::format("{esc}"', f'*error_message = TrFormat("{esc}"'),
            (f'SetErrorMessage(std::format("{esc}"', f'SetErrorMessage(TrFormat("{esc}"'),
            (f'delete_confirmation_dialog_.NotifyOpen(std::format("{esc}"',
             f'delete_confirmation_dialog_.NotifyOpen(TrFormat("{esc}"'),
            (f'NotifyOpen(std::format("{esc}"', f'NotifyOpen(TrFormat("{esc}"'),
            (f'ImGui::InfoMarker(std::format("{esc}"', f'ImGui::InfoMarker(TrFormat("{esc}"'),
            (f'dialogs->delete_confirmation_dialog.NotifyOpen(std::format("{esc}"',
             f'dialogs->delete_confirmation_dialog.NotifyOpen(TrFormat("{esc}"'),
        ]
        for old, new in replacements:
            if old in text:
                text = text.replace(old, new)
    return text


def wrap_scenario_format(text: str) -> str:
    text = text.replace(
        'std::format("cm/360: {}",',
        'TrFormat("cm/360: {}",',
    )
    return text


def wrap_move_text(text: str) -> str:
    text = text.replace(
        'ImGui::Text("Move \\"%s\\"", scenario_name.c_str());',
        'ImGui::Text("%s \\"%s\\"", Tr("Move"), scenario_name.c_str());',
    )
    return text


def wrap_prefix_assignments(text: str) -> str:
    text = text.replace(
        'prefix = is_new_high ? "Previous high score" : "Current high score"',
        'prefix = is_new_high ? Tr("Previous high score") : Tr("Current high score")',
    )
    text = text.replace(
        'prefix = is_new_high ? "Previous high" : "Current high"',
        'prefix = is_new_high ? Tr("Previous high") : Tr("Current high")',
    )
    text = text.replace(
        'message = "Click after sound"',
        'message = Tr("Click after sound")',
    )
    text = text.replace(
        'message = "Too soon"',
        'message = Tr("Too soon")',
    )
    text = text.replace(
        'const char* audio_label = "Audio"',
        'const char* audio_label = Tr("Audio")',
    )
    text = text.replace(
        'const char* visual_label = "Visual"',
        'const char* visual_label = Tr("Visual")',
    )
    text = text.replace(
        '*error_message = "Could not save theme"',
        '*error_message = Tr("Could not save theme")',
    )
    return text


def add_include(text: str) -> str:
    if "aim/i18n/i18n.h" in text or "src/main.cc" in str(text):
        return text
    if "Tr(" not in text:
        return text
    lines = text.splitlines(keepends=True)
    idx = 0
    for i, line in enumerate(lines):
        if line.startswith("#include"):
            idx = i + 1
    lines.insert(idx, '#include "aim/i18n/i18n.h"\n')
    return "".join(lines)


def process_file(path: Path) -> bool:
    text = path.read_text()
    orig = text
    text = wrap_icon_buttons(text)
    text = wrap_textfmt(text)
    text = wrap_input_bool(text)
    text = wrap_error_formats(text)
    text = wrap_scenario_format(text)
    text = wrap_move_text(text)
    text = wrap_prefix_assignments(text)
    text = add_include(text)
    if text != orig:
        path.write_text(text)
        return True
    return False


def main():
    for path in TARGETS:
        if path.exists() and process_file(path):
            print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
