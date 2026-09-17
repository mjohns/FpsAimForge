#pragma once

#include <optional>
#include <string>
#include <vector>

namespace aim {

std::string MakeUniqueName(const std::string& name, const std::vector<std::string>& used_names);

struct NameInfo {
  std::string base_name;
  std::optional<float> cm_per_360;
  std::optional<float> level;
  std::optional<float> duration;
  std::optional<float> fov;
  std::optional<float> radius_smaller;
  std::optional<float> radius_larger;
  std::optional<float> faster;
  std::optional<float> slower;
  std::optional<float> wider;
  std::optional<float> taller;
  std::optional<float> wall_smaller;
  std::optional<float> wall_larger;
  bool is_poke = false;

  // Combines the base name and suffixes into the final name.
  std::string GetFullName() const;

  bool HasDynamicSuffix() const {
    return cm_per_360 || level || duration || fov || radius_smaller || radius_larger || faster ||
           slower || wider || taller || is_poke || wall_smaller || wall_larger;
  }

  // Returns whether the suffix was a valid dynamic suffix.
  bool SetDynamicSuffixValue(std::string_view word);

  void MergeDynamicSuffixes(const NameInfo& to_merge_in);
};

NameInfo GetPlaylistNameInfo(const std::string& name);
NameInfo GetScenarioNameInfo(const std::string& name);
NameInfo GetNameInfo(const std::string& name);

std::string GetBundleName(const std::string& name);

// Returns the level number from single words like L1, L-1.
std::optional<float> GetLevelFromWord(const std::string_view& word);

std::vector<std::string> GetSortedLevelNames(const NameInfo& name,
                                             const std::vector<NameInfo>& candidates);
std::vector<std::string> GetSortedCm360Names(const NameInfo& name,
                                             const std::vector<NameInfo>& candidates);

bool ParseFloatValueSuffix(std::string_view word, std::string_view* suffix, float* value);

}  // namespace aim
