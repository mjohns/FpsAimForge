#include "scenario_analysis.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "aim/common/geometry.h"
#include "aim/common/times.h"
#include "aim/database/aim_db.h"
#include "aim/common/util.h"
#include "aim/core/camera.h"
#include "absl/time/time.h"

namespace aim {
namespace {

float ToDegrees(float radians) {
  return radians * 180.0f / 3.14159265358979323846f;
}

std::string FormatTime(float seconds) {
  return std::format("{:.2f}s", seconds);
}

void AddFinding(ScenarioAnalysis* result,
                AnalysisSeverity severity,
                std::string category,
                std::string title,
                std::string detail,
                float timestamp = -1) {
  if (result->findings.size() >= 12) return;
  result->findings.push_back(
      {severity, std::move(category), std::move(title), std::move(detail), timestamp});
}

}  // namespace

std::string AnalysisSeverityLabel(AnalysisSeverity severity) {
  switch (severity) {
    case AnalysisSeverity::CRITICAL:
      return "Critical";
    case AnalysisSeverity::WARNING:
      return "Warning";
    case AnalysisSeverity::INFO:
      return "Info";
  }
  return "Info";
}

ScenarioAnalysis AnalyzeScenarioReplay(const Replay& replay) {
  ScenarioAnalysis result;
  result.scenario_name = replay.scenario_name;
  result.duration_seconds = replay.GetDurationSeconds();
  result.cm_per_360 = replay.cm_per_360;
  result.has_target_snapshots = !replay.target_data.empty();
  if (!replay.scores.empty()) result.score = replay.scores.back();

  std::vector<float> click_intervals_ms;
  std::vector<float> mouse_speeds;
  std::vector<float> tracking_errors;
  std::vector<float> target_speeds;
  std::vector<float> recovery_times_ms;
  int tracking_samples = 0;
  int above_1deg = 0;
  int above_2deg = 0;
  int above_5deg = 0;
  float loss_start_seconds = -1.0f;
  glm::vec3 previous_target_direction{};
  bool has_previous_target = false;
  glm::vec3 previous_target_delta{};
  int target_motion_samples = 0;
  int target_direction_changes = 0;

  int miss_streak = 0;
  i64 last_click_time = -1;
  int direction_change_count = 0;
  int direction_samples = 0;
  float previous_dx = 0;
  float previous_dy = 0;

  for (const ReplayEvent& event : replay.events) {
    if (event.type != ReplayEventType::MOUSE_CLICK) continue;
    ++result.clicks;
    if (event.data.is_hit) {
      ++result.hits;
      miss_streak = 0;
    } else {
      ++result.misses;
      ++miss_streak;
      result.longest_miss_streak = std::max(result.longest_miss_streak, miss_streak);
    }
    if (last_click_time >= 0) {
      click_intervals_ms.push_back(
          static_cast<float>(event.time_micros - last_click_time) / 1000.0f);
    }
    last_click_time = event.time_micros;
  }

  result.accuracy_percent =
      result.clicks > 0 ? 100.0f * result.hits / result.clicks : 0.0f;
  if (!click_intervals_ms.empty()) {
    result.average_click_interval_ms =
        std::accumulate(click_intervals_ms.begin(), click_intervals_ms.end(), 0.0f) /
        click_intervals_ms.size();
  }

  if (replay.replay_fps > 0 && replay.pitch_yaws.size() > 1) {
    for (size_t i = 1; i < replay.pitch_yaws.size(); ++i) {
      const PitchYaw& a = replay.pitch_yaws[i - 1];
      const PitchYaw& b = replay.pitch_yaws[i];
      const float dx = b.yaw - a.yaw;
      const float dy = b.pitch - a.pitch;
      const float delta = std::sqrt(dx * dx + dy * dy);
      mouse_speeds.push_back(ToDegrees(delta) * replay.replay_fps);

      if (std::abs(dx) > 0.00001f || std::abs(dy) > 0.00001f) {
        if (direction_samples > 0 && (dx * previous_dx + dy * previous_dy) < 0) {
          ++direction_change_count;
        }
        ++direction_samples;
        previous_dx = dx;
        previous_dy = dy;
      }
    }
  }

  if (!mouse_speeds.empty()) {
    result.average_mouse_speed =
        std::accumulate(mouse_speeds.begin(), mouse_speeds.end(), 0.0f) / mouse_speeds.size();
    result.peak_mouse_speed = *std::max_element(mouse_speeds.begin(), mouse_speeds.end());
    const float threshold = std::max(180.0f, result.average_mouse_speed * 2.5f);
    int high_speed_samples = 0;
    for (float speed : mouse_speeds) {
      if (speed > threshold) ++high_speed_samples;
    }
    result.high_speed_percent =
        100.0f * high_speed_samples / mouse_speeds.size();
  }
  if (direction_samples > 0) {
    result.direction_change_rate =
        100.0f * direction_change_count / direction_samples;
  }

  if (replay.replay_fps > 0 && !replay.pitch_yaws.empty() &&
      !replay.target_metadata.empty()) {
    Camera camera(CameraParams(replay.room));
    size_t metadata_index = 0;
    size_t event_index = 0;
    std::unordered_map<u16, u16> active_targets;
    std::unordered_map<u16, ReplayTargetMetadata> metadata_by_id;
    metadata_by_id.reserve(replay.target_metadata.size());
    for (const auto& metadata : replay.target_metadata) {
      metadata_by_id.emplace(metadata.target_id, metadata);
    }

    for (size_t frame = 0; frame < replay.pitch_yaws.size(); ++frame) {
      const i64 frame_micros =
          static_cast<i64>(frame) * 1000000 / replay.replay_fps;
      const float frame_seconds =
          static_cast<float>(frame_micros) / 1000000.0f;

      while (metadata_index < replay.target_metadata.size() &&
             replay.target_metadata[metadata_index].add_time_micros <= frame_micros) {
        const auto& metadata = replay.target_metadata[metadata_index++];
        active_targets[metadata.target_id] = metadata.data_channel;
      }

      while (event_index < replay.events.size() &&
             replay.events[event_index].time_micros <= frame_micros) {
        const ReplayEvent& event = replay.events[event_index++];
        if (event.type == ReplayEventType::REMOVE_TARGET) {
          active_targets.erase(event.data.target_id);
        }
      }

      camera.UpdatePitchYaw(replay.pitch_yaws[frame]);
      const LookAtInfo look_at = camera.GetLookAt();

      float nearest_error = std::numeric_limits<float>::max();
      glm::vec3 nearest_direction{};
      for (const auto& [target_id, channel] : active_targets) {
        auto metadata_it = metadata_by_id.find(target_id);
        if (metadata_it == metadata_by_id.end()) continue;

        glm::vec3 position = metadata_it->second.initial_data.position;
        if (!replay.target_data.empty()) {
          const i64 index = static_cast<i64>(frame) * replay.num_targets + channel;
          if (index >= 0 && index < static_cast<i64>(replay.target_data.size()) &&
              replay.target_data[index].radius > 0) {
            position = replay.target_data[index].position;
          }
        }

        const glm::vec3 direction =
            glm::normalize(position - camera.GetPosition());
        const float dot = std::clamp(glm::dot(look_at.front, direction), -1.0f, 1.0f);
        const float angular_error = ToDegrees(std::acos(dot));
        if (angular_error < nearest_error) {
          nearest_error = angular_error;
          nearest_direction = direction;
        }
      }

      if (nearest_error == std::numeric_limits<float>::max()) {
        continue;
      }

      ++tracking_samples;
      tracking_errors.push_back(nearest_error);
      if (nearest_error > 1.0f) ++above_1deg;
      if (nearest_error > 2.0f) ++above_2deg;
      if (nearest_error > 5.0f) ++above_5deg;

      if (nearest_error > 2.0f) {
        if (loss_start_seconds < 0.0f) {
          loss_start_seconds = frame_seconds;
          ++result.tracking_loss_count;
        }
      } else if (loss_start_seconds >= 0.0f) {
        const float recovery_ms = (frame_seconds - loss_start_seconds) * 1000.0f;
        if (recovery_ms >= 0.0f) recovery_times_ms.push_back(recovery_ms);
        result.longest_loss_duration_seconds =
            std::max(result.longest_loss_duration_seconds,
                     frame_seconds - loss_start_seconds);
        loss_start_seconds = -1.0f;
      }

      if (nearest_error > result.worst_tracking_error) {
        result.worst_tracking_error = nearest_error;
        result.largest_error_timestamp = frame_seconds;
      }

      if (has_previous_target) {
        const glm::vec3 target_delta = nearest_direction - previous_target_direction;
        const float target_delta_angle =
            ToDegrees(std::acos(std::clamp(glm::dot(previous_target_direction,
                                                    nearest_direction),
                                          -1.0f, 1.0f)));
        target_speeds.push_back(target_delta_angle * replay.replay_fps);
        if (glm::dot(target_delta, previous_target_delta) < -0.000001f) {
          ++target_direction_changes;
        }
        if (glm::dot(target_delta, target_delta) > 0.00000001f) {
          previous_target_delta = target_delta;
          ++target_motion_samples;
        }
      }

      previous_target_direction = nearest_direction;
      has_previous_target = true;
    }

    if (loss_start_seconds >= 0.0f) {
      const float recovery_duration =
          result.duration_seconds - loss_start_seconds;
      result.longest_loss_duration_seconds =
          std::max(result.longest_loss_duration_seconds, recovery_duration);
    }

    if (tracking_samples > 0) {
      result.average_tracking_error =
          std::accumulate(tracking_errors.begin(), tracking_errors.end(), 0.0f) /
          tracking_errors.size();
      result.tracking_time_percent = 100.0f * tracking_samples /
                                     static_cast<float>(replay.pitch_yaws.size());
      result.time_above_1deg_percent =
          100.0f * above_1deg / static_cast<float>(tracking_samples);
      result.time_above_2deg_percent =
          100.0f * above_2deg / static_cast<float>(tracking_samples);
      result.time_above_5deg_percent =
          100.0f * above_5deg / static_cast<float>(tracking_samples);
    }
    if (!target_speeds.empty()) {
      result.average_target_speed =
          std::accumulate(target_speeds.begin(), target_speeds.end(), 0.0f) /
          target_speeds.size();
      result.peak_target_speed =
          *std::max_element(target_speeds.begin(), target_speeds.end());
    }
    if (!recovery_times_ms.empty()) {
      result.average_recovery_time_ms =
          std::accumulate(recovery_times_ms.begin(), recovery_times_ms.end(), 0.0f) /
          recovery_times_ms.size();
    }

    if (target_motion_samples > 0) {
      result.target_direction_change_rate =
          100.0f * target_direction_changes / static_cast<float>(target_motion_samples);
    }

    result.tracking_summary = std::format(
        "Tracking samples: {} ({:.1f}% of replay). Average target-relative error: {:.2f} deg. "
        "Worst error: {:.2f} deg at {}. Time above 1/2/5 deg: {:.1f}%/{:.1f}%/{:.1f}%. "
        "Tracking losses: {}, longest loss: {:.2f}s, average recovery: {:.0f} ms. "
        "Target speed: {:.1f} deg/s average, {:.1f} deg/s peak. Target direction changes: {:.1f}%.",
        tracking_samples, result.tracking_time_percent, result.average_tracking_error,
        result.worst_tracking_error, FormatTime(result.largest_error_timestamp),
        result.time_above_1deg_percent, result.time_above_2deg_percent,
        result.time_above_5deg_percent, result.tracking_loss_count,
        result.longest_loss_duration_seconds, result.average_recovery_time_ms,
        result.average_target_speed, result.peak_target_speed,
        result.target_direction_change_rate);
  }

  if (result.misses > 0) {
    const float miss_rate = 100.0f - result.accuracy_percent;
    AddFinding(
        &result,
        miss_rate >= 30.0f ? AnalysisSeverity::CRITICAL : AnalysisSeverity::WARNING,
        "Accuracy",
        "Miss discipline",
        std::format("{} misses across {} shots ({:.1f}% miss rate). Longest miss streak: {}.",
                    result.misses, result.clicks, miss_rate, result.longest_miss_streak));

    if (result.longest_miss_streak >= 3) {
      int streak = 0;
      for (const ReplayEvent& event : replay.events) {
        if (event.type != ReplayEventType::MOUSE_CLICK) continue;
        if (!event.data.is_hit) {
          ++streak;
          if (streak == result.longest_miss_streak) {
            AddFinding(
                &result, AnalysisSeverity::WARNING, "Failure point", "Miss streak",
                std::format("The longest miss streak reaches {} shots around {}.",
                            result.longest_miss_streak,
                            FormatTime(event.time_micros / 1000000.0f)),
                event.time_micros / 1000000.0f);
            break;
          }
        } else {
          streak = 0;
        }
      }
    }
  }

  if (result.average_click_interval_ms > 0 && result.average_click_interval_ms < 110) {
    AddFinding(&result, AnalysisSeverity::WARNING, "Pacing", "Rushed clicking",
               std::format("Average click interval is {:.0f} ms. Speed is high enough that "
                           "accuracy may be getting traded away.",
                           result.average_click_interval_ms));
  } else if (result.average_click_interval_ms > 450) {
    AddFinding(&result, AnalysisSeverity::WARNING, "Pacing", "Slow shot cycle",
               std::format("Average click interval is {:.0f} ms. There is substantial time "
                           "between shots.",
                           result.average_click_interval_ms));
  }

  if (result.high_speed_percent > 8.0f) {
    AddFinding(&result, AnalysisSeverity::WARNING, "Mouse control", "High-speed corrections",
               std::format("{:.1f}% of movement samples are above the high-speed threshold; "
                           "peak movement is {:.0f} deg/s. Review the largest flicks for "
                           "overshoot and braking.",
                           result.high_speed_percent, result.peak_mouse_speed));
  }

  if (result.direction_change_rate > 18.0f) {
    AddFinding(&result, AnalysisSeverity::WARNING, "Mouse control", "Frequent reversals",
               std::format("{:.1f}% direction-change rate suggests repeated correction cycles.",
                           result.direction_change_rate));
  }

  if (result.has_target_snapshots && result.average_tracking_error > 0) {
    const AnalysisSeverity severity =
        result.time_above_5deg_percent >= 5.0f || result.longest_loss_duration_seconds >= 1.0f
            ? AnalysisSeverity::CRITICAL
            : result.time_above_2deg_percent >= 10.0f
                  ? AnalysisSeverity::WARNING
                  : AnalysisSeverity::INFO;
    AddFinding(
        &result, severity, "Tracking", "Target-relative control",
        std::format(
            "Average error {:.2f} deg, worst {:.2f} deg at {}. "
            "{:.1f}% of tracked time was >1 deg, {:.1f}% >2 deg, {:.1f}% >5 deg. "
            "{} tracking losses; longest {:.2f}s; average recovery {:.0f} ms.",
            result.average_tracking_error, result.worst_tracking_error,
            FormatTime(result.largest_error_timestamp), result.time_above_1deg_percent,
            result.time_above_2deg_percent, result.time_above_5deg_percent,
            result.tracking_loss_count, result.longest_loss_duration_seconds,
            result.average_recovery_time_ms),
        result.largest_error_timestamp);

    if (result.time_above_2deg_percent >= 10.0f) {
      AddFinding(
          &result, AnalysisSeverity::WARNING, "Tracking", "Target separation",
          std::format("{:.1f}% of tracked time was more than 2 degrees from the target. "
                      "Focus on matching target velocity before making large corrections.",
                      result.time_above_2deg_percent));
    }
    if (result.longest_loss_duration_seconds >= 0.5f) {
      AddFinding(
          &result, AnalysisSeverity::WARNING, "Failure point", "Longest tracking loss",
          std::format("The largest sustained target separation lasted {:.2f}s around {}.",
                      result.longest_loss_duration_seconds,
                      FormatTime(result.largest_error_timestamp)),
          result.largest_error_timestamp);
    }
    if (result.average_recovery_time_ms >= 250.0f) {
      AddFinding(
          &result, AnalysisSeverity::WARNING, "Recovery", "Slow target reacquisition",
          std::format("Average recovery from >2 degree separation was {:.0f} ms.",
                      result.average_recovery_time_ms));
    }
  }

  std::ostringstream timeline;
  int shown = 0;
  for (const ReplayEvent& event : replay.events) {
    if (event.type != ReplayEventType::MOUSE_CLICK || shown >= 12) continue;
    timeline << FormatTime(event.time_micros / 1000000.0f) << " "
             << (event.data.is_hit ? "HIT" : "MISS") << "; ";
    ++shown;
  }
  result.timeline_summary = timeline.str();

  result.deterministic_summary = std::format(
      "Scenario '{}': {:.1f}s, score {:.2f}, cm/360 {:.3f}, {} hits / {} misses / {} clicks, "
      "{:.1f}% accuracy. Average click interval {:.0f} ms. Average mouse speed {:.1f} deg/s, "
      "peak {:.1f} deg/s. High-speed movement {:.1f}%. Direction-change rate {:.1f}%. {}",
      result.scenario_name, result.duration_seconds, result.score, result.cm_per_360,
      result.hits, result.misses, result.clicks, result.accuracy_percent,
      result.average_click_interval_ms, result.average_mouse_speed, result.peak_mouse_speed,
      result.high_speed_percent, result.direction_change_rate, result.tracking_summary);

  return result;
}

namespace {

std::string FormatHistoryCm360(const StatsDbRow& row) {
  const double value = row.cm_per_360 > 0 ? row.cm_per_360 : row.mm_per_360 / 10.0;
  return std::format("{:.3f}", value);
}

std::string FormatHistoryRow(const StatsDbRow& row) {
  const double shots = row.info.num_shots();
  const double hits = row.info.num_hits();
  const double accuracy = shots > 0 ? 100.0 * hits / shots : 0.0;
  // Keep persisted-history timestamps deterministic across runners and time zones.
  // The analysis prompt is machine-readable context, so it must not change based on the
  // runner local timezone. Always render the stored epoch as an explicit UTC timestamp.
  const std::string timestamp = absl::FormatTime(
      "%Y-%m-%d %H:%M:%S UTC", absl::FromUnixSeconds(row.epoch_seconds), absl::UTCTimeZone());
  return std::format(
      "run={} | date={} | score={:.3f} | cm/360={} | hits={:.1f} | shots={:.1f} | accuracy={:.2f}%",
      row.stats_id, timestamp, row.score, FormatHistoryCm360(row), hits, shots, accuracy);
}

}  // namespace

std::string BuildNimAnalysisPrompt(const ScenarioAnalysis& analysis) {
  return BuildNimAnalysisPrompt(analysis, std::span<const StatsDbRow>{});
}

std::string BuildNimAnalysisPrompt(const ScenarioAnalysis& analysis,
                                   std::span<const StatsDbRow> history) {
  std::ostringstream prompt;

  prompt <<
    "You are a friendly FPS aim coach talking directly to a player who wants to improve. "
    "Your job is to tell them IN PLAIN ENGLISH what they did wrong and how to fix it — "
    "like a coach sitting next to them, not a data scientist. "
    "NEVER use raw numbers like deg/s, degrees, ms, or percentages in your feedback. "
    "Instead say things like: 'your crosshair was drifting left of the target most of the time', "
    "'you kept losing the target after fast direction changes and took too long to get back on', "
    "'your aim is too twitchy — you're overcorrecting every small movement'. "
    "Be specific about WHAT the player felt and DID, not what the numbers say. "
    "Use only the supplied data — do not invent events.\n\n";

  prompt << "AI Overview — measured run context (internal; explain it in plain English):\n";
  prompt << analysis.deterministic_summary << "\n";
  prompt << std::format("Exact cm/360: {:.3f}\n", analysis.cm_per_360);
  prompt << "Tracking: " << analysis.tracking_summary << "\n";
  prompt << "Timeline: " << analysis.timeline_summary << "\n\n";

  prompt << "Weak Points — evidence-backed issues from this run:\n";
  if (analysis.findings.empty()) {
    prompt << "- No deterministic weak point was detected in the available replay data.\n";
  } else {
    for (const auto& finding : analysis.findings) {
      prompt << "- " << finding.title << ": " << finding.detail << "\n";
    }
  }
  prompt << "\n";

  if (!history.empty()) {
    prompt << "Recent history (" << std::min(history.size(), size_t(10)) << " runs):\n";
    const size_t start = history.size() > 10 ? history.size() - 10 : 0;
    for (size_t i = start; i < history.size(); ++i) {
      prompt << "- " << FormatHistoryRow(history[i]) << "\n";
    }
    prompt << "\n";
  }

  if (!analysis.findings.empty()) {
    prompt << "Detected issues:\n";
    for (const auto& finding : analysis.findings) {
      prompt << "- " << finding.title << ": " << finding.detail << "\n";
    }
    prompt << "\n";
  }

  prompt <<
    "Now write your coaching response using EXACTLY these section headings:\n\n"
    "AI Overview\n"
    "(A concise overall interpretation of this run, grounded in measured data.)\n\n"
    "Weak Points\n"
    "(Name only weaknesses supported by the replay; if none are supported, say so.)\n\n"
    "How It Went\n"
    "(2-3 sentences. Tell them how the run felt overall — were they mostly on target or "
    "struggling? Did they improve or get worse as the scenario went on? Talk like a coach, not a report.)\n\n"
    "What's Holding You Back\n"
    "(The main thing hurting their score. One clear problem explained in plain words. "
    "Example: 'The biggest issue is that your crosshair keeps sliding past the target "
    "when it changes direction — you're reacting a bit late and then swinging too far.')\n\n"
    "What Your Aim Feels Like Right Now\n"
    "(Describe how their aim FEELS and LOOKS from the data — smooth? Twitchy? Tense? "
    "Drifting? Sluggish? Overcorrecting? Be honest but encouraging.)\n\n"
    "What To Fix This Session\n"
    "(One or two SPECIFIC things to try RIGHT NOW. Not 'practice more tracking' — "
    "give them an actual mental cue or technique. "
    "Example: 'Try to move your crosshair SLOWER than the target feels like it needs — "
    "trust your wrist to catch up instead of chasing it.')\n\n"
    "Next Scenario To Play\n"
    "(Recommend ONE specific type of scenario to work on the weakness you identified. "
    "Be specific about why it will help them.)\n\n"
    "Do NOT use bullet points. Write in short paragraphs. "
    "Keep the whole response under 300 words. "
    "Talk directly to the player — use 'you' and 'your'.";

  return prompt.str();
}

}  // namespace aim
