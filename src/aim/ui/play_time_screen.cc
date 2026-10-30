#include "play_time_screen.h"

#include <format>

#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/core/play_time_manager.h"
#include "aim/ui/editor/scenario_editor_common.h"
#include "aim/i18n/i18n.h"
// i18n: Tr() returns translated printf-style literals; the format string is
// non-literal by design, validated against the translation table in i18n.cc.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
#endif

namespace aim {
namespace {

class PlayTimeScreen : public UiScreen {
 public:
  explicit PlayTimeScreen() : UiScreen() {}

 protected:
  void DrawScreen() override {
    BeginMainWindow("PlayTimeView");
    DrawPlayTimeScreen();
    ImGui::End();
  }

 private:
  void DrawPlayTimeScreen() {
    ImGui::IdGuard cid("PlayTime");
    if (ImGui::Button(std::format("{} Back", icons::kArrowBack))) {
      PopSelf();
    }
    ImGui::SpacedSeparator();
    auto play_times = app_.play_time_manager().GetPlayTime();

    float total_play_time_seconds =
        play_times.total.complete_run_time_seconds + play_times.total.partial_run_time_seconds;
    float total_partial_play_time_seconds = play_times.total.partial_run_time_seconds;
    ImGui::Spacing();
    ImGui::Text(Tr("Total time: %.1f hours"), total_play_time_seconds / 3600.0f);
    ImGui::Text("%s", TrFormat("Partial run time: {:.1f} hours ({:.0f}%)", total_partial_play_time_seconds / 3600.0f,
                   (total_partial_play_time_seconds / total_play_time_seconds) * 100).c_str());
    ImGui::SameLine();
    ImGui::HelpMarker(Tr("Total time spent on runs that are restarted before completion"));

    ImGui::SpacedSeparator();

    auto sort_and_print_values = [](std::vector<std::pair<int, std::string>>& values) {
      absl::c_sort(values);
      for (const auto& entry : std::views::reverse(values)) {
        ImGui::Text("%s", TrFormat("{}: {:.1f} hours", entry.second, entry.first / 3600.0f).c_str());
      }
    };

    ImGui::Text("%s", Tr("By shot type"));
    ImGui::Indent();
    const std::unordered_map<ShotType::TypeCase, PlayTimes>& by_shot_type =
        play_times.play_times_by_shot_type;

    std::vector<std::pair<int, std::string>> by_shot_types;
    for (const auto& entry : kShotTypes) {
      auto it = by_shot_type.find(entry.first);
      if (it != by_shot_type.end()) {
        by_shot_types.emplace_back(
            it->second.complete_run_time_seconds + it->second.partial_run_time_seconds,
            entry.second);
      }
    }
    sort_and_print_values(by_shot_types);

    ImGui::Unindent();

    ImGui::SpacedSeparator();

    ImGui::Text("%s", Tr("By cm/360"));
    ImGui::Indent();
    const std::unordered_map<int, PlayTimes>& by_cm_per_360 = play_times.play_times_by_cm_per_360;
    std::vector<std::pair<int, std::string>> by_cm_per_360s;
    for (const auto& entry : by_cm_per_360) {
      by_cm_per_360s.emplace_back(
          entry.second.complete_run_time_seconds + entry.second.partial_run_time_seconds,
          std::format("{}cm", entry.first));
    }
    sort_and_print_values(by_cm_per_360s);
    ImGui::Unindent();
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) override {
    if (event.key.key == SDLK_ESCAPE) {
      PopSelf();
    }
  }

  bool BeginMainWindow(const std::string& name) {
    float width = app_.screen_info().width * 0.6;
    float height = app_.screen_info().height * 0.8;

    ImGui::SetNextWindowPos(ImVec2((app_.screen_info().width - width) / 2.0,
                                   (app_.screen_info().height - height) / 2.0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    return ImGui::Begin(
        name.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
  }
};

}  // namespace

std::unique_ptr<UiScreen> CreatePlayTimeScreen() {
  return std::make_unique<PlayTimeScreen>();
}

}  // namespace aim
