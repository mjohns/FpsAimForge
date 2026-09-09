#include "collections.h"

#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"

namespace aim {

void ListUpdater::DrawCopyMenuItem(int i) {
  if (ImGui::Selectable(std::format("{} Copy", icons::kContentCopy))) {
    copy = i;
  }
}

void ListUpdater::DrawMoveMenuItems(int i) {
  if (ImGui::Selectable(std::format("{} Move up", icons::kArrowUpward))) {
    move_up = i;
  }
  if (ImGui::Selectable(std::format("{} Move down", icons::kArrowDownward))) {
    move_down = i;
  }
}

void ListUpdater::DrawDeleteMenuItem(int i) {
  if (ImGui::Selectable(std::format("{} Delete", icons::kDelete))) {
    remove = i;
  }
}
void ListUpdater::DrawMenuItems(int i) {
  DrawCopyMenuItem(i);
  DrawMoveMenuItems(i);
  ImGui::SpacedSeparator();
  DrawDeleteMenuItem(i);
}

}  // namespace aim
