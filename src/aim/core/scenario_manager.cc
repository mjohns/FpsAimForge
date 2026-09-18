#include "scenario_manager.h"

#include <cassert>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "aim/common/log.h"
#include "aim/common/name_util.h"
#include "aim/common/resource_name.h"
#include "aim/scenario/scenario_overrides.h"

namespace aim {
namespace {

struct ScenarioCacheItem {
  std::string name;
  ScenarioDef def;
};

void SortScenarios(std::vector<ScenarioItem>* scenarios) {
  std::sort(scenarios->begin(),
            scenarios->end(),
            [](const ScenarioItem& lhs, const ScenarioItem& rhs) { return lhs.name < rhs.name; });
}

void SortScenarioNames(std::vector<std::string>* scenarios) {
  std::sort(scenarios->begin(),
            scenarios->end(),
            [](const std::string& lhs, const std::string& rhs) { return lhs < rhs; });
}

class ScenarioManagerImpl : public ScenarioManager {
 public:
  explicit ScenarioManagerImpl() {
    scenario_names_ = std::make_shared<std::vector<std::string>>();
  }

  void AddScenariosForBundle(const std::string& bundle_name, BundleFile* bundle_file) override {
    for (const std::string& full_name : *scenario_names_) {
      ResourceName name = ResourceName::Parse(full_name);
      if (name.bundle_name() == bundle_name) {
        BundleScenario& bundle_scenario = *bundle_file->add_scenarios();
        bundle_scenario.set_name(name.relative_name());
        *bundle_scenario.mutable_def() = scenario_map_[full_name].def;

        // Backfill
        // ScenarioDef* s = bundle_scenario.mutable_def();
      }
    }
  }

  std::vector<std::string> GetAllRelativeNamesInBundle(const std::string& bundle_name) override {
    std::vector<std::string> names;
    for (const std::string& full_name : *scenario_names_) {
      if (full_name.starts_with(bundle_name)) {
        ResourceName name = ResourceName::Parse(full_name);
        if (name.bundle_name() == bundle_name) {
          names.push_back(name.relative_name());
        }
      }
    }
    return names;
  }

  std::optional<ScenarioItem> GetScenario(const std::string& scenario_name) override {
    std::unordered_set<std::string> visited;
    return GetScenarioInternal(scenario_name, 0, &visited);
  }

  std::optional<ScenarioItem> GetScenarioInternal(const std::string& scenario_name,
                                                  int depth,
                                                  std::unordered_set<std::string>* visited) {
    auto it = scenario_map_.find(scenario_name);
    if (it != scenario_map_.end()) {
      ScenarioItem item;
      item.name = it->second.name;
      item.unevaluated_def = it->second.def;
      return item;
    }

    if (depth > 300 || visited->contains(scenario_name)) {
      return {};
    }
    visited->insert(scenario_name);

    NameInfo name_info = GetNameInfo(scenario_name);
    auto base_scenario = GetScenarioInternal(name_info.base_name, depth + 1, visited);
    if (!base_scenario) {
      return {};
    }

    ScenarioItem item = *base_scenario;
    item.name = scenario_name;
    item.name_info = name_info;
    return item;
  }

  std::unordered_set<std::string> GetDirtyBundles() override {
    return dirty_bundles_;
  }

  void ClearDirtyBundles() override {
    dirty_bundles_.clear();
  }

  const std::string& GetCurrentScenarioName() override {
    return current_scenario_name_;
  }

  void ClearCurrentScenario() override {
    current_scenario_name_ = "";
    current_running_scenario_ = {};
  }

  bool SetCurrentScenario(const std::string& scenario_name) override {
    if (scenario_name != current_scenario_name_) {
      current_running_scenario_ = {};
    }
    current_scenario_name_ = scenario_name;
    return GetScenario(scenario_name).has_value();
  }

  std::shared_ptr<std::vector<std::string>> scenario_names() const override {
    return scenario_names_;
  }

  void UpdateScenario(const std::string& name, const ScenarioDef& def) override {
    NameInfo name_info = GetNameInfo(name);
    if (name_info.HasDynamicSuffix()) {
      assert(false && "Trying to update scenario with dynamic suffix");
      return;
    }

    auto& item = scenario_map_[name];
    item.name = name;
    item.def = def;
    RebuildCachedScenarioList();
    dirty_bundles_.insert(GetBundleName(name));
  }

  // Return the name the scenario was saved with if successful.
  std::string SaveScenarioWithUniqueName(const std::string& name_in,
                                         const ScenarioDef& def) override {
    ResourceName name = ResourceName::Parse(name_in);
    *name.mutable_relative_name() =
        MakeUniqueName(name.relative_name(), GetAllRelativeNamesInBundle(name.bundle_name()));
    UpdateScenario(name.full_name(), def);
    return name.full_name();
  }

  void DeleteScenario(const std::string& name) override {
    scenario_map_.erase(name);
    RebuildCachedScenarioList();
    dirty_bundles_.insert(GetBundleName(name));
  }

  bool RenameScenario(const std::string& old_name, const std::string& new_name) override {
    if (current_scenario_name_ == old_name) {
      current_scenario_name_ = new_name;
    }

    auto old_item = scenario_map_.find(old_name);
    if (old_item != scenario_map_.end()) {
      ScenarioCacheItem& item = scenario_map_[new_name];
      item = old_item->second;
      item.name = new_name;
      scenario_map_.erase(old_name);
    }

    for (auto& listener : scenario_rename_listeners_) {
      listener(old_name, new_name);
    }

    // Fix any references to the renamed scenario.
    for (auto& entry : scenario_map_) {
      ScenarioCacheItem& item = entry.second;
      if (item.def.reference_def().scenario_name() == old_name) {
        item.def.mutable_reference_def()->set_scenario_name(new_name);
      }
    }

    dirty_bundles_.insert(GetBundleName(old_name));
    dirty_bundles_.insert(GetBundleName(new_name));
    RebuildCachedScenarioList();
    return true;
  }

  bool has_running_scenario() const override {
    return current_running_scenario_ != nullptr;
  }

  std::shared_ptr<Screen> GetCurrentRunningScenario() override {
    return current_running_scenario_;
  }

  void SetCurrentRunningScenario(std::shared_ptr<Screen> scenario) override {
    current_running_scenario_ = std::move(scenario);
  }

  void RegisterRenameListener(std::function<void(const std::string& old_name,
                                                 const std::string& new_name)> listener) override {
    scenario_rename_listeners_.push_back(std::move(listener));
  }

  std::optional<ScenarioDef> GetEvaluatedScenarioDef(const std::string& scenario_name) override {
    std::unordered_set<std::string> visited_scenarios;
    return GetEvaluatedScenarioDef(scenario_name, &visited_scenarios, /*depth=*/1);
  }

  // Gets the list of scenarios that are directly referencing the provided scenario.
  std::vector<std::string> GetReferencingScenarios(const std::string& scenario_name) override {
    std::vector<std::string> result;
    result.reserve(30);
    std::string base_name = GetNameInfo(scenario_name).base_name;
    for (const auto& entry : scenario_map_) {
      const ScenarioCacheItem& cache_item = entry.second;
      if (GetNameInfo(cache_item.def.reference_def().scenario_name()).base_name == scenario_name) {
        result.push_back(cache_item.name);
      }
    }
    return result;
  }

 private:
  std::optional<ScenarioDef> GetEvaluatedScenarioDef(
      const std::string& scenario_name,
      std::unordered_set<std::string>* visited_scenarios,
      int depth) {
    if (depth > 200) {
      Logger::get()->warn("Stopping evaluation at depth 200 for {}", scenario_name);
      return {};
    }
    bool added = visited_scenarios->insert(scenario_name).second;
    if (!added) {
      Logger::get()->warn("Stopping evaluation due to cycle for {}", scenario_name);
      return {};
    }

    auto scenario = GetScenario(scenario_name);
    if (!scenario) {
      return {};
    }

    ScenarioDef& def = scenario->unevaluated_def;

    auto get_evaluated_def = [](const ScenarioDef& def, std::optional<NameInfo> name_info) {
      bool has_level = name_info && name_info->level;

      ScenarioDef result = has_level ? ApplyScenarioLevelOverrides(def, *name_info->level)
                                     : ApplyScenarioOverrides(def);
      if (name_info) {
        ApplyDynamicSuffixOverrides(*name_info, &result);
      }
      return result;
    };

    if (!def.has_reference_def()) {
      return get_evaluated_def(def, scenario->name_info);
    }

    auto referenced =
        GetEvaluatedScenarioDef(def.reference_def().scenario_name(), visited_scenarios, depth++);
    if (!referenced) {
      return {};
    }

    ApplyReferenceFieldOverrides(def, &(*referenced));

    return get_evaluated_def(*referenced, scenario->name_info);
  }

  void StartReload() override {
    scenario_map_.clear();
  }

  void LoadScenariosFromBundle(const std::string& bundle_name, const BundleFile& bundle) override {
    for (const BundleScenario& scenario : bundle.scenarios()) {
      ResourceName name(bundle_name, scenario.name());
      auto& item = scenario_map_[name.full_name()];
      item.name = name.full_name();

      /*
      ScenarioDef s = scenario.def();
      if (s.target_def().target_order_size() > 0) {
        *s.mutable_target_def()->mutable_profiles_info()->mutable_explicit_order() =
            s.target_def().target_order();
      }
      if (s.static_def().target_placement_strategy().region_order_size() > 0) {
        *s.mutable_static_def()
             ->mutable_target_placement_strategy()
             ->mutable_regions_info()
             ->mutable_explicit_order() = s.static_def().target_placement_strategy().region_order();
      }
      if (s.wall_wander_def().target_placement_strategy().region_order_size() > 0) {
        *s.mutable_wall_wander_def()
             ->mutable_target_placement_strategy()
             ->mutable_regions_info()
             ->mutable_explicit_order() =
      s.wall_wander_def().target_placement_strategy().region_order();
      }
      if (s.waypoint_def().target_placement_strategy().region_order_size() > 0) {
        *s.mutable_waypoint_def()
             ->mutable_target_placement_strategy()
             ->mutable_regions_info()
             ->mutable_explicit_order() =
      s.waypoint_def().target_placement_strategy().region_order();
      }
      if (s.linear_def().target_placement_strategy().region_order_size() > 0) {
        *s.mutable_linear_def()
             ->mutable_target_placement_strategy()
             ->mutable_regions_info()
             ->mutable_explicit_order() = s.linear_def().target_placement_strategy().region_order();
      }
      if (s.angle_strafe_def().target_placement_strategy().region_order_size() > 0) {
        *s.mutable_angle_strafe_def()
             ->mutable_target_placement_strategy()
             ->mutable_regions_info()
             ->mutable_explicit_order() =
      s.angle_strafe_def().target_placement_strategy().region_order();
      }
      if (s.bounce_def().target_placement_strategy().region_order_size() > 0) {
        *s.mutable_bounce_def()
             ->mutable_target_placement_strategy()
             ->mutable_regions_info()
             ->mutable_explicit_order() = s.bounce_def().target_placement_strategy().region_order();
      }
      if (s.strafe_def().target_placement_strategy().region_order_size() > 0) {
        *s.mutable_strafe_def()
             ->mutable_target_placement_strategy()
             ->mutable_regions_info()
             ->mutable_explicit_order() = s.strafe_def().target_placement_strategy().region_order();
      }
      if (s.barrel_def().target_placement_strategy().region_order_size() > 0) {
        *s.mutable_barrel_def()
             ->mutable_target_placement_strategy()
             ->mutable_regions_info()
             ->mutable_explicit_order() = s.barrel_def().target_placement_strategy().region_order();
      }

      if (s.strafe_def().forward_back_profile_order_size() > 0) {
        *s.mutable_strafe_def()->mutable_forward_back_profiles_info()->mutable_explicit_order() =
            s.strafe_def().forward_back_profile_order();
      }

      dirty_bundles_.insert(bundle_name);
      item.def = s;
      */
      item.def = scenario.def();
      // dirty_bundles_.insert(bundle_name);
    }
  }

  void FinishReload() override {
    RebuildCachedScenarioList();
  }

  void UpdateCachedScenario(const std::string& name, const ScenarioDef& new_def) {
    auto& item = scenario_map_[name];
    item.name = name;
    item.def = new_def;
  }

  void RebuildCachedScenarioList() {
    auto new_scenario_names = std::make_shared<std::vector<std::string>>();
    new_scenario_names->reserve(scenario_map_.size());

    for (auto& entry : scenario_map_) {
      new_scenario_names->push_back(entry.second.name);
    }
    SortScenarioNames(new_scenario_names.get());

    scenario_names_ = new_scenario_names;
  }

  std::shared_ptr<std::vector<std::string>> scenario_names_;
  absl::flat_hash_map<std::string, ScenarioCacheItem> scenario_map_;

  std::shared_ptr<Screen> current_running_scenario_;

  std::string current_scenario_name_;
  std::unordered_set<std::string> dirty_bundles_;

  std::vector<std::function<void(const std::string& old_name, const std::string& new_name)>>
      scenario_rename_listeners_;
};

}  // namespace

std::unique_ptr<ScenarioManager> CreateScenarioManager() {
  return std::make_unique<ScenarioManagerImpl>();
}

}  // namespace aim
