#include "reaction_time_screen.h"

#include <format>

#include "absl/strings/ascii.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/util.h"
#include "aim/core/application.h"
#include "aim/core/settings_manager.h"
#include "aim/graphics/renderer.h"
#include "imgui/backends/imgui_impl_sdl3.h"

namespace aim {
namespace {

struct ReactionTimeOptions {
  bool is_audio = false;
  float min_time = 1.0f;
  float max_time = 3.0f;
};

struct ReactionTimeResult {
  i64 reaction_micros = -1;
  bool click_too_early = false;
};

class SingleReactionTimeScreen : public Screen {
 public:
  SingleReactionTimeScreen(Application* app,
                           const Settings& settings,
                           const ReactionTimeOptions& options,
                           ReactionTimeResult* result)
      : Screen(*app), settings_(settings), result_(result), options_(options) {
    app->SetPresentMode(settings.present_mode());
  }

  void OnEvents(std::span<SDL_Event> events) override {
    for (const SDL_Event& event : events) {
      if (IsQuitEvent(event)) {
        app_.RequestExit();
      }
      OnEvent(event);
    }
  }

  void OnEvent(const SDL_Event& event) {
    if (IsEscapeKeyDown(event)) {
      PopSelf();
    }
    if (IsMappableKeyDownEvent(event)) {
      bool waiting_for_reaction = react_start_time_ > 0;
      if (waiting_for_reaction) {
        i64 now_micros = stopwatch_.GetElapsedMicros();
        result_->reaction_micros = now_micros - react_start_time_;
      } else {
        result_->click_too_early = true;
      }
      PopSelf();
    }
  }

  void DrawSquare(ImU32 color) {
    ScreenInfo screen = app_.screen_info();
    float char_x = ImGui::GetDefaultCharSizeX();
    float width = char_x * 14;

    ImVec2 upper_left;
    upper_left.x = screen.center.x - width;
    upper_left.y = screen.center.y - width;

    ImVec2 bottom_right;
    bottom_right.x = screen.center.x + width;
    bottom_right.y = screen.center.y + width;

    ImGui::GetWindowDrawList()->AddRectFilled(upper_left, bottom_right, color);
  }

  void OnTick() override {
    if (initial_wait_time_seconds_ < 0) {
      // Initialize
      stopwatch_.Start();
      initial_wait_time_seconds_ = app_.rand().GetInRange(1, 3);

      ImGui::NewSdlFrame();
      app_.BeginFullscreenWindow();

      if (options_.is_audio) {
        ScreenInfo screen = app_.screen_info();
        auto bold = app_.font_manager().UseLargeBold();
        std::string message = "Click after sound";
        ImVec2 text_size = ImGui::CalcTextSize(message.c_str());
        ImGui::SetCursorPosX(app_.screen_info().center.x - text_size.x * 0.5);
        ImGui::SetCursorPosY(app_.screen_info().center.y - text_size.y * 0.5);
        ImGui::Text(message);
      } else {
        // Visual
        DrawSquare(IM_COL32(255, 0, 0, 255));
      }

      ImGui::End();
      app_.renderer().RenderImGui();
      return;
    }

    i64 now_micros = stopwatch_.GetElapsedMicros();
    if (MicrosToSeconds(now_micros) > 10) {
      // No reaction. Just return;
      PopSelf();
      return;
    }

    bool waiting_for_reaction = react_start_time_ > 0;
    if (!waiting_for_reaction) {
      // See if we should play audio/visual cue.
      bool show_cue = MicrosToSeconds(now_micros) >= initial_wait_time_seconds_;
      if (show_cue) {
        react_start_time_ = now_micros;

        if (options_.is_audio) {
          app_.sound_manager().PlayLoadedSound(settings_.sounds().click_kill());
        } else {
          react_start_time_ = stopwatch_.GetElapsedMicros();
          ImGui::NewSdlFrame();
          app_.BeginFullscreenWindow();
          DrawSquare(IM_COL32(0, 255, 0, 255));
          ImGui::End();
          app_.renderer().RenderImGui(ImColor(0.0f, 0.0f, 0.0f, 1.0f));
        }
      }
    }
  }

  Settings settings_;
  ReactionTimeResult* result_;
  Stopwatch stopwatch_;

  float initial_wait_time_seconds_ = -1;
  i64 react_start_time_ = -1;
  ReactionTimeOptions options_;
};

class ReactionTimeScreen : public UiScreen {
 public:
  explicit ReactionTimeScreen() : UiScreen() {
    settings_ = app_.settings_manager().GetCurrentSettings();
    app_.sound_manager().LoadSounds(settings_);
  }

 protected:
  void DrawScreen() override {
    if (app_.BeginFullscreenWindow()) {
      DrawScreenInternal();
    }
    ImGui::End();
  }

  void DrawScreenInternal() {
    ImGui::Text("Reaction times");
    if (ImGui::Button("Cancel")) {
      PopSelf();
    }

    const char* audio_label = "Audio";
    const char* visual_label = "Visual";
    float char_x = ImGui::GetDefaultCharSizeX();

    std::string selected_type = options_.is_audio ? audio_label : visual_label;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Type");
    ImGui::SameLine();
    if (ImGui::SimpleDropdown(
            "TypeSelector", &selected_type, {visual_label, audio_label}, char_x * 10)) {
      options_.is_audio = selected_type == audio_label;
    }
    ImGui::Text("Use \"restart scenario\" keybind to start");

    {
      auto font = app_.font_manager().UseLarge();
      std::string message = icons::kRefresh;
      ImVec2 text_size = ImGui::CalcTextSize(message.c_str());
      ImGui::SetCursorPosX(app_.screen_info().center.x - text_size.x * 0.5);
      ImGui::SetCursorPosY(app_.screen_info().center.y - text_size.y * 4);
      if (ImGui::SelectableButton(message)) {
        StartRun();
      }
    }

    ImGui::Spacing();
    ImGui::Spacing();
    if (too_early_) {
      auto font = app_.font_manager().UseLarge();
      std::string message = "Too soon";
      ImVec2 text_size = ImGui::CalcTextSize(message.c_str());
      ImGui::SetCursorPosX(app_.screen_info().center.x - text_size.x * 0.5);
      ImGui::Text(message);
    }
    int count = 0;
    for (int i = reaction_times_.size() - 1; i >= 0; --i) {
      count++;
      if (count > 5) {
        break;
      }
      auto font = count == 1 ? app_.font_manager().UseLargeBold() : app_.font_manager().UseMedium();
      float millis = reaction_times_[i] / 1000.0f;
      std::string message = std::format("{}ms", MaybeIntToString(millis, 0));
      ImVec2 text_size = ImGui::CalcTextSize(message.c_str());
      ImGui::SetCursorPosX(app_.screen_info().center.x - text_size.x * 0.5);
      ImGui::Text(message);
    }
  }

  void OnAttachUi() override {
    if (!waiting_for_result_) {
      return;
    }
    waiting_for_result_ = false;
    too_early_ = false;

    if (result_.click_too_early) {
      too_early_ = true;
      return;
    }

    i64 reaction_time = result_.reaction_micros;
    if (reaction_time > 0) {
      reaction_times_.push_back(reaction_time);
    }
  }

  void StartRun() {
    waiting_for_result_ = true;
    result_ = {};
    PushNextScreen(
        std::make_unique<SingleReactionTimeScreen>(&app_, settings_, options_, &result_));
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) override {
    if (IsMappableKeyDownEvent(event)) {
      std::string event_name = absl::AsciiStrToLower(GetKeyNameForEvent(event));
      if (KeyMappingMatchesEvent(event_name, settings_.keybinds().restart_scenario())) {
        StartRun();
      }
    }
    if (IsEscapeKeyDown(event)) {
      PopSelf();
    }
  }

 private:
  Settings settings_;
  ReactionTimeResult result_;
  bool waiting_for_result_ = false;
  std::vector<i64> reaction_times_;

  ReactionTimeOptions options_;

  bool too_early_ = false;
};

}  // namespace

std::unique_ptr<UiScreen> CreateReactionTimeScreen() {
  return std::make_unique<ReactionTimeScreen>();
}

}  // namespace aim
