#include "perf.h"

#include "aim/common/imgui_ext.h"
#include "imgui.h"

namespace aim {

void DumpHistogram(const TimeHistogram& h) {
  std::vector<std::tuple<std::string, std::string, i64>> values{
      {"0.1ms", "10k", h.bucket_100},
      {"0.3ms", "3.3k", h.bucket_300},
      {"0.5ms", "2k", h.bucket_500},
      {"0.7ms", "1.4k", h.bucket_700},
      {"1ms", "1k", h.bucket_1000},
      {"1.5ms", "667", h.bucket_1500},
      {"2ms", "500", h.bucket_2000},
      {"3ms", "334", h.bucket_3000},
      {"5ms", "200", h.bucket_5000},
      {"10ms", "100", h.bucket_10000},
      {"16ms", "60", h.bucket_16000},
      {"33ms", "30", h.bucket_33000},
      {"33ms+", "30", h.bucket_33000_plus},
  };

  ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders;
  if (!ImGui::BeginTable("HistogramDump", 3, flags)) {
    return;
  }
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

  i64 total = 0;
  for (int i = 0; i < values.size(); ++i) {
    total += std::get<i64>(values[i]);
  }

  std::string prev_label = "";
  std::string prev_fps_label = "";
  for (int i = 0; i < values.size(); ++i) {
    i64 value = std::get<i64>(values[i]);
    std::string current_label = std::get<0>(values[i]);
    std::string current_fps_label = std::get<1>(values[i]);
    if (value > 0) {
      ImGui::TableNextRow();
      ImGui::IdGuard id(i);
      ImGui::TableNextColumn();
      if (i == values.size() - 1) {
        ImGui::TextFmt("{} ({} fps)", current_label, current_fps_label);
      } else if (i == 0) {
        ImGui::TextFmt("0ms - {} ({}+ fps)", current_label, current_fps_label);
      } else {
        ImGui::TextFmt(
            "{} - {} ({} - {} fps)", prev_label, current_label, current_fps_label, prev_fps_label);
      }

      ImGui::TableNextColumn();
      ImGui::TextFmt("{:L}", value);

      ImGui::TableNextColumn();
      ImGui::TextFmt("{:.2f}%", (100 * value) / (double)total);
    }
    prev_label = current_label;
    prev_fps_label = current_fps_label;
  }

  ImGui::EndTable();
}

void TimeTrace::Add(const std::string& label) {
  if (!stopwatch_) {
    stopwatch_ = Stopwatch();
    stopwatch_->Start();
  }
  traces_.push_back({label, stopwatch_->GetElapsedMicros()});
}

std::vector<std::string> TimeTrace::GetTrace() {
  std::vector<std::string> result;
  result.reserve(traces_.size());

  std::string prev;
  i64 prev_time = -1;

  for (const auto& trace : traces_) {
    std::string label = std::format("{} -> {}", prev, trace.first);
    prev = trace.first;

    if (prev_time < 0) {
      // Skip item for first entry.
      prev_time = trace.second;
      continue;
    }
    i64 duration_micros = trace.second - prev_time;
    prev_time = trace.second;

    result.push_back(std::format("{}: {:.2f}s", label, duration_micros / 1000000.0f));
  }

  return result;
}

void TimeHistogram::Increment(i64 value) {
  if (value <= 0) {
    return;
  } else if (value < 100) {
    bucket_100++;
  } else if (value < 300) {
    bucket_300++;
  } else if (value < 500) {
    bucket_500++;
  } else if (value < 700) {
    bucket_700++;
  } else if (value < 1000) {
    bucket_1000++;
  } else if (value < 1500) {
    bucket_1500++;
  } else if (value < 2000) {
    bucket_2000++;
  } else if (value < 3000) {
    bucket_3000++;
  } else if (value < 5000) {
    bucket_5000++;
  } else if (value < 10000) {
    bucket_10000++;
  } else if (value < 16000) {
    bucket_16000++;
  } else if (value < 33000) {
    bucket_33000++;
  } else {
    bucket_33000_plus++;
  }
}

}  // namespace aim
