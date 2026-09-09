#include "drag_and_drop.h"

#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"

namespace aim {
namespace {}

float DragAndDrop::GetDragWidth() {
  return ImGui::GetWidthWithPadding(icons::kDragIndicator);
}

void DragAndDrop::DrawDragHandle(int i, const std::string& move_text) {
  if (i == dragging_i) {
    ImGui::BeginDisabled();
    ImGui::Button(icons::kDragIndicator);
    ImGui::EndDisabled();
  } else {
    ImGui::Button(icons::kDragIndicator);
  }

  const char* type = "DRAG_AND_DROP_ITEM_TYPE";

  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(type, &i, sizeof(int));
    ImGui::TextFmt("Move \"{}\" {}", move_text, i);
    dragging_i = i;
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    ImGuiDragDropFlags drop_target_flags =
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type, drop_target_flags)) {
      ImVec2 rect_min = ImGui::GetItemRectMin();
      ImVec2 rect_max = ImGui::GetItemRectMax();

      float max_y = rect_max.y;
      float min_y = rect_min.y;
      float mouse_y = ImGui::GetMousePos().y;

      ImGuiIO& io = ImGui::GetIO();
      ImDrawList* draw_list = ImGui::GetWindowDrawList();
      float mid_y = min_y + (max_y - min_y) / 2.0;
      float draw_y = min_y;
      int dest_before_i = i;
      if (mouse_y > mid_y) {
        draw_y = max_y;
        dest_before_i++;
      }

      draw_list->AddLine(ImVec2(rect_min.x, draw_y),
                         ImVec2(rect_min.x + GetDragWidth(), draw_y),
                         ImGui::GetColorU32(ImGuiCol_DragDropTarget),
                         2.0f);

      if (payload->IsDelivery()) {
        move_to_i = dest_before_i;
      }
    }

    ImGui::EndDragDropTarget();
  }
}

}  // namespace aim
