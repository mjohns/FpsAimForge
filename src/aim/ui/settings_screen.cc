#include "settings_screen.h"

#include <format>
#include <functional>

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "aim/audio/sound_manager.h"
#include "aim/common/field.h"
#include "aim/common/files.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/search.h"
#include "aim/core/application.h"
#include "aim/core/displays.h"
#include "aim/core/settings_manager.h"
#include "aim/core/version.h"
#include "aim/proto/common.pb.h"
#include "aim/ui/crosshair_editor_screen.h"
#include "aim/ui/theme_editor_screen.h"
#include "imgui.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace aim {
namespace {

inline const std::vector<std::pair<ScenarioSettingsStoreType, std::string>>
    kScenarioSettingsStoreTypes{
        {ScenarioSettingsStoreType::STORE_GLOBALLY, "Global"},
        {ScenarioSettingsStoreType::STORE_PER_SCENARIO, "Scenario"},
    };

class SoundInputDialog {
 public:
  void NotifyOpen(std::string* sound_name_out, Application& app) {
    sound_name_out_ = sound_name_out;
    sound_names_ = app.sound_manager().ListSounds();
    popup_.Open();
    search_text_ = "";
  }

  bool Draw(Application& app) {
    if (sound_name_out_ == nullptr) {
      return false;
    }
    ImGui::IdGuard cid("SoundInputDialogContent");
    bool selected = false;
    if (popup_.Begin()) {
      float char_x = ImGui::GetDefaultCharSizeX();
      ImGui::SetNextItemWidth(char_x * 40);
      ImGui::InputTextWithHint("##SearchInput", icons::kSearch, &search_text_);

      ImGui::SpacedSeparator();
      float max_height = ImGui::GetIO().DisplaySize.y * 0.4f;
      ImGui::BeginChild("SoundsContent", ImVec2(0, max_height));

      // auto search_words = GetSearchWords(search_text_);

      ImGui::LoopId loop_id;
      for (const std::string& sound_name : sound_names_) {
        if (FuzzyMatch(sound_name, search_text_)) {
          auto lid = loop_id.Get();

          if (ImGui::Button(icons::kPlayArrow)) {
            SoundItem item;
            item.set_name(sound_name);
            app.sound_manager().LoadAndPlaySound(item);
          }
          ImGui::SameLine();
          if (ImGui::Button(sound_name)) {
            *sound_name_out_ = sound_name;
            selected = true;
            popup_.Close();
          }
        }
      }

      ImGui::EndChild();
      ImGui::SpacedSeparator();

      if (ImGui::Button("Cancel")) {
        popup_.Close();
      }

      popup_.End();
    }
    return selected;
  }

 private:
  ImGui::Popup popup_{"SoundInputDialog"};
  std::string* sound_name_out_ = nullptr;
  std::vector<std::string> sound_names_;
  std::string search_text_;
};

struct KeybindItem {
  std::string label;
  std::string help_text;
  KeyMapping* mapping;
  int is_capturing_index = 0;
};

const std::vector<std::pair<PresentMode, std::string>> kPresentModes{
    {PresentMode::PRESENT_MODE_IMMEDIATE, "Immediate"},
    {PresentMode::PRESENT_MODE_VSYNC, "Vsync"},
    {PresentMode::PRESENT_MODE_MAILBOX, "Mailbox"},
};

const std::vector<std::pair<MsaaLevel, std::string>> kMsaaLevels{
    {MsaaLevel::MSAA_LEVEL_2X, "2x"},
    {MsaaLevel::MSAA_LEVEL_4X, "4x"},
    {MsaaLevel::MSAA_LEVEL_8X, "8x"},
    {MsaaLevel::MSAA_LEVEL_OFF, "Disabled"},
};

const char* kQuickSettingsHelpText =
    "Hold the key to bring up a settings menu which will close when the key is released. Scroll "
    "wheel can be used to adjust mouse sensitivity.";
const char* kAdjustCrosshairSizeHelpText =
    "Hold the key to enable using the scroll wheel to adjust crosshair size.";

class SettingsScreen : public UiScreen {
 public:
  explicit SettingsScreen(const std::string& scenario_id)
      : UiScreen(), updater_(app_.settings_manager().CreateUpdater()), scenario_id_(scenario_id) {
    theme_names_ = app_.settings_manager().ListThemes();
    crosshair_names_ = app_.settings_manager().ListCrosshairs();
    Settings settings = app_.settings_manager().GetCurrentSettings();

    keybind_items_ = {
        {"Fire", "", updater_.settings.mutable_keybinds()->mutable_fire()},
        {"Restart Scenario", "", updater_.settings.mutable_keybinds()->mutable_restart_scenario()},
        {"Next Scenario", "", updater_.settings.mutable_keybinds()->mutable_next_scenario()},
        {"Edit Scenario", "", updater_.settings.mutable_keybinds()->mutable_edit_scenario()},
        {"Quick Settings",
         kQuickSettingsHelpText,
         updater_.settings.mutable_keybinds()->mutable_quick_settings()},
        {"Quick Metronome", "", updater_.settings.mutable_keybinds()->mutable_quick_metronome()},
        {"Adjust Crosshair Size",
         kAdjustCrosshairSizeHelpText,
         updater_.settings.mutable_keybinds()->mutable_adjust_crosshair_size()},
    };
  }

 protected:
  void DrawSettings() {
    debug_info_dialog_.Draw(/* can_set= */ false);
    if (!ImGui::BeginTabBar("SettingsTabBar")) {
      return;
    }
    if (ImGui::BeginTabItem("Settings")) {
      ImGui::Spacing();
      ImGui::InputFloat(ImGui::InputFloatParams("CmPer360")
                            .set_label("cm/360")
                            .set_step(1, 5)
                            .set_width(char_x_ * 9)
                            .set_min(1)
                            .set_default(35),
                        PROTO_FLOAT_FIELD(Settings, &updater_.settings, cm_per_360));
      ImGui::SameLine();
      ImGui::HelpMarker("Adjust within a run by holding \"s\" and using the scroll wheel");

      ImGui::InputFloat(ImGui::InputFloatParams("Dpi")
                            .set_label("DPI")
                            .set_step(100, 200)
                            .set_width(char_x_ * 10)
                            .set_min(100)
                            .set_default(800),
                        PROTO_FLOAT_FIELD(Settings, &updater_.settings, dpi));

      ImGui::SpacedSeparator();

      ImGui::InputFloat(ImGui::InputFloatParams("Fps")
                            .set_label("Max render fps")
                            .set_is_optional()
                            .set_step(10, 100)
                            .set_width(char_x_ * 10)
                            .set_range(30, 2000),
                        PROTO_FLOAT_FIELD(Settings, &updater_.settings, max_render_fps));
      ImGui::SameLine();
      ImGui::HelpMarker(
          "State updates and event polling are not tied to fps. A good target can be 2x monitor "
          "refresh rate to reduce tearing.");

      ImGui::AlignTextToFramePadding();
      ImGui::Text("Present mode");
      ImGui::SameLine();
      PresentMode present_mode = updater_.settings.present_mode();
      ImGui::SimpleTypeDropdown("GpuPresentModes", &present_mode, kPresentModes, char_x_ * 10);
      updater_.settings.set_present_mode(present_mode);

      ImGui::AlignTextToFramePadding();
      ImGui::Text("MSAA");
      ImGui::SameLine();
      MsaaLevel msaa_level = updater_.settings.msaa_level();
      if (msaa_level == MsaaLevel::MSAA_LEVEL_UNKNOWN) {
        msaa_level = app_.GetCurrentMsaaLevel();
      }
      ImGui::SimpleTypeDropdown("MsaaLevels", &msaa_level, kMsaaLevels, char_x_ * 9);
      updater_.settings.set_msaa_level(msaa_level);
      ImGui::SameLine();
      ImGui::HelpMarker("Multisample anti-aliasing sample count");
      ImGui::SameLine();
      ImGui::TextDisabled("requires restart");

      ImGui::AlignTextToFramePadding();
      ImGui::Text("Choose display");
      bool has_explicit_display = !updater_.settings.explicit_display_name().empty();
      ImGui::SameLine();
      ImGui::Checkbox("##DisplayCheck", &has_explicit_display);
      if (has_explicit_display) {
        if (!read_display_names_) {
          read_display_names_ = true;
          auto displays = ListDisplays();
          for (const auto& display : displays) {
            display_names_.push_back(display.name);
          }
        }
        if (!display_names_.empty()) {
          std::string& display_name = *updater_.settings.mutable_explicit_display_name();
          if (display_name.empty()) {
            display_name = display_names_[0];
          }
          ImGui::SameLine();
          ImGui::SimpleDropdown(
              "##DisplayNameDropdown", &display_name, display_names_, char_x_ * 30);
          ImGui::SameLine();
          ImGui::TextDisabled("requires restart");
        }
      } else {
        updater_.settings.clear_explicit_display_name();
        ImGui::SameLine();
        ImGui::HelpMarker("By default the monitor with the highest refresh rate is used");
      }

      ImGui::SpacedSeparator();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("Theme");
      ImGui::SameLine();
      ImGui::SimpleDropdown(
          "ThemeDropdown", updater_.settings.mutable_theme_name(), theme_names_, char_x_ * 20);
      ImGui::SameLine();
      if (ImGui::Button(std::format("{}##EditThemes", icons::kEdit))) {
        PushNextScreen(CreateThemeEditorScreen());
      }
      ImGui::HelpTooltip("Open theme editor");

      ImGui::AlignTextToFramePadding();
      ImGui::Text("Crosshair");
      ImGui::SameLine();
      bool crosshair_opened = false;
      ImGui::SimpleDropdown("CrosshairDropdown",
                            updater_.settings.mutable_current_crosshair_name(),
                            crosshair_names_,
                            char_x_ * 15,
                            nullptr,
                            &crosshair_opened);
      if (crosshair_opened) {
        crosshair_names_ = app_.settings_manager().ListCrosshairs();
      }
      ImGui::SameLine();
      if (ImGui::Button(std::format("{}##EditCrosshairs", icons::kEdit))) {
        PushNextScreen(CreateCrosshairEditorScreen());
      }
      ImGui::HelpTooltip("Open crosshair editor");
      ImGui::SameLine();
      if (ImGui::Button(std::format("{}##EditCrosshairColor", icons::kPalette))) {
        ThemeEditorOptions opts;
        opts.selected_theme = updater_.settings.theme_name();
        PushNextScreen(CreateThemeEditorScreen(opts));
      }
      ImGui::HelpTooltip(
          "Edit the color of the crosshair in the current theme. Crosshair colors are stored with "
          "the theme.");

      ImGui::InputFloat(ImGui::InputFloatParams("CrosshairSize")
                            .set_label("Crosshair size")
                            .set_min(0.1)
                            .set_step(0.1, 1)
                            .set_default(15)
                            .set_width(char_x_ * 9),
                        PROTO_FLOAT_FIELD(Settings, &updater_.settings, crosshair_size));
      ImGui::SameLine();
      ImGui::HelpMarker("Adjust within a run by holding \"c\" and using the scroll wheel");

      ImGui::SpacedSeparator();

      ImGui::InputBool("Save settings per scenario",
                       InvertBoolField(PROTO_BOOL_FIELD(
                           Settings, &updater_.settings, disable_per_scenario_settings)));

      ImGui::InputBool("Auto hold tracking",
                       PROTO_BOOL_FIELD(Settings, &updater_.settings, auto_hold_tracking));

      ImGui::SpacedSeparator();

      ImGui::InputBool(
          "Use \"Click to Start\"",
          InvertBoolField(PROTO_BOOL_FIELD(Settings, &updater_.settings, disable_click_to_start)));

      ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Start countdown time")
                            .set_min(0.01)
                            .set_is_optional()
                            .set_default(0.4)
                            .set_step(0.01, 0.1)
                            .set_width(char_x_ * 10),
                        PROTO_FLOAT_FIELD(Settings, &updater_.settings, start_countdown_time));
      ImGui::SameLine();
      ImGui::HelpMarker("A countdown will be shown whenever a scenario is about to start");

      ImGui::SpacedSeparator();
      ImGui::InputBool(ImGui::InputBoolParams("EnableMetronome").set_label("Enable metronome"),
                       PROTO_BOOL_FIELD(Settings, &updater_.settings, enable_metronome));
      ImGui::InputFloat(ImGui::InputFloatParams("MetronomeBpm")
                            .set_label("Metronome BPM")
                            .set_min(0)
                            .set_zero_is_unset()
                            .set_step(1, 5)
                            .set_width(char_x_ * 10),
                        PROTO_FLOAT_FIELD(Settings, &updater_.settings, metronome_bpm));
      ImGui::SameLine();
      ImGui::HelpMarker("Adjust within a run by holding \"b\" and using the scroll wheel");

      ImGui::SpacedSeparator();

      ImGui::InputBool(
          ImGui::InputBoolParams("ShowHealthBars").set_label("Show health bars"),
          PROTO_BOOL_FIELD(HealthBarSettings, updater_.settings.mutable_health_bar(), show));
      if (updater_.settings.health_bar().show()) {
        ImGui::Indent();

        ImGui::InputBool(
            ImGui::InputBoolParams("OnlyDamaged").set_label("Only damaged"),
            PROTO_BOOL_FIELD(
                HealthBarSettings, updater_.settings.mutable_health_bar(), only_damaged));

        ImGui::InputFloat(
            ImGui::InputFloatParams::WithLabelAsId("Size")
                .set_min(0.1)
                .set_max(3.0)
                .set_step(0.1, 1)
                .set_default(1)
                .set_width(char_x_ * 9),
            PROTO_FLOAT_FIELD(HealthBarSettings, updater_.settings.mutable_health_bar(), size));

        ImGui::Unindent();
      }

      ImGui::SpacedSeparator();

      ImGui::InputBool(ImGui::InputBoolParams("DisableReplays").set_label("Disable replays"),
                       PROTO_BOOL_FIELD(Settings, &updater_.settings, disable_replays));

      ImGui::SpacedSeparator();

      if (ImGui::Button(std::format("{} Folder", icons::kOpenInNew))) {
        OpenFolderInExplorer(app_.file_system().GetUserDataPath());
      }
      ImGui::HelpTooltip(std::format("Open \"{}\"", app_.file_system().GetUserDataPath().string()));

      if (ImGui::Button(std::format("{} Info", icons::kSmartToy))) {
        debug_info_dialog_.NotifyOpen(app_.GetDebugInfoString());
      }

      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Keybinds")) {
      ImGui::Spacing();
      DrawKeybinds();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Sounds")) {
      ImGui::Spacing();
      DrawSounds();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Scenario Settings")) {
      ImGui::Spacing();
      DrawScenarioSettingsConfig();
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  void DrawKeybinds() {
    if (ImGui::BeginTable("KeybindsColumns", 2)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, char_x_ * 20);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

      ImGui::LoopId loop_id;
      for (KeybindItem& item : keybind_items_) {
        auto lid = loop_id.Get("KeyMapItem");

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text(item.label);

        if (item.help_text.size() > 0) {
          ImGui::SameLine();
          ImGui::HelpMarker(item.help_text);
        }

        ImGui::TableNextColumn();
        float entry_width = char_x_ * 10;
        ImGui::SameLine();
        KeyMappingEntry(&item, 1, entry_width);
        ImGui::SameLine();
        ImGui::Text("  ");
        ImGui::SameLine();
        KeyMappingEntry(&item, 2, entry_width);
        ImGui::SameLine();
        ImGui::Text("  ");
        ImGui::SameLine();
        KeyMappingEntry(&item, 3, entry_width);
        ImGui::SameLine();
        ImGui::Text("  ");
        ImGui::SameLine();
        KeyMappingEntry(&item, 4, entry_width);
      }
      ImGui::EndTable();
    }
  }

  void DrawScenarioSettingsConfig() {
    ImGui::InputBool("Save settings per scenario",
                     InvertBoolField(PROTO_BOOL_FIELD(
                         Settings, &updater_.settings, disable_per_scenario_settings)));
    ImGui::SameLine();
    ImGui::HelpMarker(
        "If disabled, the config below will be ignored and the same settings will always be used "
        "for every scenario.");
    ImGui::SpacedSeparator();

    ScenarioSettingsConfig& config = *updater_.settings.mutable_scenario_settings_config();

    float char_x = ImGui::GetDefaultCharSizeX();
    auto draw_item = [&](const std::string& name,
                         Field<ScenarioSettingsStoreType> type_field,
                         bool default_global = false) {
      ImGui::IdGuard cid(name);
      ImGui::AlignTextToFramePadding();
      ImGui::Text(name);
      ImGui::SameLine();
      auto type = type_field.get();
      if (!type_field.has()) {
        type = default_global ? ScenarioSettingsStoreType::STORE_GLOBALLY
                              : ScenarioSettingsStoreType::STORE_PER_SCENARIO;
      }
      ImGui::SimpleTypeDropdown("##TypeSelector", &type, kScenarioSettingsStoreTypes, char_x * 12);
      type_field.set(type);
    };

    ImGui::Text("Choose which fields are stored uniquely for each scenario");
    ImGui::SameLine();
    ImGui::HelpMarker(
        "\"Scenario\" means this setting will be saved per scenario. \"Global\" means that all "
        "scenarios share the same value.");

    ImGui::Indent();
    draw_item("cm/360",
              PROTO_FIELD(ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, cm_per_360));
    draw_item("Theme",
              PROTO_FIELD(ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, theme_name));
    draw_item(
        "Crosshair",
        PROTO_FIELD(ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, crosshair_name));
    draw_item(
        "Crosshair size",
        PROTO_FIELD(ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, crosshair_size));
    draw_item(
        "Auto hold tracking",
        PROTO_FIELD(ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, auto_hold_tracking),
        /*default_global=*/true);
    draw_item("Health bar",
              PROTO_FIELD(ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, health_bar));
    draw_item(
        "Enable metronome",
        PROTO_FIELD(ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, enable_metronome));
    draw_item(
        "Metronome BPM",
        PROTO_FIELD(ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, enable_metronome));
    draw_item(
        "Tracking shots per second",
        PROTO_FIELD(
            ScenarioSettingsStoreType, ScenarioSettingsConfig, &config, tracking_shots_per_second),
        /*default_global=*/true);
    ImGui::Unindent();
  }

  void DrawSounds() {
    SoundSettings& s = *updater_.settings.mutable_sounds();

    sound_input_dialog_.Draw(app_);

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Tracking shots per second")
                          .set_step(0.5, 5)
                          .set_width(char_x_ * 9)
                          .set_is_optional()
                          .set_min(0.1)
                          .set_default(9),
                      PROTO_FLOAT_FIELD(Settings, &updater_.settings, tracking_shots_per_second));

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Proximity tracking shots per second");
    bool has_proximity_sounds = updater_.settings.has_proximity_max_shots_per_second() ||
                                updater_.settings.has_proximity_min_shots_per_second();
    ImGui::SameLine();
    ImGui::Checkbox("##HasProximitySounds", &has_proximity_sounds);

    if (has_proximity_sounds) {
      ImGui::Indent();
      ImGui::InputFloat(
          ImGui::InputFloatParams::WithLabelAsId("Slow")
              .set_step(0.5, 5)
              .set_width(char_x_ * 9)
              .set_min(0.1)
              .set_default(4),
          PROTO_FLOAT_FIELD(Settings, &updater_.settings, proximity_min_shots_per_second));
      ImGui::InputFloat(
          ImGui::InputFloatParams::WithLabelAsId("Fast")
              .set_step(0.5, 5)
              .set_width(char_x_ * 9)
              .set_min(0.1)
              .set_default(10),
          PROTO_FLOAT_FIELD(Settings, &updater_.settings, proximity_max_shots_per_second));
      ImGui::Unindent();
    } else {
      updater_.settings.clear_proximity_min_shots_per_second();
      updater_.settings.clear_proximity_max_shots_per_second();
    }

    ImGui::SpacedSeparator();

    float char_x = ImGui::GetDefaultCharSizeX();
    {
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Volume level");
      ImGui::SameLine();
      float volume_level = 1;
      if (s.has_master_volume_level()) {
        volume_level = s.master_volume_level();
      }
      ImGui::SetNextItemWidth(35 * char_x);
      ImGui::SliderFloat("##VolumeLevel", &volume_level, 0, 2, "%.2f");
      s.set_master_volume_level(volume_level);
    }

    ImGui::SpacedSeparator();

    if (ImGui::BeginTable("SoundsColumns", 2)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, char_x_ * 10);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

      auto sound_input = [this, char_x](const std::string& label, SoundItem* item) {
        ImGui::IdGuard cid(label);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGui::AlignTextToFramePadding();
        ImGui::Text(label);

        std::string* sound_name = item->mutable_name();

        ImGui::TableNextColumn();
        if (ImGui::Button(icons::kPlayArrow)) {
          app_.sound_manager().LoadAndPlaySound(*item);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(25 * char_x);
        ImGui::InputText("##SoundNameInput", sound_name);

        ImGui::SameLine();
        if (ImGui::Button(icons::kEdit)) {
          sound_input_dialog_.NotifyOpen(sound_name, app_);
        }

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Volume level");
        ImGui::SameLine();

        bool has_volume_level = item->has_volume_level();
        ImGui::Checkbox("##VolumLevel", &has_volume_level);
        if (has_volume_level) {
          float volume_level = item->has_volume_level() ? item->volume_level() : 1.0f;
          ImGui::SameLine();
          ImGui::SetNextItemWidth(char_x * 16);
          ImGui::SliderFloat("##VolumeLevel", &volume_level, 0, 2, "%.2f");
          item->set_volume_level(volume_level);
        } else {
          item->clear_volume_level();
        }

        ImGui::SameLine();
        ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Pitch modifier")
                              .set_is_optional()
                              .set_range(0.01, 100)
                              .set_step(0.02, 0.2)
                              .set_width(char_x * 12)
                              .set_default(1),
                          PROTO_FLOAT_FIELD(SoundItem, item, pitch_modifier));
      };

      sound_input("Click miss", s.mutable_click_miss());
      sound_input("Click hit", s.mutable_click_hit());
      sound_input("Click kill", s.mutable_click_kill());

      sound_input("Tracking miss", s.mutable_tracking_miss());
      sound_input("Tracking hit", s.mutable_tracking_hit());
      sound_input("Tracking kill", s.mutable_tracking_kill());

      sound_input("Metronome", s.mutable_metronome());
      sound_input("Reload", s.mutable_reload());

      ImGui::EndTable();
    }

    ImGui::SpacedSeparator();

    auto sounds_folder = app_.file_system().GetUserDataPath("resources/sounds");
    if (ImGui::Button(std::format("{} Sounds folder", icons::kOpenInNew))) {
      OpenFolderInExplorer(sounds_folder);
    }
    ImGui::HelpTooltip(std::format("Open \"{}\"", sounds_folder.string()));
  }

  void DrawScreen() override {
    ImGui::IdGuard cid("SettingsScreen");
    ImVec2 char_size = ImGui::CalcTextSize("A");
    char_x_ = char_size.x;

    DrawTopBar();

    if (BeginMainWindow("MainWindow", 0.8)) {
      DrawSettings();
    }
    ImGui::End();
  }

  void DrawControls() {
    {
      ImVec2 sz = ImVec2(char_x_ * 14, 0.0f);
      if (ImGui::Button("Save", sz)) {
        app_.settings_manager().MarkDirty();
        updater_.SaveIfChangesMade(scenario_id_);
        PopSelf();
      }
    }
    {
      ImGui::SameLine();
      ImVec2 sz = ImVec2(0, 0.0f);
      if (ImGui::Button("Cancel", sz)) {
        PopSelf();
      }
    }
  }

  void KeyMappingEntry(KeybindItem* item, int i, float width) {
    ImGui::IdGuard cid(std::format("KeymapEntry{}", i));
    std::string value;
    if (item->is_capturing_index == i) {
      value = "...";
    } else if (i == 1) {
      value = item->mapping->mapping1();
    } else if (i == 2) {
      value = item->mapping->mapping2();
    } else if (i == 3) {
      value = item->mapping->mapping3();
    } else if (i == 4) {
      value = item->mapping->mapping4();
    }

    if (ImGui::Button(value, ImVec2(width, 0))) {
      for (KeybindItem& other_item : keybind_items_) {
        other_item.is_capturing_index = 0;
      }

      item->is_capturing_index = i;
      capture_key_fn_ = [item, i](const SDL_Event& event) {
        std::string key_value = SDL_GetKeyName(event.key.key);
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          key_value = GetMouseButtonName(event.button.button);
        }
        if (i == 1) {
          item->mapping->set_mapping1(key_value);
        } else if (i == 2) {
          item->mapping->set_mapping2(key_value);
        } else if (i == 3) {
          item->mapping->set_mapping3(key_value);
        } else if (i == 4) {
          item->mapping->set_mapping4(key_value);
        }
        item->is_capturing_index = 0;
      };
    }
    ImGui::SameLine();
    if (ImGui::IconButton(icons::kClear)) {
      if (i == 1) {
        item->mapping->set_mapping1("");
      } else if (i == 2) {
        item->mapping->set_mapping2("");
      } else if (i == 3) {
        item->mapping->set_mapping3("");
      } else if (i == 4) {
        item->mapping->set_mapping4("");
      }
    }
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) override {
    bool is_capturable =
        event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    if (capture_key_fn_ && is_capturable) {
      auto capture = capture_key_fn_;
      capture_key_fn_ = {};
      capture(event);
    }
  }

 private:
  void DrawTopBar() {
    float width = char_x_ * 12.9;
    float middle = app_.screen_info().width / 2.0;
    // ImGui::SetNextWindowBgAlpha();
    ImGui::SetNextWindowPos(ImVec2(middle - width / 2.0, char_x_ / 3.0));
    ImGui::SetNextWindowSize(ImVec2(width, -1));
    ImGui::Begin("TopBar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    float start_x = ImGui::GetCursorPosX();

    if (ImGui::Button(std::format("{} Save", icons::kSave))) {
      app_.settings_manager().MarkDirty();
      updater_.SaveIfChangesMade(scenario_id_);
      PopSelf();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      PopSelf();
    }

    ImGui::End();
  }

  bool BeginMainWindow(const std::string& name, float width_multiple) {
    float padding = char_x_ * 0.3;
    float start_y = ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() * 0.9;
    float end_y = app_.screen_info().height - padding;
    float width = app_.screen_info().width * width_multiple;
    float height = end_y - start_y;

    ImGui::SetNextWindowPos(ImVec2((app_.screen_info().width - width) / 2.0, start_y));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    return ImGui::Begin(
        name.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
  }

  void Line() {
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();
  }

  SettingsUpdater updater_;
  std::vector<std::string> theme_names_;
  std::vector<std::string> crosshair_names_;
  const std::string scenario_id_;

  std::function<void(const SDL_Event&)> capture_key_fn_;
  std::vector<KeybindItem> keybind_items_;
  float char_x_ = 0;

  int edit_crosshair_index_ = 0;
  SoundInputDialog sound_input_dialog_;

  bool read_display_names_ = false;
  std::vector<std::string> display_names_;

  ImGui::MultilineTextEntryDialog debug_info_dialog_{"DebugInfoDialog"};
};

}  // namespace

std::unique_ptr<UiScreen> CreateSettingsScreen(const std::string& current_scenario_id) {
  return std::make_unique<SettingsScreen>(current_scenario_id);
}

}  // namespace aim
