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

// Insert an element into a repeated field like proto.mutable_foo() at a given index.
template <typename T, typename R>
void InsertAtIndex(R* repeated_field, const T& value, int index) {
  if (index < 0) {
    index = 0;
  }
  bool is_last = index >= repeated_field->size();
  // Add at the end and then rotate down to correct index if necessary.
  *repeated_field->Add() = value;
  if (!is_last) {
    std::rotate(repeated_field->begin() + index, repeated_field->end() - 1, repeated_field->end());
  }
}

// Insert an element into a vector at a given index.
template <typename T>
void InsertAtIndex(std::vector<T>* repeated_field, const T& value, int index) {
  if (index < 0) {
    index = 0;
  }
  bool is_last = index >= repeated_field->size();
  // Add at the end and then rotate down to correct index if necessary.
  repeated_field->push_back(value);
  if (!is_last) {
    std::rotate(repeated_field->begin() + index, repeated_field->end() - 1, repeated_field->end());
  }
}

// Simplfies draw an basic editable list with menu items to delete/copy and move items.
struct ListUpdater {
  int remove = -1;
  int copy = -1;
  int move_up = -1;
  int move_down = -1;

  template <typename T>
  void Update(T* list) {
    if (remove >= 0) {
      list->erase(list->begin() + remove);
    } else if (move_up > 0) {
      int i1 = move_up;
      int i2 = move_up - 1;
      std::swap((*list)[i1], (*list)[i2]);
    } else if (move_down >= 0) {
      int i1 = move_down;
      int i2 = move_down + 1;
      if (i2 < list->size()) {
        std::swap((*list)[i1], (*list)[i2]);
      }
    } else if (copy >= 0) {
      InsertAtIndex(list, (*list)[copy], copy);
    }
  }

  template <typename T>
  void UpdateVector(std::vector<T>* list) {
    if (remove >= 0) {
      list->erase(list->begin() + remove);
    } else if (move_up > 0) {
      int i1 = move_up;
      int i2 = move_up - 1;
      std::swap((*list)[i1], (*list)[i2]);
    } else if (move_down >= 0) {
      int i1 = move_down;
      int i2 = move_down + 1;
      if (i2 < list->size()) {
        std::swap((*list)[i1], (*list)[i2]);
      }
    } else if (copy >= 0) {
      InsertAtIndex(list, (*list)[copy], copy);
    }
  }

  void DrawMenuItems(int i);

  void DrawCopyMenuItem(int i);
  void DrawMoveMenuItems(int i);
  void DrawDeleteMenuItem(int i);
};

}  // namespace aim
