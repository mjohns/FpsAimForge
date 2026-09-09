#include "history_plot.h"

#include <algorithm>
#include <cassert>

#include "absl/cleanup/cleanup.h"
#include "aim/common/collections.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/implot_ext.h"
#include "aim/common/times.h"
#include "aim/common/util.h"
#include "imgui.h"
#include "implot.h"

namespace aim {
namespace {
const ImVec4 kTopThresholdColor(0.9, 0.2, 0.4, 0.9);
const ImVec4 kMidThresholdColor(0.4, 0.4, 0.5, 0.7);
}  // namespace

void DrawHistoryPlot(const std::string& id,
                     const StatsDetails& details,
                     int max_to_show,
                     double score_target) {
  ImGui::IdGuard cid(id);
  if (details.scores.size() < 2) {
    return;
  }

  auto crust = HexToImColor("#181926");
  ImPlot::PushStyleColor(ImPlotCol_PlotBg, crust.Value);
  ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
  auto style_cleanup = absl::MakeCleanup([]() {
    ImPlot::PopStyleColor(1);
    ImPlot::PopStyleVar(1);
  });
  ImPlotFlags plot_flags = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
                           ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;
  if (!ImPlot::BeginPlot("##ScoreHistory", ImVec2(-1, 0), plot_flags)) {
    return;
  }

  std::span<const StatsDbRow> stats = std::span(details.all_stats);
  if (stats.size() > max_to_show) {
    // Take the last n items.
    stats = stats.subspan(stats.size() - max_to_show, max_to_show);
  }

  double high_score =
      std::max<double>(details.previous_high_score_stats.score, details.stats.score);
  float min_score = high_score + 1;
  for (const auto& row : stats) {
    float score = row.score;
    min_score = std::min(score, min_score);
  }

  auto axis_flags =
      ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch;
  ImPlot::SetupAxis(ImAxis_X1, "Run number", axis_flags);
  ImPlot::SetupAxis(ImAxis_Y1, "Score", axis_flags);

  ImPlot::SetupAxisLimits(ImAxis_X1, 0.5, stats.size() + 0.5, ImPlotCond_Always);

  double top_score = std::max(score_target, high_score);
  float score_range = abs(top_score - min_score);
  float vertical_padding = score_range * 0.05;
  ImPlot::SetupAxisLimits(
      ImAxis_Y1, min_score - vertical_padding, top_score + vertical_padding, ImPlotCond_Always);

  struct PlotData {
    std::span<const StatsDbRow> rows;
  };
  PlotData plot_data;
  plot_data.rows = stats;

  auto point_getter = [](int idx, void* data_ptr) {
    PlotData data = *((PlotData*)data_ptr);
    return ImPlotPoint(idx + 1, data.rows[idx].score);
  };

  ImPlot::DragLineY(0, &high_score, kTopThresholdColor, 1.0f, ImPlotDragToolFlags_NoInputs);
  if (score_target > 0) {
    ImPlot::DragLineY(1, &score_target, kMidThresholdColor, 1.0f, ImPlotDragToolFlags_NoInputs);
  }

  // ImPlot::PlotShaded("Score History", times.data(), scores.data(), scores.size(), 0);
  ImPlot::PlotLineG("Scores", point_getter, &plot_data, stats.size());
  ImPlotSpec spec;
  spec.Marker = ImPlotMarker_Circle;
  spec.MarkerSize = 3.0f;
  spec.MarkerFillColor = IMPLOT_AUTO_COL;
  spec.MarkerLineColor = IMPLOT_AUTO_COL;
  spec.LineWeight = IMPLOT_AUTO;
  ImPlot::PlotScatterG("ScorePoint", point_getter, &plot_data, stats.size(), spec);

  if (ImPlot::IsPlotHovered()) {
    ImPlotPoint mouse_pos = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);

    float plot_index = mouse_pos.x;
    int closest_index = std::round(plot_index) - 1;
    if (IsValidIndex(stats, closest_index)) {
      float x_val = closest_index + 1;
      const auto& row = stats[closest_index];
      float score = row.score;

      float vertical_distance = abs(score - mouse_pos.y) / score;

      if (ImPlot::IsPointNearMouse(mouse_pos, x_val, score, 20)) {
        ImGui::BeginTooltip();

        if (score < high_score) {
          float diff_percent = (high_score - score) / high_score;
          ImGui::TextFmt(
              "{} (-{}%)", MaybeIntToString(score, 2), MaybeIntToString(diff_percent * 100, 1));
        } else {
          ImGui::TextFmt("{} (High)", MaybeIntToString(score, 2));
        }

        std::string time_ago =
            GetHowLongAgoStringFromEpochSeconds(GetNowEpochSeconds(), row.epoch_seconds);
        ImGui::Text(time_ago);

        ImGui::EndTooltip();

        ImPlotSpec spec;
        spec.Marker = ImPlotMarker_Circle;
        spec.MarkerSize = 4.0f;
        spec.MarkerFillColor = ImVec4(1, 0, 0, 1);
        spec.MarkerLineColor = ImVec4(1, 0, 0, 1);
        spec.LineWeight = IMPLOT_AUTO;
        ImPlot::PlotScatter("MouseDot", &x_val, &score, 1, spec);
      } else {
        // See if it is near one of the drag lines and show the tooltip if so.
        ImPlotPoint threshold = ImPlot::GetPlotDistanceFromPixels(10);
        ImPlotSpec spec;
        spec.Marker = ImPlotMarker_Circle;
        spec.MarkerSize = 3.0f;
        spec.MarkerFillColor = kTopThresholdColor;
        spec.MarkerLineColor = kTopThresholdColor;
        spec.LineWeight = IMPLOT_AUTO;
        if (abs(high_score - mouse_pos.y) < threshold.y) {
          ImGui::BeginTooltip();
          ImGui::TextFmt("High score: {}", MaybeIntToString(high_score, 2));
          ImGui::EndTooltip();

          float float_high_score = high_score;
          float mouse_x = mouse_pos.x;
          ImPlot::PlotScatter("HighScoreDot", &mouse_x, &float_high_score, 1, spec);
        } else if (score_target > 0 && abs(score_target - mouse_pos.y) < threshold.y) {
          ImGui::BeginTooltip();
          ImGui::TextFmt("Target score: {}", MaybeIntToString(score_target, 2));
          ImGui::EndTooltip();

          float mouse_x = mouse_pos.x;
          float float_score_target = score_target;
          ImPlot::PlotScatter("ScoreTargetDot", &mouse_x, &float_score_target, 1, spec);
        }
      }
    }
  }

  ImPlot::EndPlot();
}

}  // namespace aim
