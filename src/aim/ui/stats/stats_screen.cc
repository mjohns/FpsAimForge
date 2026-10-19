#include "stats_screen.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>

#include "absl/cleanup/cleanup.h"
#include "absl/time/time.h"
#include "aim/common/collections.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
#include "aim/common/proto_util.h"
#include "aim/common/simple_types.h"
#include "aim/common/times.h"
#include "aim/common/util.h"
#include "aim/analysis/nim_client.h"
#include "aim/analysis/scenario_analysis.h"
#include "aim/core/perf.h"
#include "aim/common/files.h"
#include "aim/core/replay_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/core/settings_manager.h"
#include "aim/core/stats_manager.h"
#include "aim/proto/stats.pb.h"
#include "aim/scenario/replay.h"
#include "aim/scenario/replay_viewer.h"
#include "aim/ui/playlist_ui.h"
#include "aim/ui/quick_settings_screen.h"
#include "aim/ui/stats/history_plot.h"
#include "aim/ui/stats/perf_ui.h"
#include "aim/ui/top_bar.h"
#include "imgui.h"
#include "implot.h"

namespace aim {
namespace {

const ImVec4 kTopThresholdColor(0.9, 0.2, 0.4, 0.9);
const ImVec4 kMidThresholdColor(0.4, 0.4, 0.5, 0.7);

struct ScoresOverTime {
  ScoresOverTime(const std::vector<float>& replay_scores) {
    scores.reserve(replay_scores.size());
    times.reserve(replay_scores.size());
    for (int i = 0; i < replay_scores.size(); ++i) {
      float score = replay_scores[i];
      scores.push_back(score);
      float t = i / static_cast<float>(kRecordScoresPerSecond);

      if (t >= 2) {
        // Don't count the very beginning scores as they will be unpredictable
        min_score = std::min(score, min_score);
        max_score = std::max(score, max_score);
      }

      times.push_back(t);
    }
  }

  std::vector<float> scores;
  std::vector<float> times;

  float max_score = 0;
  float min_score = 12000000;
};

void DrawScoresOverTimePlot(const std::string& scenario_name,
                            i64 run_id,
                            ScoresOverTime& scores_over_time,
                            float score_target) {
  std::vector<float>& scores = scores_over_time.scores;
  std::vector<float>& times = scores_over_time.times;
  if (scores.empty()) {
    return;
  }
  if (scores.size() != times.size()) {
    assert(false && "Scores and times not the same size");
    return;
  }

  double threshold = scores.back();
  double double_score_target = score_target;

  auto crust = HexToImColor("#181926");
  ImPlot::PushStyleColor(ImPlotCol_PlotBg, crust.Value);
  ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
  auto style_cleanup = absl::MakeCleanup([]() {
    ImPlot::PopStyleColor(1);
    ImPlot::PopStyleVar(1);
  });
  std::string id = std::format("Scores Over Time##{}_{}", scenario_name, run_id);
  ImPlotFlags plot_flags = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
                           ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;
  if (!ImPlot::BeginPlot(id.c_str(), ImVec2(-1, 0), plot_flags)) {
    return;
  }

  auto axis_flags =
      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch;
  ImPlot::SetupAxis(ImAxis_X1, "Time", axis_flags);
  ImPlot::SetupAxis(ImAxis_Y1, "Score", axis_flags);

  ImPlot::SetupAxisLimits(ImAxis_X1, times.front(), times.back(), ImPlotCond_Always);
  ImPlot::SetupAxisLimits(
      ImAxis_Y1, scores_over_time.min_score * 0.95, scores_over_time.max_score * 1.05);

  // ImPlot::PlotShaded("Score History", times.data(), scores.data(), scores.size(), 0);
  ImPlot::PlotLine("Score History", times.data(), scores.data(), scores.size());

  ImPlot::DragLineY(0, &threshold, kTopThresholdColor, 1.0f, ImPlotDragToolFlags_NoInputs);
  if (double_score_target > 0) {
    ImPlot::DragLineY(
        1, &double_score_target, kMidThresholdColor, 1.0f, ImPlotDragToolFlags_NoInputs);
  }

  if (ImPlot::IsPlotHovered()) {
    ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);

    float time = mouse_pos.x;

    int closest_index = std::round(time * kRecordScoresPerSecond);

    if (IsValidIndex(scores, closest_index)) {
      float x_val = mouse_pos.x;
      float y_val = scores[closest_index];
      ImGui::BeginTooltip();
      ImGui::Text("Score: %.2f", y_val);
      ImGui::Text("Time: %.2f", x_val);
      ImGui::EndTooltip();

      ImPlotSpec spec;
      spec.Marker = ImPlotMarker_Circle;
      spec.MarkerSize = 4.0f;
      spec.MarkerFillColor = ImVec4(1, 0, 0, 1);
      spec.MarkerLineColor = ImVec4(1, 0, 0, 1);
      spec.LineWeight = IMPLOT_AUTO;
      ImPlot::PlotScatter("MouseDot", &x_val, &y_val, 1, spec);
    }
  }

  ImPlot::EndPlot();
}

enum class SelectedScreen : int {
  STATS = 1,
  HISTORY = 2,
  PERF = 3,
  ANALYSIS = 4,
};

struct HistoryRow {
  int run_number = 0;
  std::string time_ago;
  std::string timestamp;
  float score;
  i64 stats_id;
  float cm_per_360 = 0;
};

struct StatsComparison {
  float score_diff = 0;
  float score_diff_percent = 0;
  std::string score_diff_percent_string;
  std::string score_diff_string;
};

StatsComparison GetStatsComparison(const StatsDbRow& current_stats,
                                   const StatsDbRow& comparison_stats) {
  StatsComparison r;
  r.score_diff = current_stats.score - comparison_stats.score;
  if (comparison_stats.score == 0) {
    r.score_diff_percent = 0;
    r.score_diff_percent_string = r.score_diff == 0 ? "0%" : "N/A";
    r.score_diff_string = MaybeIntToString(r.score_diff, 2);
    return r;
  }

  r.score_diff_percent = r.score_diff / comparison_stats.score;

  std::string percent_diff_str = MaybeIntToString(abs(r.score_diff_percent) * 100, 1);
  std::string plus_minus = r.score_diff_percent < 0 ? "-" : "+";

  r.score_diff_percent_string = std::format("{}{}%", plus_minus, percent_diff_str);
  std::string abs_diff_str = MaybeIntToString(abs(r.score_diff), 2);
  r.score_diff_string = std::format("{}{}", plus_minus, abs_diff_str);
  return r;
}

class StatsScreen : public UiScreen {
 public:
  StatsScreen(std::string scenario_name, i64 run_id, bool delay_display)
      : UiScreen(),
        scenario_name_(scenario_name),
        run_id_(run_id),
        replay_(app_.replay_manager().GetReplay(run_id)),
        delay_display_(delay_display) {
    screen_start_time_millis_ = GetNowEpochMillis();
    scenario_ = app_.scenario_manager().GetScenario(scenario_name);
    evaluated_scenario_def_ = app_.scenario_manager().GetEvaluatedScenarioDef(scenario_name);
    if (scenario_) {
      reference_scenario_name_ = scenario_->unevaluated_def.reference_def().scenario_name();
    }

    is_valid_ = app_.stats_manager().GetStatsDetails(scenario_name_, run_id_, &details_);

    performance_stats_ = state_.GetPerformanceStats(scenario_name, run_id);

    ImVec2 char_size = ImGui::CalcTextSize("A");
    char_x_ = char_size.x;

    compare_to_scenarios_ = GetCompareToList();
    if (replay_ && replay_->scores.size() > 0) {
      scores_over_time_ = ScoresOverTime(replay_->scores);
    }

    // If the player had "AI watch" toggled on, auto-start the visual capture.
    if (app_.state().ai_watch_enabled && replay_ && IsNimConfigured()) {
      app_.state().ai_watch_enabled = false;  // consume the flag — one shot
      // Analysis needs to be computed first; we defer to OnAttachUi.
      ai_watch_pending_ = true;
    }
  }

 protected:
  void OnAttachUi() override {
    if (scenario_name_ != app_.scenario_manager().GetCurrentScenarioName()) {
      app_.scenario_manager().SetCurrentScenario(scenario_name_);
    }
    // Auto-start AI watch capture if flagged from the play button toggle.
    if (ai_watch_pending_ && !visual_capture_state_ && replay_) {
      ai_watch_pending_ = false;
      if (!analysis_) {
        analysis_ = AnalyzeScenarioReplay(*replay_);
      }
      visual_capture_state_ = std::make_shared<VisualReplayCaptureState>();
      visual_capture_state_->prompt = BuildNimAnalysisPrompt(*analysis_, details_.all_stats);
      PushNextScreen(CreateReplayViewerScreen(replay_, &app_, visual_capture_state_));
    }
    playlist_run_ = app_.playlist_manager().GetCurrentRun();
    if (!playlist_run_) {
      return;
    }
    bool playlist_has_scenario = false;
    for (auto& item : playlist_run_->progress_list) {
      if (item.item.scenario() == scenario_name_) {
        playlist_has_scenario = true;
        break;
      }
    }
    if (!playlist_has_scenario) {
      playlist_run_ = nullptr;
    }
  }

  bool HasAccuracyPenalty() {
    if (evaluated_scenario_def_) {
      switch (evaluated_scenario_def_->shot_type().type_case()) {
        case ShotType::kClickSingle:
        case ShotType::kClickMulti:
          return !evaluated_scenario_def_->shot_type().has_reload();
        default:
          break;
      }
    }
    return false;
  }

  bool IsScreenOlderThan(i64 millis) {
    i64 age_millis = GetNowEpochMillis() - screen_start_time_millis_;
    return age_millis > millis;
  }

  void DrawScreen() override {
    ImGui::IdGuard cid("StatsScreen");

    if (reset_stats_) {
      reset_stats_ = false;
      history_rows_ = {};
      details_ = {};
      is_valid_ = app_.stats_manager().GetStatsDetails(scenario_name_, run_id_, &details_);
    }

    if (!is_valid_) {
      PopSelf();
      return;
    }

    i64 age_millis = GetNowEpochMillis() - screen_start_time_millis_;
    if (delay_display_ && !IsScreenOlderThan(200)) {
      return;
    }

    if (app_.BeginFullscreenWindow()) {
      DrawScreenInternal();
    }
    ImGui::End();
  }

  void DrawScreenInternal() {
    if (evaluated_scenario_def_) {
      score_target_ = GetTargetScore(scenario_name_, *evaluated_scenario_def_, playlist_run_);
    }

    top_bar_->Draw();
    ImGui::Spacing();
    ImGui::Spacing();

    ImGuiTableFlags main_column_flags = ImGuiTableFlags_SizingStretchProp |
                                        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersOuter |
                                        ImGuiTableFlags_BordersV;

    if (ImGui::BeginTable("MainColumns", 2, main_column_flags)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 12);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      DrawLeftNav();

      ImGui::TableNextColumn();

      if (ImGui::BeginChild("PrimaryContent")) {
        DrawExportStatus();
        if (selected_screen_ == SelectedScreen::STATS) {
          DrawStatsPanel();
        }
        if (selected_screen_ == SelectedScreen::PERF) {
          if (performance_stats_) {
            DrawPerformanceStats(*performance_stats_);
          }
        }
        if (selected_screen_ == SelectedScreen::HISTORY) {
          DrawHistoryPanel();
        }
        if (selected_screen_ == SelectedScreen::ANALYSIS) {
          DrawAnalysisPanel();
        }
      }
      ImGui::EndChild();

      ImGui::EndTable();
    }
  }

  void DrawLeftNav() {
    if (ImGui::Selectable(std::format("{} Home", icons::kHome).c_str(), false)) {
      ReturnHome();
    }
    if (ImGui::Selectable(std::format("{} Stats", icons::kAssignment).c_str(),
                          selected_screen_ == SelectedScreen::STATS)) {
      selected_screen_ = SelectedScreen::STATS;
    }
    if (ImGui::Selectable(std::format("{} History", icons::kBarChart).c_str(),
                          selected_screen_ == SelectedScreen::HISTORY)) {
      selected_screen_ = SelectedScreen::HISTORY;
    }
    if (replay_ && ImGui::Selectable(std::format("{} Replay Review", icons::kLiveTv).c_str(),
                                      false)) {
      PushNextScreen(CreateReplayViewerScreen(replay_, &app_));
    }
    if (replay_ && ImGui::Selectable("Export Replay as TXT", false)) {
      ExportReplayTxt();
    }
    if (replay_ && ImGui::Selectable(std::format("{} Export MP4", icons::kMovie).c_str(),
                                      false)) {
      StartReplayMp4Export();
    }
    if (replay_ && ImGui::Selectable(
                        std::format("{} Detailed Analysis", icons::kSmartToy).c_str(),
                        selected_screen_ == SelectedScreen::ANALYSIS)) {
      selected_screen_ = SelectedScreen::ANALYSIS;
    }
    if (performance_stats_) {
      std::string label = std::format("{} Perf", icons::kSmartToy);
      if (kIsDebugBuild) {
        auto& worst_times = performance_stats_->worst_times;
        float total_ms = (worst_times.end - worst_times.start) / 1000.0;
        label += "-" + MaybeIntToString(1000 / total_ms, 0);
      }
      if (ImGui::Selectable(label.c_str(), selected_screen_ == SelectedScreen::PERF)) {
        selected_screen_ = SelectedScreen::PERF;
      }
    }
  }

  void ExportReplayTxt() {
    if (!replay_) {
      txt_export_status_ = "TXT export failed: this run has no saved replay.";
      return;
    }

    const auto exports_dir = app_.file_system().GetUserDataPath("replays");
    std::error_code ec;
    std::filesystem::create_directories(exports_dir, ec);
    if (ec) {
      txt_export_status_ = std::format("TXT export failed: could not create {} ({})",
                                        exports_dir.string(), ec.message());
      return;
    }

    std::string safe_name = scenario_name_;
    for (char& c : safe_name) {
      if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
          c == '"' || c == '<' || c == '>' || c == '|' || c == ' ') {
        c = '_';
      }
    }
    txt_export_path_ = exports_dir / std::format("{}_{}.txt", safe_name, run_id_);

    std::ostringstream out;
    out << "FPS AIMFORGE REPLAY TXT EXPORT\n";
    out << "Format: human-readable measurement dump (angles/targets/events/scores)\n";
    out << "Run ID: " << run_id_ << "\n";
    out << "Scenario: " << scenario_name_ << "\n";
    out << "Recorded local time: "
        << absl::FormatTime("%Y-%m-%d %H:%M:%S",
                            absl::FromTimeT(details_.stats.epoch_seconds),
                            absl::LocalTimeZone()) << "\n";
    out << std::fixed << std::setprecision(6);
    out << "Score: " << details_.stats.score << "\n";
    out << "cm/360: " << (replay_->cm_per_360 > 0 ? replay_->cm_per_360
                                                    : details_.stats.cm_per_360) << "\n";
    out << "mm/360 (stored stats): " << details_.stats.mm_per_360 << "\n";
    out << "Duration seconds: " << replay_->GetDurationSeconds() << "\n";
    out << "Replay FPS: " << replay_->replay_fps << "\n";
    out << "Target channels: " << replay_->num_targets << "\n";
    out << "Shot type enum: " << static_cast<int>(replay_->shot_type) << "\n";
    out << "Frame count: " << replay_->pitch_yaws.size() << "\n";
    out << "Target sample count: " << replay_->target_data.size() << "\n";
    out << "Target metadata count: " << replay_->target_metadata.size() << "\n";
    out << "Event count: " << replay_->events.size() << "\n";
    out << "Score sample count: " << replay_->scores.size() << "\n\n";
    out << "[ROOM_PROTO_JSON]\n" << MessageToJson(replay_->room) << "\n\n";

    out << "[AIM_ANGLES]\n";
    out << "frame,time_seconds,pitch,yaw\n";
    for (size_t i = 0; i < replay_->pitch_yaws.size(); ++i) {
      const auto& sample = replay_->pitch_yaws[i];
      out << i << ',' << static_cast<double>(i) / replay_->replay_fps << ','
          << sample.pitch << ',' << sample.yaw << '\n';
    }

    out << "\n[TARGET_METADATA]\n";
    out << "add_time_seconds,target_id,data_channel,x,y,z,radius,health,is_ghost,has_health,pill_height\n";
    for (const auto& metadata : replay_->target_metadata) {
      out << static_cast<double>(metadata.add_time_micros) / 1000000.0 << ','
          << metadata.target_id << ',' << metadata.data_channel << ','
          << metadata.initial_data.position.x << ',' << metadata.initial_data.position.y << ','
          << metadata.initial_data.position.z << ',' << metadata.initial_data.radius << ','
          << static_cast<int>(metadata.initial_data.health) << ','
          << metadata.is_ghost << ',' << metadata.has_health << ','
          << metadata.pill_height << '\n';
    }

    out << "\n[TARGET_SNAPSHOTS]\n";
    out << "frame,time_seconds,channel,x,y,z,radius,health\n";
    for (size_t i = 0; i < replay_->target_data.size(); ++i) {
      const auto& sample = replay_->target_data[i];
      const size_t frame = i / replay_->num_targets;
      const size_t channel = i % replay_->num_targets;
      out << frame << ',' << static_cast<double>(frame) / replay_->replay_fps << ','
          << channel << ',' << sample.position.x << ',' << sample.position.y << ','
          << sample.position.z << ',' << sample.radius << ','
          << static_cast<int>(sample.health) << '\n';
    }

    out << "\n[EVENTS]\n";
    out << "time_seconds,event_type,event_value\n";
    for (const auto& event : replay_->events) {
      out << static_cast<double>(event.time_micros) / 1000000.0 << ',';
      switch (event.type) {
        case ReplayEventType::PLAY_SOUND:
          out << "PLAY_SOUND," << static_cast<int>(event.data.play_sound.sound);
          break;
        case ReplayEventType::REMOVE_TARGET:
          out << "REMOVE_TARGET," << event.data.target_id;
          break;
        case ReplayEventType::MOUSE_CLICK:
          out << "MOUSE_CLICK," << (event.data.is_hit ? "HIT" : "MISS");
          break;
        default:
          out << "UNKNOWN,";
          break;
      }
      out << '\n';
    }

    out << "\n[SCORE_SAMPLES]\n";
    out << "time_seconds,score\n";
    for (size_t i = 0; i < replay_->scores.size(); ++i) {
      out << static_cast<double>(i) / kRecordScoresPerSecond << ',' << replay_->scores[i] << '\n';
    }

    if (!WriteStringToFile(txt_export_path_, out.str())) {
      txt_export_status_ = "TXT export failed: unable to write the file. Check folder permissions.";
      return;
    }
    txt_export_status_ = std::format("Replay TXT exported: {}", txt_export_path_.string());
  }

  void StartReplayMp4Export() {
    if (!replay_ || export_capture_state_) return;
    export_capture_state_ = std::make_shared<ReplayExportCaptureState>();
    // Build output path: <userdata>/replays/<scenario>_<run_id>.mp4
    const std::string safe_name = [&] {
      std::string s = scenario_name_;
      for (char& c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || c == ' ') {
          c = '_';
        }
      }
      return s;
    }();
    const auto exports_dir = app_.file_system().GetUserDataPath("replays");
    std::error_code ec;
    std::filesystem::create_directories(exports_dir, ec);
    export_capture_state_->output_path =
        exports_dir / std::format("{}_{}.mp4", safe_name, run_id_);
    PushNextScreen(CreateReplayExportViewerScreen(replay_, &app_, export_capture_state_));
  }

  void DrawExportStatus() {
    if (!txt_export_status_.empty()) {
      ImGui::TextWrapped("%s", txt_export_status_.c_str());
      if (!txt_export_path_.empty() && ImGui::Button("Open TXT export folder")) {
        OpenFolderInExplorer(txt_export_path_.parent_path());
      }
      ImGui::Spacing();
    }
    if (!export_capture_state_) return;
    auto vis = export_capture_state_->vis_capture;
    if (!vis) {
      ImGui::TextDisabled("Preparing export...");
      return;
    }
    std::lock_guard lock(vis->mutex);
    if (vis->active) {
      ImGui::TextDisabled("Capturing replay frames...");
      ImGui::ProgressBar(vis->progress, ImVec2(-1, 0));
      return;
    }
    if (!vis->error.empty()) {
      ImGui::TextWrapped("Export failed: %s", vis->error.c_str());
      if (ImGui::Button("Dismiss")) export_capture_state_.reset();
      return;
    }
    auto es = vis->export_state;
    if (!es) {
      ImGui::TextDisabled("Encoding...");
      return;
    }
    std::lock_guard elock(es->mutex);
    if (!es->done) {
      ImGui::TextDisabled("Encoding MP4...");
      ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(-1, 0), "Encoding...");
      return;
    }
    if (!es->success) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
      ImGui::TextWrapped("Export failed: %s", es->error.c_str());
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
      ImGui::Text("Export saved!");
      ImGui::PopStyleColor();
      ImGui::TextWrapped("%s", es->output_path.string().c_str());
      if (ImGui::Button("Open folder")) {
        OpenFolderInExplorer(es->output_path.parent_path());
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Dismiss")) export_capture_state_.reset();
  }

  void DrawHistoryPanel() {
    auto scenario_history_to_delete = delete_history_confirmation_dialog_.Draw("Delete");
    if (scenario_history_to_delete) {
      app_.stats_manager().DeleteAllStats(*scenario_history_to_delete);
      PopSelf();
    }
    DrawHistory();
  }

  void DrawStatsTable(bool is_comparisons = false) {
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY;
    int num_cols = 6;
    if (HasAccuracyPenalty()) {
      num_cols++;
    }
    ImVec2 table_size = ImVec2(0, 0);
    if (!is_comparisons) {
      table_size.y = ImGui::GetFrameHeight() * 5;
    } else {
      table_size.y = ImGui::GetFrameHeight() * 100;
    }
    if (ImGui::BeginTable("StatsTable", num_cols, flags, table_size)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Diff", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Accuracy", ImGuiTableColumnFlags_WidthStretch);
      if (HasAccuracyPenalty()) {
        ImGui::TableSetupColumn("Penalty", ImGuiTableColumnFlags_WidthStretch);
      }
      ImGui::TableSetupColumn("CM", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthStretch);

      // Always pin current at top for scrolling.
      ImGui::TableSetupScrollFreeze(0, 2);

      ImGui::TableHeadersRow();

      DrawStatsTableRow("Current", details_.stats, details_.stats);
      if (!is_comparisons) {
        if (details_.all_stats.size() > 1) {
          DrawStatsTableRow("High score", details_.stats, details_.previous_high_score_stats);
        }
        if (details_.all_stats.size() > 2) {
          DrawStatsTableRow("Average", details_.stats, details_.average_stats);
        }

        if (details_.all_stats.size() > 1) {
          DrawStatsTableRow(
              "Previous", details_.stats, details_.all_stats[details_.all_stats.size() - 2]);
        }

      } else {
        if (details_.all_stats.size() > 1) {
          int prev_index = 1;
          for (int i = (details_.all_stats.size() - 2); i >= 0; --i) {
            if (prev_index >= 5) {
              break;
            }
            DrawStatsTableRow(
                std::format("Previous{}", prev_index), details_.stats, details_.all_stats[i]);
            prev_index++;
          }
        }
        for (const std::string& scenario : compare_to_scenarios_) {
          auto compare_stats = app_.stats_manager().GetAggregateStats(scenario);
          if (compare_stats.total_runs > 0) {
            DrawStatsTableRow(scenario, details_.stats, compare_stats.high_score_stats);
          }
        }
      }

      ImGui::EndTable();
    }
  }

  void DrawStatsTableRow(const std::string& name,
                         const StatsDbRow& current_stats,
                         const StatsDbRow& comparison_stats) {
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    if (name == scenario_name_) {
      ImGui::Text("*" + name);
    } else {
      ImGui::Text(name);
    }

    auto comparison = GetStatsComparison(current_stats, comparison_stats);

    // Percent diff
    ImGui::TableNextColumn();
    if (comparison.score_diff != 0) {
      ImGui::Text(comparison.score_diff_percent_string);
    }

    // Score
    ImGui::TableNextColumn();
    ImGui::TextFmt("{}", MaybeIntToString(comparison_stats.score, 2));
    // if (comparison.score_diff != 0) {
    //  ImGui::TextFmt(
    //      "{} ({})", MaybeIntToString(comparison_stats.score, 2), comparison.score_diff_string);

    // Accuracy
    ImGui::TableNextColumn();
    ImGui::Text(GetHitPercentageString(comparison_stats));

    if (HasAccuracyPenalty()) {
      // Penalty
      ImGui::TableNextColumn();

      float max_score = comparison_stats.info.num_hits();
      float penalty = max_score - comparison_stats.score;
      float penalty_percent = max_score > 0 ? 100 * (penalty / max_score) : 0;

      if (penalty > 0) {
        ImGui::TextFmt("{}% ({})", MaybeIntToString(penalty_percent, 2), MaybeIntToString(penalty));
      }
    }

    // cm/360
    ImGui::TableNextColumn();
    const double cm = comparison_stats.cm_per_360 > 0
                          ? comparison_stats.cm_per_360
                          : comparison_stats.mm_per_360 / 10.0;
    ImGui::TextFmt("{:.3f}", cm);

    // time
    ImGui::TableNextColumn();
    std::string time_ago;
    i64 epoch_seconds = comparison_stats.epoch_seconds;
    if (epoch_seconds > 0) {
      ImGui::Text(GetHowLongAgoStringFromEpochSeconds(epoch_seconds, GetNowEpochSeconds()));
    }
    ImGui::HelpTooltip([epoch_seconds]() { return EpochSecondsToString(epoch_seconds); });
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

  void DrawCurrentStatsPanel() {
    StatsDbRow stats = details_.stats;
    const auto& all_stats = details_.all_stats;
    float previous_high_score = details_.previous_high_score_stats.score;
    bool has_previous_high_score = all_stats.size() > 0 && previous_high_score > 0;

    float percent_diff = 0;
    std::string percent_diff_string;
    if (has_previous_high_score) {
      auto comparison = GetStatsComparison(details_.stats, details_.previous_high_score_stats);
      percent_diff = comparison.score_diff_percent;
      percent_diff_string = comparison.score_diff_percent_string;
    }
    ImGui::Spacing();
    ImGui::Spacing();
    if (percent_diff > 0) {
      auto font = app_.font_manager().UseDefault();
      ImGui::Button("NEW HIGH SCORE");
    }
    {
      auto font = app_.font_manager().UseLarge();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Score:");
      ImGui::SameLine();
      ImGui::Text(MaybeIntToString(stats.score, 2));
    }
    if (has_previous_high_score) {
      auto font = app_.font_manager().UseLarge();
      ImGui::SameLine();
      ImGui::Button(std::format("{}###pervious_high_diff", percent_diff_string));

      font.Pop();

      std::string high_score_time = GetHowLongAgoStringFromEpochSeconds(
          details_.previous_high_score_stats.epoch_seconds, GetNowEpochSeconds());
      bool is_new_high = percent_diff > 0;
      std::string prefix = is_new_high ? "Previous high score" : "Current high score";
      ImGui::HelpTooltip(std::format(
          "{}: {} ({})", prefix, MaybeIntToString(previous_high_score, 2), high_score_time));
    }
    if (evaluated_scenario_def_) {
      float score_level = GetScenarioScoreLevel(stats.score, score_target_);
      if (score_level > 0) {
        auto font1 = app_.font_manager().UseLarge();
        ImGui::SameLine();
        if (score_level < 5) {
          ImGui::Button(std::format("{}{}", MaybeIntToString(score_level, 2), icons::kBolt));
        } else {
          ImGui::Button(std::format("5 {}", icons::kVerified));
        }
        auto font2 = app_.font_manager().UseDefault();
        ImGui::HelpTooltip(std::format("Target score: {}", MaybeIntToString(score_target_, 1)));
      }
    }

    {
      auto font = app_.font_manager().UseMedium();
      if (stats.info.has_proximity_percentiles()) {
        float num_shots = stats.info.num_shots();
        float num_hits = stats.info.num_hits();
        if (num_shots > 0) {
          const auto& p = stats.info.proximity_percentiles();
          float hit_percent = (100 * num_hits) / num_shots;
          ImGui::TextFmt("{:.0f}%, {}%, {}%", hit_percent, p.p50(), p.p20());

          std::string prox_string;
          auto add_percentile = [&](const std::string& percentile,
                                    u32 percent_value,
                                    std::optional<u32> old_percent_value) {
            i32 diff = 0;
            if (old_percent_value) {
              diff = percent_value - *old_percent_value;
            }
            std::string diff_str;
            if (diff == 0) {
              // No string
            } else if (diff < 0) {
              diff_str = std::format(" ({})", diff);
            } else {
              diff_str = std::format(" (+{})", diff);
            }
            prox_string += std::format("{}th: {}%{}\n", percentile, percent_value, diff_str);
          };

          ProximityPercentiles prev_p =
              details_.previous_high_score_stats.info.proximity_percentiles();
          bool has_prev = !IsDefaultInstance(prev_p);

          float prev_hits = details_.previous_high_score_stats.info.num_hits();
          float prev_shots = details_.previous_high_score_stats.info.num_shots();

          std::optional<u32> none;
          add_percentile("100",
                         hit_percent,
                         prev_shots > 0 ? static_cast<u32>(100 * (prev_hits / prev_shots)) : none);
          add_percentile(" 90", p.p90(), has_prev ? prev_p.p90() : none);
          add_percentile(" 80", p.p80(), has_prev ? prev_p.p80() : none);
          add_percentile(" 70", p.p70(), has_prev ? prev_p.p70() : none);
          add_percentile(" 60", p.p60(), has_prev ? prev_p.p60() : none);
          add_percentile(" 50", p.p50(), has_prev ? prev_p.p50() : none);
          add_percentile(" 40", p.p40(), has_prev ? prev_p.p40() : none);
          add_percentile(" 30", p.p30(), has_prev ? prev_p.p30() : none);
          add_percentile(" 20", p.p20(), has_prev ? prev_p.p20() : none);
          add_percentile(" 10", p.p10(), has_prev ? prev_p.p10() : none);
          ImGui::SameLine();

          float large_height = ImGui::GetTextLineHeight();

          ImGui::SameLine();

          // Center the info icon vertically and make smaller
          auto smaller_font = app_.font_manager().UseDefault();
          float small_height = ImGui::GetFontSize();
          float offset = (large_height - small_height) * 0.5f;
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
          ImGui::InfoMarker(prox_string);

          ImGui::SameLine();
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
          ImGui::HelpMarker(
              "Displays percentage of time within the specified middle % of the target. The 3 "
              "values displayed are 100% (anywhere on target), 50%, 20%. 50% "
              "means time in the center half of the target.");
        }
      } else {
        std::string hit_percent = GetHitPercentageString(stats);
        if (hit_percent.size() > 0) {
          ImGui::Text(hit_percent);
        }
      }
    }
    if (has_previous_high_score) {
      std::string high_score_time = GetHowLongAgoStringFromEpochSeconds(
          details_.previous_high_score_stats.epoch_seconds, GetNowEpochSeconds());
      bool is_new_high = percent_diff > 0;
      std::string prefix = is_new_high ? "Previous high" : "Current high";
      ImGui::TextFmt(
          "{}: {} ({})", prefix, MaybeIntToString(previous_high_score, 2), high_score_time);
    }
    if (all_stats.size() > 2) {
      auto avg_comparison = GetStatsComparison(details_.stats, details_.average_stats);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Average");
      ImGui::BeginDisabled();
      ImGui::SameLine();
      ImGui::Button(std::format("{}###avg_diff_button", avg_comparison.score_diff_percent_string));
      ImGui::EndDisabled();
    }

    if (all_stats.size() > 1) {
      ImGui::TextFmt("{} total runs", all_stats.size());
    }
  }

  void DrawStatsPanel() {
    ImGui::BeginChild("StatsPanelContainer", ImVec2(0, 0));
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("StatsPanelTable", 2, flags)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      DrawCurrentStatsPanel();

      ImGui::SpacedSeparator();
      DrawStatsTable();

      DrawHistoryPlot(std::format("##ScoreHistory_{}_{}", scenario_name_, run_id_),
                      details_,
                      90,
                      score_target_);

      if (scores_over_time_) {
        ImGui::SpacedSeparator();
        DrawScoresOverTimePlot(scenario_name_, run_id_, *scores_over_time_, score_target_);
      }

      // if (compare_to_scenarios_.size() > 0) {
      //   ImGui::SpacedSeparator();
      //   if (ImGui::TreeNode("More comparisons")) {
      //     ImGui::IdGuard cid("MoreComparisons");
      //     DrawStatsTable(/*is_comparisons=*/true);
      //     ImGui::TreePop();
      //   }
      // }

      ImGui::TableNextColumn();
      if (playlist_run_) {
        PlaylistRunComponent("PlaylistRun", playlist_run_);
      }

      ImGui::EndTable();
    }
    ImGui::EndChild();
  }

  void DrawPlaylistItem(int i, const PlaylistItemProgress& progress, PlaylistRun* run) {
    bool allow_click = IsScreenOlderThan(500);
    if (ImGui::Button(progress.item.scenario()) && allow_click) {
      app_.scenario_manager().SetCurrentScenario(progress.item.scenario());
      state_.scenario_run_option = ScenarioRunOption::START_CURRENT;
      run->current_index = i;
      ReturnHome();
    }
    ImGui::SameLine();
    ImGui::TextFmt("{}/{}", progress.runs_done, progress.item.num_plays());
  }

  void DrawAnalysisPanel() {
    if (!replay_) {
      ImGui::TextWrapped("No replay data was captured for this run.");
      return;
    }

    if (!analysis_) {
      analysis_ = AnalyzeScenarioReplay(*replay_);
    }

    ImGui::BeginChild("ScenarioAnalysisPanel", ImVec2(0, 0));

    ImGui::Text("Scenario analysis");
    ImGui::SameLine();
    if (ImGui::Button("Watch replay")) {
      PushNextScreen(CreateReplayViewerScreen(replay_, &app_));
    }

    ImGui::Spacing();
    ImGui::Text("Detailed Analysis");
    ImGui::TextWrapped(analysis_->deterministic_summary);

    ImGui::Spacing();
    if (ImGui::BeginTable("AnalysisOverview", 4,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("Score");
      ImGui::TableSetupColumn("Accuracy");
      ImGui::TableSetupColumn("Clicks");
      ImGui::TableSetupColumn("Duration");
      ImGui::TableHeadersRow();
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextFmt("{:.2f}", analysis_->score);
      ImGui::TableNextColumn();
      ImGui::TextFmt("{:.1f}%", analysis_->accuracy_percent);
      ImGui::TableNextColumn();
      ImGui::TextFmt("{}", analysis_->clicks);
      ImGui::TableNextColumn();
      ImGui::TextFmt("{:.1f}s", analysis_->duration_seconds);
      ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("Weak Points");
    if (analysis_->findings.empty()) {
      ImGui::TextWrapped("No major weak point was detected from the available replay data.");
    }
    for (const auto& finding : analysis_->findings) {
      ImGui::PushID(&finding);
      ImGui::Text("[%s] %s", AnalysisSeverityLabel(finding.severity).c_str(),
                  finding.title.c_str());
      ImGui::TextWrapped("%s", finding.detail.c_str());
      if (finding.timestamp_seconds >= 0) {
        ImGui::TextDisabled("Replay time: %.2fs", finding.timestamp_seconds);
      }
      ImGui::Spacing();
      ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Text("Replay Review");
    ImGui::TextWrapped(
        "Use Watch replay to inspect the exact movement path. The analysis above is derived from "
        "the same captured replay, including click events, pitch/yaw frames, target snapshots, "
        "and score samples.");

    ImGui::Spacing();
    ImGui::Text("AI Overview");
    if (!visual_capture_state_ && !nim_state_) {
      if (IsNimConfigured()) {
        if (ImGui::Button("Watch Full Scenario with AI")) {
          visual_capture_state_ = std::make_shared<VisualReplayCaptureState>();
          visual_capture_state_->prompt = BuildNimAnalysisPrompt(*analysis_, details_.all_stats);
          PushNextScreen(
              CreateReplayViewerScreen(replay_, &app_, visual_capture_state_));
        }
      } else {
        ImGui::TextWrapped(
            "NVIDIA NIM is optional. Set NVIDIA_NIM_API_KEY to enable AI coaching. "
            "The replay video reviewer uses NVIDIA_NIM_VIDEO_MODEL, defaulting to "
            "nvidia/cosmos3-nano-reasoner.");
      }
    }

    if (visual_capture_state_) {
      std::lock_guard capture_lock(visual_capture_state_->mutex);
      if (visual_capture_state_->active) {
        ImGui::Text("Capturing the full replay for visual analysis...");
        ImGui::ProgressBar(visual_capture_state_->progress, ImVec2(-1, 0));
      } else if (!visual_capture_state_->error.empty()) {
        ImGui::TextWrapped("Replay capture failed: %s", visual_capture_state_->error.c_str());
      } else if (visual_capture_state_->nim_state) {
        nim_state_ = visual_capture_state_->nim_state;
      }
    }

    if (nim_state_) {
      std::lock_guard lock(nim_state_->mutex);
      if (!nim_state_->done) {
        ImGui::Text("Analyzing replay with NVIDIA NIM...");
      } else if (!nim_state_->success) {
        ImGui::TextWrapped("AI analysis failed: %s", nim_state_->error.c_str());
      } else {
        ImGui::TextWrapped("%s", nim_state_->response.c_str());
      }
    }

    ImGui::EndChild();
  }

  void DrawHistory() {
    DrawHistoryPlot(std::format("##FullScoreHistory_{}_{}", scenario_name_, run_id_),
                    details_,
                    60,
                    score_target_);
    DrawHistoryListTable();
  }

  void DrawHistoryListTable() {
    if (ImGui::Button("Clear history")) {
      delete_history_confirmation_dialog_.NotifyOpen(
          std::format("Delete history for \"{}\"?", scenario_name_), scenario_name_);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Sort by score");
    ImGui::SameLine();
    if (ImGui::Checkbox("##HistorySortByScore", &sort_by_score_)) {
      history_rows_ = {};
    }

    if (!history_rows_) {
      history_rows_ = std::vector<HistoryRow>();
      history_rows_->reserve(details_.all_stats.size());
      int run_number = 0;
      i64 now_micros = GetNowEpochMicros();
      for (StatsDbRow& stats : details_.all_stats) {
        run_number++;
        HistoryRow row;
        row.stats_id = stats.stats_id;
        row.run_number = run_number;
        row.score = stats.score;
        row.cm_per_360 = stats.cm_per_360 > 0 ? stats.cm_per_360 : stats.mm_per_360 / 10.0f;

        i64 time_micros = stats.epoch_seconds * 1000000;
        if (time_micros > 0) {
          row.time_ago = GetHowLongAgoStringFromEpochMicros(time_micros, now_micros);
        }
        row.timestamp = absl::FormatTime(
            "%Y-%m-%d %H:%M", absl::FromTimeT(stats.epoch_seconds), absl::LocalTimeZone());
        history_rows_->push_back(row);
      }
      if (sort_by_score_) {
        std::sort(
            history_rows_->begin(),
            history_rows_->end(),
            [](const HistoryRow& lhs, const HistoryRow& rhs) { return lhs.score > rhs.score; });
      }
    }

    bool allow_delete = IsScreenOlderThan(1300);

    float remaining_height = ImGui::GetContentRegionAvail().y;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersV | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("HistoryTable", 5, flags, ImVec2(0, remaining_height))) {
      ImGui::TableSetupColumn("Run", ImGuiTableColumnFlags_WidthFixed, char_x_ * 4);
      ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, char_x_ * 8);
      ImGui::TableSetupColumn("CM", ImGuiTableColumnFlags_WidthFixed, char_x_ * 5);
      ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, char_x_ * 12);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, char_x_ * 10);
      ImGui::TableHeadersRow();

      ImGui::LoopId loop_id;
      for (const HistoryRow& row : *history_rows_) {
        auto lid = loop_id.Get();
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextFmt("{}", row.run_number);

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextFmt("{}", MaybeIntToString(row.score, 2));

        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextFmt("{:.3f}", row.cm_per_360);

        ImGui::TableNextColumn();
        if (row.time_ago.size() > 0) {
          ImGui::AlignTextToFramePadding();
          ImGui::Text(row.time_ago);
          ImGui::HelpTooltip(row.timestamp);
        }

        ImGui::TableNextColumn();
        ImGui::SetButtonCursorAtRight(icons::kDelete);
        if (!allow_delete) {
          ImGui::BeginDisabled();
        }
        if (ImGui::Button(icons::kDelete)) {
          app_.stats_manager().DeleteStats(scenario_name_, row.stats_id);
          reset_stats_ = true;
        }
        if (!allow_delete) {
          ImGui::EndDisabled();
        }
      }

      ImGui::EndTable();
    }
  }

  void OnEvent(const SDL_Event& event, bool user_is_typing) override {
    if (IsEscapeKeyDown(event)) {
      PopSelf();
    }
    HandleDefaultScenarioEvents(event, user_is_typing, scenario_name_);
  }

 private:
  std::vector<std::string> GetCompareToList() {
    NameInfo name_info = GetNameInfo(scenario_name_);
    std::vector<NameInfo> candidate_scenarios;
    for (const std::string& candidate : app_.db().GetScenarioNamesWithPrefix(name_info.base_name)) {
      candidate_scenarios.push_back(GetNameInfo(candidate));
    }

    std::vector<std::string> result = GetSortedLevelNames(name_info, candidate_scenarios);

    if (reference_scenario_name_.size() > 0) {
      if (!VectorContains(result, reference_scenario_name_)) {
        result.push_back(reference_scenario_name_);
      }
    }

    for (const auto& candidate : candidate_scenarios) {
      if (candidate.base_name == name_info.base_name) {
        std::string name = candidate.GetFullName();
        if (!VectorContains(result, name)) {
          result.push_back(name);
        }
      }
    }
    return result;
  }

  std::string scenario_name_;
  i64 run_id_;

  StatsDetails details_;

  bool is_valid_ = false;

  float char_x_ = 0;
  std::optional<ScenarioItem> scenario_;
  std::optional<ScenarioDef> evaluated_scenario_def_;
  std::string reference_scenario_name_;

  std::optional<QuickSettingsType> show_settings_;
  std::string show_settings_release_key_;
  std::optional<RunPerformanceStats> performance_stats_;
  ImGui::ConfirmationDialog<std::string> delete_history_confirmation_dialog_{
      "DeleteHistoryConfirmationDialog"};
  std::shared_ptr<PlaylistRun> playlist_run_;

  bool reset_stats_ = false;
  bool sort_by_score_ = false;
  std::optional<std::vector<HistoryRow>> history_rows_;
  i64 screen_start_time_millis_;

  std::vector<std::string> compare_to_scenarios_;

  SelectedScreen selected_screen_ = SelectedScreen::STATS;
  std::shared_ptr<Replay> replay_;
  std::optional<ScoresOverTime> scores_over_time_;
  std::optional<ScenarioAnalysis> analysis_;
  std::shared_ptr<NimAnalysisState> nim_state_;
  std::shared_ptr<VisualReplayCaptureState> visual_capture_state_;
  bool ai_watch_pending_ = false;
  std::shared_ptr<ReplayExportCaptureState> export_capture_state_;
  std::filesystem::path txt_export_path_;
  std::string txt_export_status_;
  float score_target_ = 0;
  bool delay_display_;
  std::unique_ptr<TopBar> top_bar_ = CreateTopBar();
};

}  // namespace

std::unique_ptr<UiScreen> CreateStatsScreen(const std::string& scenario_name,
                                            i64 run_id,
                                            bool delay_display) {
  return std::make_unique<StatsScreen>(scenario_name, run_id, delay_display);
}

}  // namespace aim
