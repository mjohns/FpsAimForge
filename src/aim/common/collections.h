#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include "google/protobuf/message.h"

namespace aim {

template <typename T>
bool IsValidIndex(const T& list, int i) {
  return i >= 0 && i < list.size();
}

template <typename T>
int ClampIndex(const T& list, int i) {
  if (i < 0) return 0;
  if (i < list.size()) return i;
  return list.size() - 1;
}

template <typename T>
std::optional<T> GetValueIfPresent(const std::vector<T>& list, int i) {
  return IsValidIndex(list, i) ? list[i] : std::optional<T>{};
}

template <typename T>
std::optional<T> GetValueIfPresent(const google::protobuf::RepeatedPtrField<T>& list, int i) {
  return IsValidIndex(list, i) ? list[i] : std::optional<T>{};
}

template <typename T>
std::optional<T> FindValue(const std::vector<T>& list, std::function<bool(const T&)> predicate) {
  for (const T& value : list) {
    if (predicate(value)) {
      return value;
    }
  }
  return {};
}

template <typename T>
std::optional<T> FindValue(const google::protobuf::RepeatedPtrField<T>& list,
                           std::function<bool(const T&)> predicate) {
  for (const T& value : list) {
    if (predicate(value)) {
      return value;
    }
  }
  return {};
}

template <typename T>
void PushBackAll(std::vector<T>* v, const std::vector<T>& values) {
  if (values.size() == 0) {
    return;
  }

  v->reserve(v->size() + values.size());
  v->insert(v->end(), values.begin(), values.end());
}

template <typename T>
void InsertAll(T* v, const T& values) {
  v->insert(values.begin(), values.end());
}

template <typename T>
bool VectorContains(const std::vector<T>& values, const T& value) {
  auto it = std::find(values.begin(), values.end(), value);
  return it != values.end();
}

template <typename T>
std::vector<T> MoveVectorItem(const std::vector<T>& original_values, int src_i, int dest_before_i) {
  if (!IsValidIndex(original_values, src_i)) {
    return original_values;
  }
  bool is_valid_dest = dest_before_i >= 0 && dest_before_i <= original_values.size();
  if (!is_valid_dest) {
    return original_values;
  }

  std::vector<T> result;

  // Copy over all values before dest_before_i except the item being moved.
  for (int i = 0; i < original_values.size() && i < dest_before_i; ++i) {
    if (i != src_i) {
      result.push_back(original_values[i]);
    }
  }
  result.push_back(original_values[src_i]);
  for (int i = dest_before_i; i < original_values.size(); ++i) {
    if (i != src_i) {
      result.push_back(original_values[i]);
    }
  }
  return result;
}

template <typename T>
void MoveRepeatedItem(google::protobuf::RepeatedPtrField<T>* values, int src_i, int dest_before_i) {
  std::vector<T> vec_values(values->begin(), values->end());
  auto result = MoveVectorItem(vec_values, src_i, dest_before_i);
  values->Clear();
  values->Assign(result.begin(), result.end());
}

}  // namespace aim
