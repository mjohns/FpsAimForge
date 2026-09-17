#include "select_variation_dialog.h"

#include <optional>

#include "aim/common/imgui_ext.h"
#include "aim/common/name_util.h"
#include "imgui.h"

namespace aim {

// Share with quick settings version?
void DrawSensTable(const std::string& name,
                   int start_value,
                   int num_rows,
                   std::function<void(float)> value_setter) {
  ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp;
  float char_x = ImGui::GetDefaultCharSizeX();
  if (ImGui::BeginTable(name.c_str(), 5, flags, ImVec2(char_x * 30, -1))) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, char_x);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextRow();
    for (int i = 0; i < num_rows; ++i) {
      int val1 = start_value + (10 * i);
      int val2 = val1 + 5;
      int val3 = val1 + (num_rows * 10);
      int val4 = val3 + 5;

      ImVec2 button_sz(-1, 0);
      ImGui::TableNextColumn();
      if (ImGui::Button(std::format("{}", val1), button_sz)) {
        value_setter(val1);
      }
      ImGui::TableNextColumn();
      if (ImGui::Button(std::format("{}", val2), button_sz)) {
        value_setter(val2);
      }

      ImGui::TableNextColumn();

      ImGui::TableNextColumn();
      if (ImGui::Button(std::format("{}", val3), button_sz)) {
        value_setter(val3);
      }
      ImGui::TableNextColumn();
      if (ImGui::Button(std::format("{}", val4), button_sz)) {
        value_setter(val4);
      }
    }
    ImGui::EndTable();
  }
}

bool SelectVariationDialog::Draw(std::string* updated_name) {
  ImGui::IdGuard cid("SelectVariationDialog_" + id_);
  bool selected = false;
  if (popup_.Begin()) {
    ImGui::Spacing();
    ImGui::Text(name_info_.GetFullName());

    ImGui::Spacing();

    if (ImGui::Button("Select")) {
      selected = true;
      popup_.Close();
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      NameInfo cleared;
      cleared.base_name = name_info_.base_name;
      name_info_ = cleared;
      selected = true;
      popup_.Close();
    }
    ImGui::HelpTooltip("Clear variation");

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      popup_.Close();
    }
    float char_x = ImGui::GetDefaultCharSizeX();

    ImGui::SpacedSeparator();
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Level")
                          .set_is_optional()
                          .set_step(1, 2)
                          .set_default(1)
                          .set_width(char_x * 10),
                      CreateOptionalFloatField(&name_info_.level));
    ImGui::SpacedSeparator();

    const auto default_percent_param = ImGui::InputFloatParams("BaseId")
                                           .set_is_optional()
                                           .set_step(1, 5)
                                           .set_min(1)
                                           .set_default(10)
                                           .set_width(char_x * 10);

    ImGui::InputFloat(default_percent_param.clone().set_id_and_label("Larger"),
                      CreateOptionalFloatField(&name_info_.radius_larger));

    ImGui::InputFloat(default_percent_param.clone().set_max(99).set_id_and_label("Smaller"),
                      CreateOptionalFloatField(&name_info_.radius_smaller));

    ImGui::SpacedSeparator();

    ImGui::InputFloat(default_percent_param.clone().set_id_and_label("Faster"),
                      CreateOptionalFloatField(&name_info_.faster));

    ImGui::InputFloat(default_percent_param.clone().set_max(99).set_id_and_label("Slower"),
                      CreateOptionalFloatField(&name_info_.slower));

    ImGui::SpacedSeparator();

    ImGui::InputFloat(default_percent_param.clone().set_min(-99).set_id_and_label("Wider"),
                      CreateOptionalFloatField(&name_info_.wider));

    ImGui::InputFloat(default_percent_param.clone().set_min(-99).set_id_and_label("Taller"),
                      CreateOptionalFloatField(&name_info_.taller));

    ImGui::InputFloat(default_percent_param.clone().set_id_and_label("Wall larger"),
                      CreateOptionalFloatField(&name_info_.wall_larger));
    ImGui::InputFloat(default_percent_param.clone().set_min(-99).set_id_and_label("Wall smaller"),
                      CreateOptionalFloatField(&name_info_.wall_smaller));

    ImGui::SpacedSeparator();

    ImGui::InputBool("Poke", CreateBoolField(&name_info_.is_poke));

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Duration")
                          .set_is_optional()
                          .set_step(5, 10)
                          .set_default(45)
                          .set_width(char_x * 10),
                      CreateOptionalFloatField(&name_info_.duration));

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("FOV")
                          .set_is_optional()
                          .set_step(1, 2)
                          .set_default(103)
                          .set_width(char_x * 10),
                      CreateOptionalFloatField(&name_info_.fov));
    ImGui::SpacedSeparator();

    // Sensitivity variation selection
    ImGui::Text("Sensitivity");
    ImGui::Indent();

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("cm/360")
                          .set_is_optional()
                          .set_step(1, 5)
                          .set_width(char_x * 10)
                          .set_default(45)
                          .set_min(1),
                      CreateOptionalFloatField(&name_info_.cm_per_360));

    ImGui::Spacing();

    DrawSensTable("SensTable", 10, 5, [this](float value) { name_info_.cm_per_360 = value; });

    ImGui::Unindent();
    popup_.End();
  }
  if (selected) {
    *updated_name = name_info_.GetFullName();
  }
  return selected;
}

}  // namespace aim
