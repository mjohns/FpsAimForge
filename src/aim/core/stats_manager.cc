#include "stats_manager.h"

#include <map>
#include <memory>
#include <tuple>

#include "aim/common/times.h"
#include "aim/core/playlist_manager.h"

namespace aim {
namespace {

class StatsManagerImpl : public StatsManager {
 public:
  StatsManagerImpl(AimDb* db) : db_(db) {}

  void AddStats(const std::string& scenario_name, StatsDbRow* row) override {
    i64 scenario_id = db_->GetScenarioId(scenario_name);
    db_->AddStats(scenario_id, row);
    stats_cache_.erase(scenario_id);
    latest_scenario_id_ = scenario_id;
    latest_run_id_ = row->stats_id;

    // TODO: Invalidate more precisely
    highest_complete_level_cache_.clear();
  }

  std::vector<StatsDbRow> GetStats(const std::string& scenario_name) override {
    // TODO: Cache at this layer?
    i64 scenario_id = db_->GetScenarioId(scenario_name);
    return GetStats(scenario_id);
  }

  i64 GetLatestRunId(const std::string& scenario_name) override {
    i64 scenario_id = db_->GetScenarioId(scenario_name);
    return db_->GetLatestStatsId(scenario_id);
  }

  AggregateScenarioStats GetAggregateStats(const std::string& scenario_name) override {
    i64 scenario_id = db_->GetScenarioId(scenario_name);
    auto it = stats_cache_.find(scenario_id);
    if (it != stats_cache_.end()) {
      return it->second;
    }
    AggregateScenarioStats stats = GetAggregateStatsFromDb(scenario_id);
    stats_cache_[scenario_id] = stats;
    return stats;
  }

  void DeleteAllStats(const std::string& scenario_name) override {
    i64 scenario_id = db_->GetScenarioId(scenario_name);
    db_->DeleteAllStats(scenario_id);
    stats_cache_.erase(scenario_id);
  }

  void CopyAllStats(const std::string& from_scenario_name,
                    const std::string& to_scenario_name) override {}

  void DeleteStats(const std::string& scenario_name, i64 run_id) override {
    i64 scenario_id = db_->GetScenarioId(scenario_name);
    db_->DeleteStats(scenario_id, run_id);
    stats_cache_.erase(scenario_id);
  }

  bool GetStatsDetails(const std::string& scenario_name,
                       i64 run_id,
                       StatsDetails* details) override {
    auto all_stats = GetStats(scenario_name);
    details->all_stats.reserve(all_stats.size());
    details->scores.reserve(all_stats.size());

    if (all_stats.size() == 0) {
      return false;
    }

    i64 now_micros = GetNowEpochMicros();

    int found_max_index = -1;
    float max_score = 0;
    bool found_stats = false;
    details->min_score = 1000000;

    i64 average_mm_per_360 = 0;
    float average_runs_count = 0;
    for (int i = 0; i < all_stats.size(); ++i) {
      StatsDbRow& stats = all_stats[i];
      details->all_stats.push_back(stats);
      details->scores.push_back(stats.score);

      if (stats.stats_id == run_id) {
        details->stats = stats;
        found_stats = true;
        break;
      }

      {
        // Sum values for calculating the average. This will not include the current run.
        details->average_stats.score += stats.score;
        // This will overflow if done directly with the mm_per_360 i16.
        average_mm_per_360 += stats.mm_per_360;
        StatsInfo& info = details->average_stats.info;
        info.set_num_hits(info.num_hits() + stats.info.num_hits());
        info.set_num_shots(info.num_shots() + stats.info.num_shots());
        average_runs_count++;
      }

      if (stats.score >= max_score && stats.score > 0) {
        found_max_index = i;
        max_score = stats.score;
      }
      if (stats.score < details->min_score) {
        details->min_score = stats.score;
      }
    }

    if (average_runs_count > 0) {
      details->average_stats.score /= average_runs_count;

      details->average_stats.info.set_num_hits(details->average_stats.info.num_hits() /
                                               average_runs_count);
      details->average_stats.info.set_num_shots(details->average_stats.info.num_shots() /
                                                average_runs_count);
      details->average_stats.mm_per_360 = average_mm_per_360 / average_runs_count;
    }

    if (!found_stats) {
      return false;
    }

    if (found_max_index >= 0) {
      details->previous_high_score_stats = all_stats[found_max_index];
    }

    details->sorted_stats = details->all_stats;
    std::sort(details->sorted_stats.begin(),
              details->sorted_stats.end(),
              [](const StatsDbRow& lhs, const StatsDbRow& rhs) { return lhs.score < rhs.score; });

    return true;
  }

  std::optional<LatestStatsRun> GetLatestRun() override {
    if (latest_scenario_id_ < 0) {
      return {};
    }
    LatestStatsRun run;
    run.scenario_name = db_->GetScenarioName(latest_scenario_id_);
    run.run_id = latest_run_id_;
    return run;
  }

  std::optional<float> GetHighestCompleteScenarioLevel(const std::string& base_name,
                                                       float target_score) override {
    i64 id = db_->GetScenarioId(base_name);
    auto it = highest_complete_level_cache_.find({id, target_score});
    if (it != highest_complete_level_cache_.end()) {
      return it->second;
    }

    auto highest_level = db_->GetHighestCompleteScenarioLevel(base_name, target_score);
    highest_complete_level_cache_[{id, target_score}] = highest_level;

    return highest_level;
  }

 private:
  std::vector<StatsDbRow> GetStats(i64 scenario_id) {
    // TODO: Cache at this layer?
    return db_->GetStats(scenario_id);
  }

  AggregateScenarioStats GetAggregateStatsFromDb(i64 scenario_id) {
    std::vector<StatsDbRow> all_stats = GetStats(scenario_id);

    AggregateScenarioStats info;
    info.total_runs = all_stats.size();
    if (all_stats.size() == 0) {
      return info;
    }

    info.last_run_stats = all_stats.back();
    int found_max_index = 0;
    float max_score = -100000;
    for (int i = 0; i < all_stats.size(); ++i) {
      StatsDbRow& stats = all_stats[i];
      if (stats.score >= max_score) {
        found_max_index = i;
        max_score = stats.score;
      }
    }
    info.high_score_stats = all_stats[found_max_index];
    return info;
  }

  std::unique_ptr<StatsDbRow> stats_db_;
  std::unordered_map<i64, AggregateScenarioStats> stats_cache_;
  std::map<std::tuple<i64, float>, std::optional<float>> highest_complete_level_cache_;
  AimDb* db_;

  i64 latest_scenario_id_ = -1;
  i64 latest_run_id_ = -1;
};

}  // namespace

float GetScenarioScoreLevel(float score, float target_score) {
  if (target_score <= 0 || score <= 0) {
    return 0;
  }

  float start_score = target_score * 0.8;  // 1 is assigned at 80% of target
  if (score >= target_score) {
    return 5;
  }
  if (score < start_score) {
    return score / start_score;
  }

  float percent_complete = (score - start_score) / (target_score - start_score);
  return 1.0 + 4.0 * percent_complete;
}

std::unique_ptr<StatsManager> CreateStatsManager(AimDb* db) {
  return std::make_unique<StatsManagerImpl>(db);
}

}  // namespace aim
