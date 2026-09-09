#pragma once

#include <aim/common/util.h>

#include <string>
#include <vector>

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
};

}  // namespace aim
