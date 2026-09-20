#pragma once

#include <optional>
#include <string>
#include <vector>

#include "aim/common/simple_types.h"
#include "aim/database/aim_db.h"

namespace aim {

float GetScenarioScoreLevel(float score, float target_score);

struct AggregateScenarioStats {
  StatsDbRow high_score_stats;
  StatsDbRow last_run_stats;
  int total_runs = 0;
};

struct LatestStatsRun {
  std::string scenario_name;
  i64 run_id;
};

// Stats to display for a particular run along with relevant history.
struct StatsDetails {
  std::vector<StatsDbRow> all_stats;
  std::vector<StatsDbRow> sorted_stats;
  StatsDbRow stats;
  StatsDbRow previous_high_score_stats;
  StatsDbRow average_stats;

  std::vector<double> scores;
  float min_score = 0;
};

class StatsManager {
 public:
  virtual ~StatsManager() {}

  virtual void AddStats(const std::string& scenario_name, StatsDbRow* row) = 0;

  virtual std::vector<StatsDbRow> GetStats(const std::string& scenario_name) = 0;

  virtual i64 GetLatestRunId(const std::string& scenario_name) = 0;

  virtual std::optional<LatestStatsRun> GetLatestRun() = 0;

  virtual AggregateScenarioStats GetAggregateStats(const std::string& scenario_name) = 0;

  virtual void DeleteAllStats(const std::string& scenario_name) = 0;

  virtual void CopyAllStats(const std::string& from_scenario_name,
                            const std::string& to_scenario_name) = 0;

  virtual void DeleteStats(const std::string& scenario_name, i64 run_id) = 0;

  // Gets relevant stats for a particular run including things like the previous high score.
  virtual bool GetStatsDetails(const std::string& scenario_name,
                               i64 run_id,
                               StatsDetails* details) = 0;

  virtual std::optional<float> GetHighestCompleteScenarioLevel(const std::string& base_name,
                                                               float target_score) = 0;
};

std::unique_ptr<StatsManager> CreateStatsManager(AimDb* db);

}  // namespace aim
