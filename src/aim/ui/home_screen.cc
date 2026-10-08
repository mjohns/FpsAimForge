#include "home_screen.h"

#include <future>
#include <optional>

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "absl/cleanup/cleanup.h"
#include "aim/common/files.h"
#include "aim/common/http.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/json.h"
#include "aim/common/log.h"
#include "aim/common/mat_icons.h"
#include "aim/common/simple_types.h"
#include "aim/common/system.h"
#include "aim/common/times.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/guide_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/local_store.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/settings_manager.h"
#include "aim/core/stats_manager.h"
#include "aim/core/version.h"
#include "aim/proto/scenario.pb.h"
#include "aim/scenario/scenario.h"
#include "aim/scenario/scenario_factory.h"
#include "aim/ui/bundle_ui.h"
#include "aim/ui/guide_ui.h"
#include "aim/ui/object_browser.h"
#include "aim/ui/playlist_ui.h"
#include "aim/ui/scenario_ui.h"
#include "aim/ui/stats/stats_screen.h"
#include "aim/ui/top_bar.h"
#include "aim/ui/ui_screen.h"
#include "imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"

namespace aim {
namespace {

const char* kSelectedAppScreenKey = "SelectedAppScreen";
const char* kLeftNavCollapsedKey = "LeftNavCollapsed";

const char* kDefaultBundlePackCurrentCommitSha = "DefaultBundlePackCommitSha";
const char* kDefaultBundlePackUrl =
    "https://github.com/mjohns/FpsAimForgeBundles/archive/refs/heads/main.zip";
const char* kDefaultBundlePackApiUrl =
    "https://api.github.com/repos/mjohns/FpsAimForgeBundles/branches/main";
const char* kDefaultBundlePackWebUrl = "https://github.com/mjohns/FpsAimForgeBundles";

std::shared_ptr<BundlePack> DownloadBundlePackOnBackgroundThread(const std::string& url) {
  FileDownload download;
  if (!DownloadFile(url, "", &download)) {
    return {};
  }
  std::shared_ptr<BundlePack> pack = ConvertZipToBundlePack(download.content);
  if (!pack) {
    Logger::get()->warn("Failed to convert bundle pack zip file. {}", url);
    return {};
  }
  return pack;
}

std::string GetCommitShaFromResponse(const std::string& response) {
  nlohmann::json json = nlohmann::json::parse(response);
  if (json.is_discarded()) {
    Logger::get()->warn("Unable to parse github api response: {}", response);
    return "";
  }

  if (!json.is_object()) {
    return "";
  }
  if (json.contains("commit") && json["commit"].is_object()) {
    const auto& commit = json["commit"];
    if (commit.contains("sha") && commit["sha"].is_string()) {
      return commit["sha"].get<std::string>();
    }
  }

  return "";
}

std::string GetCurrentDefaultPackCommitShaOnBackgroundThread() {
  FileDownload download;
  if (!DownloadFile(kDefaultBundlePackApiUrl, "", &download)) {
    return "";
  }
  std::string commit_sha = GetCommitShaFromResponse(download.content);
  return commit_sha;
}

class SetInitialDpiDialog {
 public:
  void NotifyOpen() {
    popup_.Open();
  }

  std::optional<int> Draw() {
    ImGui::IdGuard cid("SetInitialDpiDialog");
    std::optional<int> set_dpi;
    if (popup_.Begin()) {
      ImGui::AlignTextToFramePadding();
      ImGui::Text("What is your mouse DPI?");
      ImGui::SameLine();
      ImGui::HelpMarker("DPI is used to calculate sensitivity given a cm/360 value.");

      if (ImGui::Button("400")) {
        set_dpi = 400;
      }
      ImGui::SameLine();
      if (ImGui::Button("800")) {
        set_dpi = 800;
      }
      ImGui::SameLine();
      if (ImGui::Button("1600")) {
        set_dpi = 1600;
      }
      ImGui::SameLine();
      if (ImGui::Button("3200")) {
        set_dpi = 3200;
      }

      ImGui::Spacing();

      float char_x = ImGui::GetDefaultCharSizeX();
      ImGui::SetNextItemWidth(char_x * 12);
      ImGui::InputInt("##DpiInput", &dpi_input_value_, 100, 200);

      ImGui::SameLine();
      bool is_valid = dpi_input_value_ > 0;
      if (!is_valid) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Set")) {
        set_dpi = dpi_input_value_;
      }
      if (!is_valid) {
        ImGui::EndDisabled();
      }

      if (set_dpi) {
        popup_.Close();
      }
      popup_.End();
    }
    return set_dpi;
  }

 private:
  int dpi_input_value_ = 800;
  ImGui::Popup popup_{"SetDpiDialog"};
};

class UpdateDialog {
 public:
  void NotifyOpen() {
    bundle_pack_future_ =
        std::async(std::launch::async, DownloadBundlePackOnBackgroundThread, kDefaultBundlePackUrl);
    download_stopwatch_ = {};
    download_stopwatch_.Start();
    bundle_pack_ = {};
    start_update_ = false;
    do_update_ = false;
    popup_.Open();
  }

  // Returns if the update is done and should be cleared from top_bar.
  bool Draw() {
    ImGui::IdGuard cid("UpdateDialog");
    std::optional<int> set_dpi;
    if (!popup_.BeginModal()) {
      return false;
    }
    auto end_cleanup = absl::MakeCleanup([this] { popup_.End(); });

    if (bundle_pack_future_) {
      std::future_status status = bundle_pack_future_->wait_for(std::chrono::seconds(0));
      if (status == std::future_status::ready) {
        download_stopwatch_.Stop();
        bundle_pack_ = bundle_pack_future_->get();
        bundle_pack_future_ = {};
        return false;
      }

      // Still waiting for download.
      ImGui::TextFmt("Downloading update - {:.1f}s", download_stopwatch_.GetElapsedSeconds());

      ImGui::SameLine();
      DrawCancelButton();

      return false;
    }

    // Download is done. Check if it failed.
    if (!bundle_pack_) {
      ImGui::Text("Update failed");
      DrawCancelButton();
      return false;
    }

    if (start_update_) {
      ImGui::Text("Updating");
      start_update_ = false;
      do_update_ = true;
      return false;
    }

    if (do_update_) {
      do_update_ = false;
      auto& app = GetUiApp();
      if (!WriteBinaryMessageToFile(app.file_system().GetUserDataPath("bundles/Default.pack.bin"),
                                    *bundle_pack_)) {
        return false;
      }
      app.bundle_manager().LoadBundlesFromDisk();
      ClosePopupAndCleanup();
      return true;
    }

    ImGui::Text("Download complete");
    ImGui::TextFmt("{} Bundles found", bundle_pack_->items_size());

    if (ImGui::Button(std::format("{} View in browser", icons::kOpenInNew))) {
      OpenUrlInBrowser(kDefaultBundlePackWebUrl);
    }
    ImGui::HelpTooltip(kDefaultBundlePackWebUrl);

    ImGui::SpacedSeparator();
    if (ImGui::Button("Update")) {
      start_update_ = true;
    }
    ImGui::SameLine();
    DrawCancelButton();
    return false;
  }

  void DrawCancelButton() {
    if (ImGui::Button("Cancel")) {
      ClosePopupAndCleanup();
    }
  }

  void ClosePopupAndCleanup() {
    bundle_pack_future_ = {};
    bundle_pack_ = {};
    popup_.Close();
  }

 private:
  ImGui::Popup popup_{"UpdateDialog"};
  std::optional<std::future<std::shared_ptr<BundlePack>>> bundle_pack_future_;
  std::shared_ptr<BundlePack> bundle_pack_;
  Stopwatch download_stopwatch_;

  // Render updating text and then close.
  bool start_update_ = false;
  bool do_update_ = false;
};

class HomeScreen : public UiScreen {
 public:
  HomeScreen() : UiScreen() {
    playlist_component_ = CreatePlaylistComponent();
    playlist_list_component_ = CreatePlaylistListComponent(this);
    bundle_ui_component_ = CreateBundleUiComponent(this);
    scenarios_component_ = CreateScenariosComponent();
    guides_component_ = CreateGuidesComponent();

    auto selected_app_screen = app_.local_store().GetInt(kSelectedAppScreenKey);
    if (selected_app_screen) {
      app_screen_ = static_cast<AppScreen>(*selected_app_screen);
    }

    auto last_playlist = app_.history_manager().GetRecentViews(ObjectType::PLAYLIST, 1);
    if (last_playlist.size() > 0) {
      std::string name = last_playlist[0].name;
      if (name.size() > 0) {
        app_.playlist_manager().SetCurrentPlaylist(name);
      }
    }

    auto last_scenario = app_.history_manager().GetRecentViews(ObjectType::SCENARIO, 1);
    if (last_scenario.size() > 0) {
      app_.scenario_manager().SetCurrentScenario(last_scenario[0].name);
    }

    auto last_guide = app_.history_manager().GetRecentViews(ObjectType::GUIDE, 1);
    if (last_guide.size() > 0) {
      app_.guide_manager().SetCurrentGuide(last_guide[0].name);
    }

    pending_default_pack_version_future_ =
        std::async(std::launch::async, GetCurrentDefaultPackCommitShaOnBackgroundThread);
  }

  void OnTickStart() override {
    if (state_.go_to_app_screen) {
      app_screen_ = *state_.go_to_app_screen;
      app_.local_store().PutInt(kSelectedAppScreenKey, (int)app_screen_);
      state_.go_to_app_screen = {};
    }
    auto run_option = state_.scenario_run_option;
    if (run_option) {
      if (*run_option == ScenarioRunOption::START_CURRENT) {
        RunCurrentScenario();
      }
      if (*run_option == ScenarioRunOption::RESUME_CURRENT) {
        ResumeCurrentScenario();
      }
      if (*run_option == ScenarioRunOption::PLAYLIST_NEXT) {
        HandlePlaylistNext();
      }
    }
    state_.scenario_run_option = {};

    HandleCheckDefaultPackVersion();
  }

  void HandleCheckDefaultPackVersion() {
    if (!pending_default_pack_version_future_) {
      return;
    }
    std::future_status status =
        pending_default_pack_version_future_->wait_for(std::chrono::seconds(0));
    if (status != std::future_status::ready) {
      return;
    }
    std::string commit_sha = pending_default_pack_version_future_->get();
    pending_default_pack_version_future_ = {};

    // std::cout << std::format("Got commit sha {}", commit_sha) << std::endl;

    std::string current_commit_sha = app_.local_store().Get(kDefaultBundlePackCurrentCommitSha);
    if (current_commit_sha.empty()) {
      current_commit_sha = kAimForgeDefaultBundlePackCommitSha;
    }
    if (current_commit_sha == commit_sha) {
      return;
    }

    default_bundle_pack_update_available_ = commit_sha;
  }

  void RunCurrentScenario() {
    if (!GetCurrentScenario().has_value()) {
      return;
    }
    ScenarioItem current_scenario = *GetCurrentScenario();
    app_.history_manager().UpdateRecentView(ObjectType::SCENARIO, current_scenario.name);

    auto run = app_.playlist_manager().GetCurrentRun();
    if (run) {
      app_.history_manager().UpdateRecentView(ObjectType::PLAYLIST, run->playlist.name);
    }
    CreateScenarioParams params;
    params.name = current_scenario.name;
    std::optional<ScenarioDef> evaluated_def =
        app_.scenario_manager().GetEvaluatedScenarioDef(current_scenario.name);
    if (!evaluated_def) {
      std::string msg = std::format("Unable to evaluate scenario \"{}\".\n{}",
                                    current_scenario.name,
                                    MessageToJson(current_scenario.unevaluated_def));
      Logger::get()->warn("{}", msg);
      notification_popup_.NotifyOpen(msg);
      return;
    }
    params.def = *evaluated_def;
    std::shared_ptr<Screen> running_scenario = CreateScenario(params);
    if (!running_scenario) {
      std::string msg = std::format(
          "Invalid scenario \"{}\".\n{}", current_scenario.name, MessageToJson(params.def));
      Logger::get()->warn("{}", msg);
      notification_popup_.NotifyOpen(msg);
      return;
    }
    app_.scenario_manager().SetCurrentRunningScenario(running_scenario);
    PushNextScreen(running_scenario);
  }

  void ResumeCurrentScenario() {
    if (app_.scenario_manager().has_running_scenario()) {
      PushNextScreen(app_.scenario_manager().GetCurrentRunningScenario());
    }
  }

  void DrawScreen() override {
    if (app_.BeginFullscreenWindow()) {
      DrawScreenInternal();
    }
    ImGui::End();
  }

  void OnAttachUi() override {}

  void DrawScreenInternal() {
    ImGui::IdGuard cid("HomePage");

    notification_popup_.Draw();

    std::optional<int> set_dpi = set_dpi_dialog_.Draw();
    if (set_dpi) {
      auto updater = app_.settings_manager().CreateUpdater();
      updater.settings.set_dpi(*set_dpi);
      updater.SaveIfChangesMade("");
    }

    Settings settings = app_.settings_manager().GetCurrentSettings();
    if (settings.dpi() <= 0) {
      set_dpi_dialog_.NotifyOpen();
    }

    TopBar::Result top_bar_result;
    top_bar_->DrawEx(!default_bundle_pack_update_available_.empty(), &top_bar_result);
    if (top_bar_result.do_update_clicked) {
      update_dialog_.NotifyOpen();
    }

    bool updated_version = update_dialog_.Draw();
    if (updated_version) {
      app_.local_store().Put(kDefaultBundlePackCurrentCommitSha,
                             default_bundle_pack_update_available_);
      default_bundle_pack_update_available_ = "";
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGuiTableFlags main_column_flags =
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV;

    if (ImGui::BeginTable("MainColumns", 2, main_column_flags)) {
      bool left_nav_collapsed = app_.local_store().GetBool(kLeftNavCollapsedKey);
      float left_size = ImGui::GetDefaultCharSizeX() * 9;
      if (left_nav_collapsed) {
        auto font = app_.font_manager().UseMedium();
        left_size = ImGui::CalcTextSize(icons::kList).x;
      }
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, left_size);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      DrawLeftNav(left_nav_collapsed);

      ImGui::TableNextColumn();

      if (ImGui::BeginChild("PrimaryContent")) {
        if (app_screen_ == AppScreen::SCENARIOS) {
          scenarios_component_->Show();
        }
        if (app_screen_ == AppScreen::PLAYLISTS) {
          DrawPlaylistsScreen();
        }
        if (app_screen_ == AppScreen::BUNDLES) {
          bundle_ui_component_->Show();
        }
        if (app_screen_ == AppScreen::GUIDES) {
          guides_component_->Show();
        }
        last_app_screen_ = app_screen_;
      }
      ImGui::EndChild();

      ImGui::EndTable();
    }
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) override {
    Settings settings = app_.settings_manager().GetCurrentSettings();
    if (user_is_typing) {
      return;
    }
    if (IsMappableKeyDownEvent(event)) {
      auto current_scenario = GetCurrentScenario();
      std::string scenario_name = current_scenario ? current_scenario->name : "";
      HandleDefaultScenarioEvents(event, user_is_typing, scenario_name);

      if (event.key.key == SDLK_ESCAPE) {
        state_.scenario_run_option = ScenarioRunOption::RESUME_CURRENT;
      }
    }
  }

 private:
  std::optional<ScenarioItem> GetCurrentScenario() {
    return app_.scenario_manager().GetCurrentScenario();
  }

  std::string GetCurrentScenarioId() {
    auto scenario = app_.scenario_manager().GetCurrentScenario();
    if (scenario.has_value()) {
      return scenario->name;
    }
    return "";
  }

  void HandlePlaylistNext() {
    std::shared_ptr<PlaylistRun> run = app_.playlist_manager().GetCurrentRun();
    if (run == nullptr) {
      RunCurrentScenario();
      return;
    }
    std::optional<std::string> next_scenario = run->Next();
    if (next_scenario) {
      app_.scenario_manager().SetCurrentScenario(*next_scenario);
    }
    RunCurrentScenario();
  }

  void DrawLeftNav(bool left_nav_collapsed) {
    AppScreen original_app_screen = app_screen_;

    auto font =
        left_nav_collapsed ? app_.font_manager().UseMedium() : app_.font_manager().UseDefault();

    ImGui::Spacing();
    if (ImGui::Selectable(
            left_nav_collapsed ? icons::kKeyboardDoubleArrowRight : icons::kKeyboardDoubleArrowLeft,
            false)) {
      app_.local_store().PutBool(kLeftNavCollapsedKey, !left_nav_collapsed);
    }
    ImGui::HelpTooltip(left_nav_collapsed ? "Expand" : "Collapse");

    ImGui::SpacedSeparator();

    float tooltip_delay_seconds = 0.6;
    auto item_selectable =
        [&](const std::string& icon, const std::string& name, AppScreen this_app_screen) {
          std::string text = left_nav_collapsed ? icon : std::format("{} {}", icon, name);
          if (ImGui::Selectable(text, app_screen_ == this_app_screen)) {
            app_screen_ = this_app_screen;
          }
          if (left_nav_collapsed) {
            ImGui::HelpTooltip(name, tooltip_delay_seconds);
          }
        };

    item_selectable(icons::kMap, "Guides", AppScreen::GUIDES);
    item_selectable(icons::kList, "Playlists", AppScreen::PLAYLISTS);
    item_selectable(icons::kCenterFocusWeak, "Scenarios", AppScreen::SCENARIOS);
    item_selectable(icons::kAutoAwesomeMotion, "Bundles", AppScreen::BUNDLES);
    auto latest_run = app_.stats_manager().GetLatestRun();
    if (latest_run) {
      auto* icon = icons::kAssignment;
      std::string text = left_nav_collapsed ? icon : std::format("{} Results", icon);
      if (ImGui::Selectable(text.c_str(), false)) {
        PushNextScreen(CreateStatsScreen(latest_run->scenario_name, latest_run->run_id, false));
      }
      if (left_nav_collapsed) {
        ImGui::HelpTooltip("Results", tooltip_delay_seconds);
      }
    }

    if (original_app_screen != app_screen_) {
      app_.local_store().PutInt(kSelectedAppScreenKey, (int)app_screen_);
    }

    if (kIsDebugBuild) {
      ImGui::SetCursorAtBottom();
      ImGui::Text("%d", (int)ImGui::GetIO().Framerate);
    }
  }

  void DrawPlaylistsScreen() {
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("PlaylistColumns", 2, flags)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, GetDefaultObjectBrowserWidth());
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

      ImGui::TableNextColumn();

      if (ImGui::BeginChild("Playlists")) {
        playlist_list_component_->Show();
      }
      ImGui::EndChild();

      ImGui::TableNextColumn();
      DrawCurrentPlaylistScreen();

      ImGui::EndTable();
    }
  }

  void DrawCurrentPlaylistScreen() {
    ImVec2 sz = ImVec2(0.0f, 0.0f);
    std::shared_ptr<PlaylistRun> run = app_.playlist_manager().GetCurrentRun();
    if (run) {
      PlaylistComponent::Options options;
      options.is_playlist_screen = true;
      playlist_component_->Show(run, options);
    }
  }

  AppScreen app_screen_ = AppScreen::PLAYLISTS;
  std::optional<AppScreen> last_app_screen_;

  std::unique_ptr<PlaylistComponent> playlist_component_;
  std::unique_ptr<PlaylistListComponent> playlist_list_component_;
  std::unique_ptr<BundleUiComponent> bundle_ui_component_;
  std::unique_ptr<ScenariosComponent> scenarios_component_;
  std::unique_ptr<GuidesComponent> guides_component_;
  SetInitialDpiDialog set_dpi_dialog_;
  UpdateDialog update_dialog_;
  std::unique_ptr<TopBar> top_bar_ = CreateTopBar();
  ImGui::NotificationPopup notification_popup_{"Notification"};

  std::optional<std::future<std::string>> pending_default_pack_version_future_;

  // The commit sha of available default bundle pack update.
  std::string default_bundle_pack_update_available_;
};

}  // namespace

std::shared_ptr<Screen> CreateHomeScreen() {
  return std::make_shared<HomeScreen>();
}

}  // namespace aim
