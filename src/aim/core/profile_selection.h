#pragma once

#include <optional>

#include "aim/common/collections.h"
#include "aim/common/random.h"
#include "aim/proto/scenario.pb.h"
#include "google/protobuf/message_lite.h"

namespace aim {

struct ProfileSelectionContext {
  int counter = 0;
  std::optional<int> next_index{};
  std::unordered_map<int, int> rate_limited_indices{};
  // Counter used only for an initial order sequence.
  int start_counter = 0;
};

template <typename T>
std::optional<T> SelectProfile(const ProfileListInfo& profiles_info,
                               const google::protobuf::RepeatedPtrField<T>& profiles,
                               ProfileSelectionContext* context,
                               Random& rand) {
  if (profiles.size() == 0) {
    return {};
  }
  if (profiles.size() == 1) {
    return profiles[0];
  }
  const auto& start_orders = profiles_info.start_order();
  if (start_orders.size() > 0 && context->start_counter < start_orders.size()) {
    int profile_i = start_orders[context->start_counter];
    context->start_counter++;
    return profiles[ClampIndex(profiles, profile_i)];
  }

  const auto& orders = profiles_info.explicit_order();
  if (orders.size() > 0) {
    int n = context->counter;
    context->counter++;
    int order_i = n % orders.size();
    int i = orders[order_i];
    return profiles[ClampIndex(profiles, i)];
  }

  if (context->next_index.has_value()) {
    int i = ClampIndex(profiles, *context->next_index);
    context->next_index = {};

    const T& profile = profiles[i];

    if (profile.info().has_next_profile()) {
      context->next_index = profile.info().next_profile();
    }
    if (profile.info().has_min_selection_gap()) {
      context->rate_limited_indices[i] = profile.info().min_selection_gap();
    }
    return profile;
  }

  float total_weight = 0;
  for (int i = 0; i < profiles.size(); ++i) {
    if (context->rate_limited_indices[i] <= 0) {
      total_weight += profiles[i].info().weight();
    }
  }

  float roll = rand.Get(total_weight);
  float used_weight = 0;
  std::optional<T> selected_profile;
  for (int i = 0; i < profiles.size(); ++i) {
    if (context->rate_limited_indices[i] > 0) {
      context->rate_limited_indices[i]--;
      continue;
    }
    if (!selected_profile.has_value()) {
      const T& profile = profiles[i];
      used_weight += profile.info().weight();
      if (used_weight >= roll) {
        if (profile.info().has_next_profile()) {
          context->next_index = profile.info().next_profile();
        }
        if (profile.info().has_min_selection_gap()) {
          context->rate_limited_indices[i] = profile.info().min_selection_gap();
        }
        selected_profile = profile;
      }
    }
  }

  return selected_profile.value_or(profiles[0]);
}

}  // namespace aim
