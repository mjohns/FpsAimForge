#!/usr/bin/env python3
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Parse English keys from i18n.cc
i18n_cc = (ROOT / "src/aim/i18n/i18n.cc").read_text()
keys = re.findall(r'\{"((?:\\.|[^"\\])*)",', i18n_cc)
keys = sorted(set(keys), key=len, reverse=True)

I18N_KEY_MAP = {
    "i18n_key::kGlobal": '"Global"',
    "i18n_key::kScenario": '"Scenario"',
    "i18n_key::kFire": '"Fire"',
    "i18n_key::kRestartScenario": '"Restart Scenario"',
    "i18n_key::kNextScenario": '"Next Scenario"',
    "i18n_key::kEditScenario": '"Edit Scenario"',
    "i18n_key::kQuickSettings": '"Quick Settings"',
    "i18n_key::kQuickSettingsHelp": '"Hold the key to bring up a settings menu which will close when the key is released. Scroll wheel can be used to adjust mouse sensitivity."',
    "i18n_key::kQuickMetronome": '"Quick Metronome"',
    "i18n_key::kAdjustCrosshairSize": '"Adjust Crosshair Size"',
    "i18n_key::kAdjustCrosshairHelp": '"Hold the key to enable using the scroll wheel to adjust crosshair size."',
    "i18n_key::kSettings": '"Settings"',
    "i18n_key::kLanguage": '"Language"',
    "i18n_key::kLanguageChinese": '"Chinese"',
    "i18n_key::kLanguageEnglish": '"English"',
    "i18n_key::kCmPer360": '"cm/360"',
    "i18n_key::kDpi": '"DPI"',
    "i18n_key::kCmPer360ScrollHelp": '"Adjust within a run by holding \\"s\\" and using the scroll wheel"',
    "i18n_key::kKeybinds": '"Keybinds"',
    "i18n_key::kDpiPrompt": '"What is your mouse DPI?"',
    "i18n_key::kDpiHelp": '"DPI is used to calculate sensitivity given a cm/360 value."',
    "i18n_key::kSet": '"Set"',
    "i18n_key::kClickToStart": '"Click to Start"',
    "i18n_key::kTargetScore": '"Target score"',
    "i18n_key::kStartNewRun": '"Start new run"',
    "i18n_key::kMenuSettings": '"Settings"',
    "i18n_key::kMenuThemes": '"Themes"',
    "i18n_key::kMenuCrosshairs": '"Crosshairs"',
    "i18n_key::kMenuPlayTime": '"Play time"',
    "i18n_key::kMenuReaction": '"Reaction"',
    "i18n_key::kMenuRestart": '"Restart"',
    "i18n_key::kMenuExit": '"Exit"',
}

TARGETS = list((ROOT / "src/aim/ui").glob("*.cc"))
TARGETS += list((ROOT / "src/aim/editor").glob("*.cc"))
TARGETS += [
    ROOT / "src/aim/scenario/scenario.cc",
    ROOT / "src/aim/scenario/replay_viewer.cc",
    ROOT / "src/main.cc",
]
TARGETS += list((ROOT / "src/aim/common").glob("imgui_ext.cc"))


def esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def add_include(text: str) -> str:
    if "aim/i18n/i18n.h" in text:
        return text
    lines = text.splitlines(keepends=True)
    idx = 0
    for i, line in enumerate(lines):
        if line.startswith("#include"):
            idx = i + 1
    lines.insert(idx, '#include "aim/i18n/i18n.h"\n')
    return "".join(lines)


def fix_i18n_key(text: str) -> str:
    for old, new in I18N_KEY_MAP.items():
        text = text.replace(f"Tr({old})", f"Tr({new})")
        text = text.replace(old, new)
    return text


def wrap_literals(text: str) -> str:
    for key in keys:
        if "{" in key or "%" in key:
            # format strings handled separately
            continue
        k = esc(key)
        patterns = [
            (f'ImGui::Text("{k}")', f'ImGui::Text("%s", Tr("{k}"))'),
            (f'ImGui::Button("{k}")', f'ImGui::Button(Tr("{k}"))'),
            (f'ImGui::Selectable("{k}"', f'ImGui::Selectable(Tr("{k}")'),
            (f'ImGui::BeginTabItem("{k}")', f'ImGui::BeginTabItem(Tr("{k}"))'),
            (f'.set_label("{k}")', f'.set_label(Tr("{k}"))'),
            (f'ImGui::HelpMarker("{k}")', f'ImGui::HelpMarker(Tr("{k}"))'),
            (f'ImGui::HelpTooltip("{k}")', f'ImGui::HelpTooltip(Tr("{k}"))'),
            (f'ImGui::InputBool("{k}"', f'ImGui::InputBool(Tr("{k}")'),
        ]
        for old, new in patterns:
            if "Tr(" not in old and old in text:
                text = text.replace(old, new)
    # present mode dropdown
    text = text.replace(
        '{PresentMode::PRESENT_MODE_IMMEDIATE, "Immediate"}',
        '{PresentMode::PRESENT_MODE_IMMEDIATE, Tr("Immediate")}',
    )
    text = text.replace(
        '{PresentMode::PRESENT_MODE_VSYNC, "Vsync"}',
        '{PresentMode::PRESENT_MODE_VSYNC, Tr("Vsync")}',
    )
    text = text.replace(
        '{PresentMode::PRESENT_MODE_MAILBOX, "Mailbox"}',
        '{PresentMode::PRESENT_MODE_MAILBOX, Tr("Mailbox")}',
    )
    return text


def process_file(path: Path) -> bool:
    text = path.read_text()
    orig = text
    text = add_include(text)
    text = fix_i18n_key(text)
    text = wrap_literals(text)
    if text != orig:
        path.write_text(text)
        return True
    return False


def main():
    changed = []
    for path in TARGETS:
        if path.exists() and process_file(path):
            changed.append(path)
    for p in changed:
        print(p.relative_to(ROOT))


if __name__ == "__main__":
    main()
