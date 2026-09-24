#include "top_bar.h"

#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/core/scenario_manager.h"
#include "aim/graphics/textures.h"
#include "aim/ui/crosshair_editor_screen.h"
#include "aim/ui/play_time_screen.h"
#include "aim/ui/reaction_time_screen.h"
#include "aim/ui/select_variation_dialog.h"
#include "aim/ui/settings_screen.h"
#include "aim/ui/theme_editor_screen.h"
#include "aim/ui/ui_screen.h"
#include "imgui.h"

namespace aim {
namespace {

class TopBarImpl : public TopBar {
 public:
  void Draw() override {
    ImGui::IdGuard cid("TopBar");

    std::string selected_scenario_name;
    if (select_variation_dialog_.Draw(&selected_scenario_name)) {
      app_.scenario_manager().SetCurrentScenario(selected_scenario_name);
    }

    int large_font_size = app_.font_manager().large_font_size();
    ImGui::BeginChild("Header", ImVec2(0, large_font_size * 1.3));
    if (app_.logo_texture().is_loaded()) {
      int size = app_.font_manager().large_font_size();
      ImGui::Image(app_.logo_texture().GetImTextureId(),
                   ImVec2(size + 6, size + 6),
                   ImVec2(0.0f, 0.0f),
                   ImVec2(1.0f, 1.0f));

      // Draw circle around logo.
      // ImGui::DebugDrawItemRect();
      auto min = ImGui::GetItemRectMin();
      auto max = ImGui::GetItemRectMax();

      float radius = (max.x - min.x) / 2.0;

      auto center_x = min.x + radius;
      auto center_y = min.y + radius;

      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      float thickness = 3;
      auto white = IM_COL32(230, 230, 230, 255);
      draw_list->AddCircle(
          ImVec2(center_x, center_y), radius - (thickness / 2.0), white, 400, thickness);
      ImGui::SameLine();
    }
    {
      auto font = app_.font_manager().UseLargeBold();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("FpsAimForge");
    }

    auto font = app_.font_manager().UseLarge();
    auto current_scenario = app_.scenario_manager().GetCurrentScenario();
    if (current_scenario) {
      float available_height = ImGui::GetContentRegionAvail().y + ImGui::GetCursorPosY();
      float button_height = ImGui::GetFrameHeight();

      ImGui::SameLine();
      float y_off = (available_height - button_height) / 2.0f;
      if (y_off > 0) {
        // ImGui::Dummy(ImVec2(0, y_off));
        //  TODO: This vertical centering is not working.
        // ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_off);
      }
      ImGui::AlignTextToFramePadding();
      ImGui::Text("                ");
      ImGui::SameLine();

      float frame_height = ImGui::GetFrameHeight();
      ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, frame_height / 2.0f);
      if (ImGui::IconButton(icons::kPlayArrow, 0.9)) {
        app_.state().scenario_run_option = ScenarioRunOption::START_CURRENT;
        app_.GetCurrentScreen()->ReturnHome();
      }
      ImGui::PopStyleVar();

      ImGui::SameLine();
      ImGui::Text(current_scenario->name + "  ");

      ImGui::SameLine();
      if (ImGui::IconButton(icons::kTune)) {
        select_variation_dialog_.NotifyOpen(current_scenario->name);
      }
      {
        auto normal_font = app_.font_manager().UseDefault();
        ImGui::HelpTooltip("Select scenario variation");
      }
    }

    ImGui::SameLine();

    bool open_settings = false;
    const char* menu_id = "top_bar_menu";
    if (ImGui::BeginPopup(menu_id)) {
      auto normal_font = app_.font_manager().UseDefault();
      // if (ImGui::Selectable(std::format("{} Settings", icons::kSettings).c_str())) {
      //   open_settings = true;
      // }
      if (ImGui::Selectable(std::format("{} Themes", icons::kPalette).c_str(), false)) {
        app_.PushNextScreen(CreateThemeEditorScreen());
      }
      if (ImGui::Selectable(std::format("{} Crosshairs", icons::kMyLocation).c_str(), false)) {
        app_.PushNextScreen(CreateCrosshairEditorScreen());
      }
      if (ImGui::Selectable(std::format("{} Play time", icons::kHourglassEmpty).c_str(), false)) {
        app_.PushNextScreen(CreatePlayTimeScreen());
      }
      if (ImGui::Selectable(std::format("{} Reaction", icons::kTimer).c_str(), false)) {
        app_.PushNextScreen(CreateReactionTimeScreen());
      }

      ImGui::SpacedSeparator();

      if (ImGui::Selectable(std::format("{} Restart", icons::kRefresh).c_str())) {
        app_.RequestRestart();
      }

      if (ImGui::Selectable(std::format("{} Exit", icons::kLogout).c_str())) {
        // Show a screen to confirm?
        app_.RequestExit();
      }

      normal_font.Pop();
      ImGui::EndPopup();
    }
    ImGui::SetCursorAtRight(ImGui::GetDefaultCharSizeX() * 4);
    if (ImGui::SelectableButton(icons::kSettings)) {
      open_settings = true;
    }

    // ImGui::SameLine();
    // ImGui::Text(" ");

    ImGui::SameLine();
    if (ImGui::SelectableButton(icons::kMoreVert)) {
      ImGui::OpenPopup(menu_id);
    }

    if (open_settings) {
      std::string current_scenario_name = current_scenario ? current_scenario->name : "";
      app_.PushNextScreen(CreateSettingsScreen(current_scenario_name));
    }

    font.Pop();
    ImGui::EndChild();
  }

 private:
  Application& app_ = GetUiApp();
  SelectVariationDialog select_variation_dialog_{"TopBarSelectScenarioVariation"};
};
}  // namespace

std::unique_ptr<TopBar> CreateTopBar() {
  return std::make_unique<TopBarImpl>();
}

}  // namespace aim
