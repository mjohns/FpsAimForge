#include "name_util.h"

#include <stdlib.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "aim/common/util.h"

namespace aim {
namespace {

NameInfo GetNameInfo(const std::string& name, bool support_levels = true) {
  NameInfo info;
  if (name.empty()) {
    return info;
  }

  std::vector<std::string_view> words =
      absl::StrSplit(name, absl::ByAnyChar(" \t\n\r\f\v"), absl::SkipEmpty());

  std::optional<std::string_view> last_dynamic_word;

  for (std::string_view word : std::views::reverse(words)) {
    bool is_dynamic_suffix = info.SetDynamicSuffixValue(word);
    if (!is_dynamic_suffix) {
      break;
    }
    last_dynamic_word = word;
  }

  if (!last_dynamic_word) {
    info.base_name = name;
    return info;
  }

  // Find the base name.
  int start_of_dynamic = last_dynamic_word->data() - name.data();

  int end_of_base = start_of_dynamic - 1;
  if (end_of_base >= 0 && end_of_base < name.size()) {
    info.base_name = name.substr(0, end_of_base);
  }

  return info;
}

std::vector<std::string> GetFullNames(const std::vector<NameInfo>& name_infos) {
  std::vector<std::string> result;
  for (const auto& info : name_infos) {
    result.push_back(info.GetFullName());
  }
  return result;
}

}  // namespace

std::string MakeUniqueName(const std::string& name, const std::vector<std::string>& used_names) {
  std::unordered_set<std::string> used_names_set(used_names.begin(), used_names.end());

  if (used_names_set.find(name) == used_names_set.end()) {
    return name;
  }

  int n = 1;
  while (true) {
    std::string unique_name = std::format("{} ({})", name, n);
    if (used_names_set.find(unique_name) == used_names_set.end()) {
      return unique_name;
    }

    n++;
  }
}

// Returns the level number from single words like L1, L-1.
std::optional<float> GetLevelFromWord(const std::string_view& word) {
  if (word.length() <= 1 || !word.starts_with("L")) {
    return {};
  }
  float level;
  if (!absl::SimpleAtof(word.substr(1, word.length()), &level)) {
    return {};
  }

  return level;
}

NameInfo GetNameInfo(const std::string& name) {
  return GetNameInfo(name, true);
}

std::vector<std::string> GetSortedLevelNames(const NameInfo& name,
                                             const std::vector<NameInfo>& candidates) {
  std::vector<NameInfo> names;
  for (const NameInfo& candidate : candidates) {
    if (candidate.base_name != name.base_name || candidate.cm_per_360 != name.cm_per_360) {
      continue;
    }
    if (candidate.level) {
      names.push_back(candidate);
    }
  }

  std::sort(names.begin(), names.end(), [](const NameInfo& lhs, const NameInfo& rhs) {
    float left = lhs.level.value_or(0.0f);
    float right = rhs.level.value_or(0.0f);
    return left < right;
  });

  return GetFullNames(names);
}

std::vector<std::string> GetSortedCm360Names(const NameInfo& name,
                                             const std::vector<NameInfo>& candidates) {
  std::vector<NameInfo> names;
  for (const NameInfo& candidate : candidates) {
    if (candidate.base_name != name.base_name || candidate.level != name.level) {
      continue;
    }
    if (candidate.cm_per_360 != name.cm_per_360) {
      names.push_back(candidate);
    }
  }

  std::sort(names.begin(), names.end(), [](const NameInfo& lhs, const NameInfo& rhs) {
    float left = lhs.cm_per_360.value_or(0.0f);
    float right = rhs.cm_per_360.value_or(0.0f);
    return left < right;
  });

  return GetFullNames(names);
}

std::string GetBundleName(const std::string& name) {
  size_t first_space = name.find(' ');
  if (first_space == std::string::npos) {
    return name;
  }
  return name.substr(0, first_space);
}

bool ParseFloatValueSuffix(std::string_view word, std::string_view* suffix, float* value) {
  int maybe_end_of_float = 0;
  for (int i = word.size() - 1; i >= 0; --i) {
    char c = word[i];
    bool is_suffix_char = std::isalpha(c) || c == '%';
    if (!is_suffix_char) {
      break;
    }
    maybe_end_of_float = i;
  }

  if (maybe_end_of_float == 0 || maybe_end_of_float >= word.size()) {
    // It was all characters or no characters.
    return false;
  }

  if (!absl::SimpleAtof(word.substr(0, maybe_end_of_float), value)) {
    return false;
  }

  *suffix = word.substr(maybe_end_of_float, word.size() - maybe_end_of_float);
  return true;
}

bool NameInfo::SetDynamicSuffixValue(std::string_view word) {
  if (word == "Poke") {
    is_poke = true;
    return true;
  }
  std::optional<float> parsed_level = GetLevelFromWord(word);
  if (parsed_level) {
    level = parsed_level;
    return true;
  }

  std::string_view suffix;
  float value = 0;
  if (!ParseFloatValueSuffix(word, &suffix, &value)) {
    return false;
  }

  if (suffix == "cm") {
    cm_per_360 = value;
    return true;
  }
  if (suffix == "s") {
    duration = value;
    return true;
  }
  if (suffix == "fov") {
    fov = value;
    return true;
  }
  if (suffix == "%Smaller") {
    radius_smaller = value;
    return true;
  }
  if (suffix == "%Larger") {
    radius_larger = value;
    return true;
  }
  if (suffix == "%Slower") {
    slower = value;
    return true;
  }
  if (suffix == "%Faster") {
    faster = value;
    return true;
  }
  if (suffix == "%Taller") {
    taller = value;
    return true;
  }
  if (suffix == "%Wider") {
    wider = value;
    return true;
  }
  if (suffix == "%SmallerWall") {
    wall_smaller = value;
    return true;
  }
  if (suffix == "%LargerWall") {
    wall_larger = value;
    return true;
  }

  return false;
}

std::string NameInfo::GetFullName() const {
  std::string result;
  result.reserve(base_name.size() + 30);
  result.append(base_name);

  if (is_poke) {
    result.append(" Poke");
  }
  if (level && *level != 0) {
    result.append(std::format(" L{}", MaybeIntToString(*level, 2)));
  }
  if (radius_smaller) {
    result.append(std::format(" {}%Smaller", MaybeIntToString(*radius_smaller, 2)));
  }
  if (radius_larger) {
    result.append(std::format(" {}%Larger", MaybeIntToString(*radius_larger, 2)));
  }
  if (faster) {
    result.append(std::format(" {}%Faster", MaybeIntToString(*faster, 2)));
  }
  if (slower) {
    result.append(std::format(" {}%Slower", MaybeIntToString(*slower, 2)));
  }
  if (fov) {
    result.append(std::format(" {}fov", MaybeIntToString(*fov, 0)));
  }
  if (wider) {
    result.append(std::format(" {}%Wider", MaybeIntToString(*wider, 2)));
  }
  if (taller) {
    result.append(std::format(" {}%Taller", MaybeIntToString(*taller, 2)));
  }
  if (wall_smaller) {
    result.append(std::format(" {}%SmallerWall", MaybeIntToString(*wall_smaller, 2)));
  }
  if (wall_larger) {
    result.append(std::format(" {}%LargerWall", MaybeIntToString(*wall_larger, 2)));
  }
  if (duration) {
    result.append(std::format(" {}s", MaybeIntToString(*duration, 0)));
  }
  if (cm_per_360) {
    result.append(std::format(" {}cm", MaybeIntToString(*cm_per_360, 1)));
  }
  return result;
}

void NameInfo::MergeDynamicSuffixes(const NameInfo& other) {
  if (other.level) {
    if (level) {
      level = *other.level + *level;
    } else {
      level = other.level;
    }
  }
  if (other.cm_per_360) {
    cm_per_360 = other.cm_per_360;
  }
  if (other.duration) {
    duration = other.duration;
  }
  if (other.fov) {
    fov = other.fov;
  }

  auto merge_percent_values = [](const std::optional<float>& existing,
                                 const std::optional<float>& other) {
    if (other) {
      if (existing) {
        return std::optional<float>(*existing + *other);
      } else {
        return other;
      }
    } else {
      return existing;
    }
  };
  radius_smaller = merge_percent_values(radius_smaller, other.radius_smaller);
  radius_larger = merge_percent_values(radius_larger, other.radius_larger);
  faster = merge_percent_values(faster, other.faster);
  slower = merge_percent_values(slower, other.slower);
  wider = merge_percent_values(wider, other.wider);
  taller = merge_percent_values(taller, other.taller);
  wall_larger = merge_percent_values(wall_larger, other.wall_larger);
  wall_smaller = merge_percent_values(wall_smaller, other.wall_smaller);

  if (other.is_poke) {
    is_poke = true;
  }
}

}  // namespace aim
