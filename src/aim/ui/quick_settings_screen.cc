#include "quick_settings_screen.h"

#include <format>
#include <functional>

#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/util.h"
#include "aim/core/settings_manager.h"
#include "aim/ui/settings_screen.h"
#include "aim/ui/ui_screen.h"
#include "backends/imgui_impl_sdl3.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

void DrawCenterTable(const std::string& name,
                     int start_value,
                     int num_rows,
                     std::function<void(float)> value_setter) {
  ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable(name.c_str(), 5, flags)) {
    float char_x = ImGui::GetDefaultCharSizeX();

    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, char_x);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextRow();
    for (int i = 0; i < num_rows; ++i) {
      int val1 = start_value + (10 * i);
      int val2 = val1 + 5;
      int val3 = val1 + (num_rows * 10);
      int val4 = val3 + 5;

      ImVec2 button_sz(-1, 0);
      ImGui::TableNextColumn();
      if (ImGui::Button(std::format("{}", val1), button_sz)) {
        value_setter(val1);
      }
      ImGui::TableNextColumn();
      if (ImGui::Button(std::format("{}", val2), button_sz)) {
        value_setter(val2);
      }

      ImGui::TableNextColumn();

      ImGui::TableNextColumn();
      if (ImGui::Button(std::format("{}", val3), button_sz)) {
        value_setter(val3);
      }
      ImGui::TableNextColumn();
      if (ImGui::Button(std::format("{}", val4), button_sz)) {
        value_setter(val4);
      }
    }
    ImGui::EndTable();
  }
}

class QuickSettingsScreen : public UiScreen {
 public:
  explicit QuickSettingsScreen(const std::string& scenario_name,
                               QuickSettingsType type,
                               const std::string& release_key)
      : UiScreen(),
        scenario_name_(scenario_name),
        updater_(app_.settings_manager().CreateUpdater()),
        type_(type),
        release_key_(release_key) {
    theme_names_ = app_.settings_manager().ListThemes();
    crosshair_names_ = app_.settings_manager().ListCrosshairs();
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) override {
    if (event.type == SDL_EVENT_MOUSE_WHEEL) {
      if (event.wheel.y != 0) {
        if (type_ == QuickSettingsType::DEFAULT) {
          updater_.settings.set_cm_per_360(updater_.settings.cm_per_360() + event.wheel.y);
        }
        if (type_ == QuickSettingsType::METRONOME) {
          updater_.settings.set_enable_metronome(true);
          updater_.settings.set_metronome_bpm(updater_.settings.metronome_bpm() + event.wheel.y);
        }
      }
    }
    if (IsMappableKeyUpEvent(event) && KeyNameMatchesEvent(event, release_key_)) {
      updater_.SaveIfChangesMade(scenario_name_);
      PopSelf();
    }
  }

 protected:
  void OnAttachUi() override {
    if (went_to_settings_) {
      // Returned from full settings so just exit quick settings too.
      PopSelf();
    }
  }

  void DrawScreen() override {
    if (app_.BeginFullscreenWindow()) {
      DrawScreenInternal();
    }
    ImGui::End();
  }

  void DrawScreenInternal() {
    const ScreenInfo& screen = app_.screen_info();
    auto medium_font = app_.font_manager().UseMedium();
    if (ImGui::Button(std::format("{} Settings", icons::kSettings))) {
      PushNextScreen(CreateSettingsScreen(scenario_name_));
      went_to_settings_ = true;
    }

    if (type_ == QuickSettingsType::DEFAULT) {
      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::Spacing();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Themes"));
      // Display a few of the recent themes as direct buttons to click.
      ImGui::LoopId lid;
      for (int i = 0; i < std::min<int>(10, theme_names_.size()); ++i) {
        auto id = lid.Get("Theme");
        if (ImGui::Button(theme_names_[i])) {
          updater_.settings.set_theme_name(theme_names_[i]);
        }
      }

      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::Spacing();
      ImGui::Spacing();

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Crosshairs"));

      // Display a few of the recent crosshairs as direct buttons to click.
      for (int i = 0; i < std::min<int>(8, crosshair_names_.size()); ++i) {
        auto id = lid.Get("Crosshair");
        if (ImGui::Button(crosshair_names_[i])) {
          updater_.settings.set_current_crosshair_name(crosshair_names_[i]);
        }
      }
    }

    ImGui::Columns(3, "SettingsColumns", false);

    float width = screen.width * 0.5;
    float x_start = (screen.width - width) * 0.5;
    ImGui::SetColumnWidth(0, x_start);
    ImGui::SetColumnWidth(1, width);

    ImGui::NextColumn();
    ImVec2 char_size = ImGui::CalcTextSize("A");

    float center_gap = 10;

    ImGui::SetCursorPosY(screen.height * 0.15);

    {
      auto large_font = app_.font_manager().UseLargeBold();
      std::string top_text = type_ == QuickSettingsType::DEFAULT
                                 ? MaybeIntToString(updater_.settings.cm_per_360())
                                 : MaybeIntToString(updater_.settings.metronome_bpm());
      std::string top_help =
          type_ == QuickSettingsType::DEFAULT ? "Scroll to adjust cm/360" : "Scroll to adjust BPM";
      float text_size = ImGui::CalcTextSize(top_text.c_str()).x;
      ImGui::SetCursorPosX((screen.width - text_size) / 2.0);
      ImGui::TextFmt("{}{}", top_text, icons::kHeight);
      auto normal_font = app_.font_manager().UseDefault();
      ImGui::HelpTooltip(top_help);
      normal_font.Pop();
      ImGui::Spacing();
      ImGui::Spacing();
    }

    if (type_ == QuickSettingsType::DEFAULT) {
      int start_value = 10;
      int num_rows = 9;
      DrawCenterTable("SensTable", start_value, num_rows, [&](float val) {
        updater_.settings.set_cm_per_360(val);
      });

      ImGui::Spacing();
      ImGui::Spacing();

      // ImGui::InputFloat(ImGui::InputFloatParams("CmPer360")
      //                       .set_label("cm/360")
      //                       .set_step(1, 10)
      //                       .set_width(char_size.x * 9)
      //                       .set_min(1)
      //                       .set_default(35),
      //                   PROTO_FLOAT_FIELD(Settings, &updater_.settings, cm_per_360));

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Theme"));
      ImGui::SameLine();
      ImGui::SimpleDropdown(
          "ThemeDropdown", updater_.settings.mutable_theme_name(), theme_names_, char_size.x * 20);

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Crosshair"));
      ImGui::SameLine();
      ImGui::SimpleDropdown("CrosshairDropdown",
                            updater_.settings.mutable_current_crosshair_name(),
                            crosshair_names_,
                            char_size.x * 18);

      ImGui::InputBool(Tr("Auto hold tracking"),
                       PROTO_BOOL_FIELD(Settings, &updater_.settings, auto_hold_tracking));
      ImGui::InputBool(Tr("Show health bars"),
          PROTO_BOOL_FIELD(HealthBarSettings, updater_.settings.mutable_health_bar(), show));

      ImGui::InputBool(Tr("Save settings per scenario"),
                       InvertBoolField(PROTO_BOOL_FIELD(
                           Settings, &updater_.settings, disable_per_scenario_settings)));
    }

    if (type_ == QuickSettingsType::METRONOME) {
      float original_bpm = updater_.settings.metronome_bpm();

      int start_value = 70;
      int num_rows = 10;
      DrawCenterTable("MetronomeTable", start_value, num_rows, [&](float val) {
        updater_.settings.set_metronome_bpm(val);
        updater_.settings.set_enable_metronome(true);
      });

      ImGui::Spacing();

      ImGui::InputBool(ImGui::InputBoolParams("EnableMetronome").set_label("Enable metronome"),
                       PROTO_BOOL_FIELD(Settings, &updater_.settings, enable_metronome));

      // ImGui::InputFloat(ImGui::InputFloatParams("MetronomeBpm")
      //                       .set_label("BPM")
      //                       .set_min(0)
      //                       .set_zero_is_unset()
      //                       .set_step(1, 10)
      //                       .set_width(char_size.x * 9),
      //                   PROTO_FLOAT_FIELD(Settings, &updater_.settings, metronome_bpm));

      if (ImGui::Button(Tr("Clear BPM"))) {
        updater_.settings.clear_metronome_bpm();
        updater_.settings.set_enable_metronome(false);
      }

      if (updater_.settings.metronome_bpm() > 0 &&
          updater_.settings.metronome_bpm() != original_bpm) {
        updater_.settings.set_enable_metronome(true);
      }
    }
  }

 private:
  std::string scenario_name_;
  SettingsUpdater updater_;
  QuickSettingsType type_;
  std::vector<std::string> theme_names_;
  std::vector<std::string> crosshair_names_;
  std::string release_key_;
  bool went_to_settings_ = false;
};

}  // namespace

std::unique_ptr<UiScreen> CreateQuickSettingsScreen(const std::string& scenario_name,
                                                    QuickSettingsType type,
                                                    const std::string& release_key) {
  return std::make_unique<QuickSettingsScreen>(scenario_name, type, release_key);
}

}  // namespace aim
