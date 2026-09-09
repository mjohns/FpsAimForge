#pragma once

#include <string>
#include <vector>

#include "aim/common/collections.h"
#include "google/protobuf/message.h"
#include "imgui.h"

namespace aim {

struct DragAndDrop {
  int dragging_i = -1;
  int move_to_i = -1;

  void DrawDragHandle(int i, const std::string& move_text);

  float GetDragWidth();

  template <typename T>
  void UpdateVector(std::vector<T>* vec) {
    if (move_to_i >= 0) {
      *vec = MoveVectorItem(*vec, dragging_i, move_to_i);
      dragging_i = -1;
      move_to_i = -1;
    }
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      dragging_i = -1;
    }
  }

  template <typename T>
  void UpdateRepeated(google::protobuf::RepeatedPtrField<T>* values) {
    if (move_to_i >= 0) {
      MoveRepeatedItem(values, dragging_i, move_to_i);
      dragging_i = -1;
      move_to_i = -1;
    }
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      dragging_i = -1;
    }
  }
};

}  // namespace aim
