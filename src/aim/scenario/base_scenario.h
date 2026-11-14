#pragma once

#include <memory>

#include "aim/common/simple_types.h"
#include "aim/core/application.h"
#include "aim/database/aim_db.h"
#include "aim/scenario/scenario.h"
#include "aim/scenario/tracking_sound.h"

namespace aim {

struct ProximityStats {
  // percent to hit_micros map.
  std::unordered_map<int, i64> hit_micros_map{};
};

struct ScenarioStats {
  double num_hits = 0;
  double num_shots = 0;
  double num_kills = 0;
  Stopwatch hit_stopwatch;
  Stopwatch shot_stopwatch;
  ProximityStats proximity;
};

class BaseScenario : public Scenario {
 public:
  explicit BaseScenario(const CreateScenarioParams& params) : Scenario(params) {}
  ~BaseScenario() {}

 protected:
  virtual void FillInNewTarget(Target* target) = 0;
  virtual void UpdateTargetPositions() {}

  void Initialize() override;
  void UpdateState(UpdateStateData* data) override;
  void OnPause() override;
  void OnScenarioDone() override;
  std::optional<StatsDbRow> GetStatsRow() override;

  void AddReplayScores(float now_seconds);
  void DrawAdditionalUiElements() override;

 private:
  void HandleClickHits(UpdateStateData* data);
  void HandlePokeHits(UpdateStateData* data);
  void HandlePokeInstantHits(UpdateStateData* data);
  void HandleTrackingHits(UpdateStateData* data);
  void HandleProximityTrackingHits(UpdateStateData* data);
  void AddNewTarget(u16 old_target_id, bool no_delay = false);
  void TrackingHoldDone();
  Target GetNewTarget();
  bool ShouldCountPartialKills();
  bool ShouldLimitShotRate();
  float CalculateScore(float current_time);

  std::optional<u16> current_poke_target_id_;
  i64 current_poke_start_time_micros_ = 0;

  i64 last_proximity_tracking_update_time_micros_ = 0;

  // When limiting the rate you can click, the last time a click happened.
  i64 last_shot_time_micros_ = -99999999;  // Sufficiently negative so first click will be allowed.

  ScenarioStats stats_;

  std::unique_ptr<TrackingSound> tracking_sound_;
  std::unique_ptr<ProximityTrackingSound> proximity_tracking_sound_;
  i64 last_recorded_score_frame_ = -1;

  int available_shots_ = 0;
};

}  // namespace aim
