#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "aim/common/object_type.h"
#include "aim/common/simple_types.h"
#include "aim/proto/scenario.pb.h"
#include "aim/proto/settings.pb.h"
#include "aim/proto/stats.pb.h"

namespace aim {

struct RecentViewV2 {
  std::string name;
  i64 view_time_micros;
};

struct StatsDbRow {
  i64 stats_id = -1;
  i64 epoch_seconds = -1;
  double score = 0;
  i16 mm_per_360 = 0;
  StatsInfo info{};
};

struct PlayTimeDetails {
  bool is_complete_run = false;
  float cm_per_360 = 0;
  ShotType::TypeCase shot_type;
};

struct PlayTimes {
  int complete_run_time_seconds = 0;
  int partial_run_time_seconds = 0;
};

struct TotalPlaytime {
  PlayTimes total;
  std::unordered_map<ShotType::TypeCase, PlayTimes> play_times_by_shot_type;
  std::unordered_map<int, PlayTimes> play_times_by_cm_per_360;
};

class AimDb {
 public:
  virtual ~AimDb() {}

  virtual std::optional<std::string> GetInitializationError() = 0;

  //
  // Playlists
  //

  virtual std::unordered_map<std::string, i64> GetPlaylistIdMap() = 0;
  virtual i64 GetPlaylistId(const std::string& name) = 0;
  virtual void RenamePlaylist(const std::string& old_name, const std::string& new_name) = 0;
  virtual std::vector<std::string> GetPlaylistNamesWithPrefix(const std::string& prefix) = 0;

  //
  // Guides
  //

  virtual std::unordered_map<std::string, i64> GetGuideIdMap() = 0;
  virtual i64 GetGuideId(const std::string& name) = 0;
  virtual void RenameGuide(const std::string& old_name, const std::string& new_name) = 0;
  virtual std::vector<std::string> GetGuideNamesWithPrefix(const std::string& prefix) = 0;

  //
  // Scenarios
  //

  virtual i64 GetScenarioId(const std::string& name) = 0;
  virtual ScenarioSettings GetScenarioSettings(i64 scenario_id) = 0;
  virtual void UpdateScenarioSettings(i64 scenario_id, ScenarioSettings settings) = 0;
  virtual std::unordered_map<std::string, i64> GetScenarioIdMap() = 0;
  virtual void RenameScenario(const std::string& old_name, const std::string& new_name) = 0;
  virtual std::vector<std::string> GetScenarioNamesWithPrefix(const std::string& prefix) = 0;
  virtual std::string GetScenarioName(i64 scenario_id) = 0;

  virtual std::optional<float> GetHighestCompleteScenarioLevel(const std::string& base_name,
                                                               float target_score) = 0;

  //
  // Stats
  //

  // The stats_id and timestamp will be filled in after insert.
  virtual bool AddStats(i64 scenario_id, StatsDbRow* row) = 0;
  virtual std::vector<StatsDbRow> GetStats(i64 scenario_id) = 0;
  virtual i64 GetLatestStatsId(i64 scenario_id) = 0;
  virtual void CopyAllStats(i64 from_scenario_id, i64 to_scenario_id) = 0;
  virtual void DeleteStats(i64 scenario_id, i64 stats_run_id) = 0;
  virtual void DeleteAllStats(i64 scenario_id) = 0;

  //
  // PlayTime
  //

  virtual bool AddPlayTime(i64 scenario_id,
                           float duration_seconds,
                           const PlayTimeDetails& details) = 0;
  virtual TotalPlaytime GetTotalPlaytime() = 0;

  //
  // RecentViews
  //

  virtual void UpdateRecentView(ObjectType type, const std::string& name) = 0;
  virtual void DeleteRecentView(ObjectType type, const std::string& name) = 0;
  virtual std::vector<RecentViewV2> GetRecentViews(ObjectType type, int limit) = 0;

  //
  // LabeledItems
  //

  virtual std::vector<std::string> GetLabeledItems(int label, ObjectType type) = 0;
  virtual void RemoveLabeledItem(int label, ObjectType type, i64 object_id) = 0;
  virtual void AddLabeledItem(int label, ObjectType type, i64 object_id) = 0;
};

std::unique_ptr<AimDb> CreateAimDb(const std::filesystem::path& db_path);

}  // namespace aim
