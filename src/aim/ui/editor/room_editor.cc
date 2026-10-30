#include "room_editor.h"

#include <string>

#include "aim/common/field.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/util.h"
#include "aim/proto/scenario.pb.h"
#include "aim/ui/editor/scenario_editor_common.h"
#include "glm/gtc/constants.hpp"
#include "imgui.h"
#include "aim/i18n/i18n.h"

namespace aim {

float GetDefaultSideAngleForDegrees(float degrees) {
  float min_room_degrees = 35;
  float max_room_degrees = 180;

  if (degrees < min_room_degrees) {
    // For really small rooms always make the walls very open.
    return -10;
  }

  float min_side_angle = 9;
  float max_side_angle = 80;
  if (degrees >= max_room_degrees) {
    return max_side_angle;
  }

  // Assign a side angle from min (9) to max (70) as the room degrees goes from min (35) to max
  // (180).

  float percent = (degrees - min_room_degrees) / (max_room_degrees - min_room_degrees);

  float side_angle_range = max_side_angle - min_side_angle;
  return std::roundf(min_side_angle + side_angle_range * percent);
}

void DrawRoomEditorInputs(Room& room, CameraUpdates* camera_updates) {
  ImGuiComboFlags combo_flags = 0;
  float char_x = ImGui::GetDefaultCharSizeX();

  auto type = room.type_case();
  if (ImGui::SimpleTypeDropdown("RoomTypeDropdown", &type, kRoomTypes, char_x * 15)) {
    if (type != room.type_case()) {
      if (type == Room::kSimpleRoom) {
        room = GetDefaultSimpleRoom();
      }
      if (type == Room::kCylinderRoom) {
        room = GetDefaultCylinderRoom();
      }
      if (type == Room::kBarrelRoom) {
        room = GetDefaultBarrelRoom();
      }
    }
  }

  if (room.type_case() == Room::kSimpleRoom) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Width"));
    ImGui::SameLine();
    float width = room.simple_room().width();
    ImGui::SetNextItemWidth(char_x * 12);
    ImGui::InputFloat("##RoomWidth", &width, 1, 25, "%.0f");
    room.mutable_simple_room()->set_width(width);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Height"));
    ImGui::SameLine();
    float height = room.simple_room().height();
    ImGui::SetNextItemWidth(char_x * 12);
    ImGui::InputFloat("##RoomHeight", &height, 1, 25, "%.0f");
    room.mutable_simple_room()->set_height(height);

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Depth")
                          .set_is_optional()
                          .set_default(400)
                          .set_step(1, 10)
                          .set_width(char_x * 12),
                      PROTO_FLOAT_FIELD(SimpleRoom, room.mutable_simple_room(), depth));
    ImGui::SameLine();
    ImGui::HelpMarker(Tr("If set the room will have a fixed depth and the back wall will be drawn"));
  }

  if (room.type_case() == Room::kBarrelRoom) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Radius"));
    ImGui::SameLine();
    float radius = room.barrel_room().radius();
    ImGui::SetNextItemWidth(char_x * 12);
    ImGui::InputFloat("##RoomRadius", &radius, 1, 5, "%.0f");
    room.mutable_barrel_room()->set_radius(radius);
  }

  if (room.type_case() == Room::kCylinderRoom) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Height"));
    ImGui::SameLine();
    float height = room.cylinder_room().height();
    ImGui::SetNextItemWidth(char_x * 12);
    ImGui::InputFloat("##RoomHeight", &height, 1, 10, "%.0f");

    bool use_width_degrees = room.cylinder_room().width_degrees() > 0;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Width as degrees"));
    ImGui::SameLine();
    ImGui::Checkbox("##WidthPercentCheckbox", &use_width_degrees);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Width"));
    ImGui::SameLine();
    float current_width_degrees = 0;
    if (use_width_degrees) {
      ImGui::InputFloat(
          ImGui::InputFloatParams("WidthDegrees").set_min(1).set_default(90).set_step(1, 5),
          PROTO_FLOAT_FIELD(CylinderRoom, room.mutable_cylinder_room(), width_degrees));
      room.mutable_cylinder_room()->clear_width();
      current_width_degrees = room.cylinder_room().width_degrees();
    } else {
      float width = FirstGreaterThanZero(room.cylinder_room().width(), 100);
      ImGui::SetNextItemWidth(char_x * 12);
      ImGui::InputFloat("##Width", &width, 1, 10, "%.0f");
      room.mutable_cylinder_room()->set_width(width);
      room.mutable_cylinder_room()->clear_width_degrees();
      float perimeter = room.cylinder_room().radius() * glm::two_pi<float>();
      current_width_degrees = width / perimeter;
    }

    room.mutable_cylinder_room()->set_height(height);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Radius"));
    ImGui::SameLine();
    float radius = room.cylinder_room().radius();
    ImGui::SetNextItemWidth(char_x * 12);
    ImGui::InputFloat("##RoomRadius", &radius, 1, 10, "%.0f");
    room.mutable_cylinder_room()->set_radius(radius);

    ImGui::InputFloat(
        ImGui::InputFloatParams::WithLabelAsId("Sides")
            .set_is_optional()
            .set_optional_secondary_label("angle")
            .set_default(GetDefaultSideAngleForDegrees(current_width_degrees))
            .set_step(0.5, 3)
            .set_width(char_x * 10),
        PROTO_FLOAT_FIELD(CylinderRoom, room.mutable_cylinder_room(), side_angle_degrees));
  }

  ImGui::SpacedSeparator();

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Camera position"));
  ImGui::Indent();
  VectorEditor(
      ImGui::InputFloatParams("CameraPositionVector").set_step(1, 10).set_width(char_x * 10),
      room.mutable_camera_position());
  ImGui::Unindent();

  ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Horizontal FOV")
                        .set_is_optional()
                        .set_step(1, 5)
                        .set_min(1)
                        .set_default(103)
                        .set_width(char_x * 10),
                    PROTO_FLOAT_FIELD(Room, &room, horizontal_fov));

  ImGui::Spacing();
  bool has_camera_up = room.has_camera_up();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Set camera up"));
  ImGui::SameLine();
  ImGui::Checkbox("##CameraUp", &has_camera_up);
  ImGui::SameLine();
  ImGui::HelpMarker(
      "Define up for the camera (usually the z axis). This allows you to rotate the entire "
      "scenario. (1, 0, 1) would be a 45 degree rotation.");
  if (has_camera_up) {
    if (IsZero(room.camera_up())) {
      room.mutable_camera_up()->set_z(1);
    }
    ImGui::Indent();
    VectorEditor(ImGui::InputFloatParams("CameraUpVector").set_step(0.1, 1).set_width(char_x * 10),
                 room.mutable_camera_up());
    ImGui::Unindent();
  } else {
    room.clear_camera_up();
  }

  ImGui::Spacing();
  bool has_camera_front = room.has_camera_front();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Set camera front"));
  ImGui::SameLine();
  ImGui::Checkbox("##CameraFront", &has_camera_front);
  if (has_camera_front) {
    if (IsZero(room.camera_front())) {
      room.mutable_camera_front()->set_y(1);
    }
    ImGui::Indent();
    VectorEditor(
        ImGui::InputFloatParams("CameraFrontVector").set_step(0.1, 1).set_width(char_x * 10),
        room.mutable_camera_front());
    ImGui::Unindent();
  } else {
    room.clear_camera_front();
  }

  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Look at point"));
  ImGui::SameLine();
  DrawOptionalRegionVec2Editor("LookAtPoint",
                               PROTO_PTR_FIELD(RegionVec2, Room, &room, look_at_point));

  ImGui::SpacedSeparator();
  ImGui::AlignTextToFramePadding();
  ImGui::Text("%s", Tr("Look around"));

  float yaw_per_click = 0.2;
  float pitch_per_click = 0.2;
  ImGui::SameLine();
  if (ImGui::Button(icons::kArrowLeft)) {
    camera_updates->delta_yaw -= yaw_per_click;
  }
  ImGui::SameLine();
  if (ImGui::Button(icons::kArrowDropUp)) {
    camera_updates->delta_pitch += pitch_per_click;
  }
  ImGui::SameLine();
  if (ImGui::Button(icons::kArrowDropDown)) {
    camera_updates->delta_pitch -= pitch_per_click;
  }
  ImGui::SameLine();
  if (ImGui::Button(icons::kArrowRight)) {
    camera_updates->delta_yaw += yaw_per_click;
  }
  ImGui::SameLine();
  if (ImGui::SelectableButton(icons::kClear)) {
    camera_updates->delta_pitch = 0;
    camera_updates->delta_yaw = 0;
  }
}

Room GetDefaultSimpleRoom() {
  Room r;
  r.mutable_simple_room()->set_height(130);
  r.mutable_simple_room()->set_width(150);
  *r.mutable_camera_position() = ToStoredVec3(0, -200, 0);
  return r;
}

Room GetDefaultCylinderRoom() {
  Room r;
  r.mutable_cylinder_room()->set_height(130);
  r.mutable_cylinder_room()->set_radius(200);
  r.mutable_cylinder_room()->set_width(150);
  return r;
}

Room GetDefaultBarrelRoom() {
  Room r;
  r.mutable_barrel_room()->set_radius(75);
  *r.mutable_camera_position() = ToStoredVec3(0, -200, 0);
  return r;
}

}  // namespace aim
