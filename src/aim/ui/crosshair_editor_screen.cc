#include "crosshair_editor_screen.h"

#include <algorithm>
#include <format>
#include <optional>

#include "aim/common/files.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/name_util.h"
#include "aim/common/util.h"
#include "aim/core/settings_manager.h"
#include "aim/graphics/crosshair_renderer.h"
#include "imgui/misc/cpp/imgui_stdlib.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

const std::vector<std::pair<CrosshairLayer::TypeCase, std::string>> kCrosshairTypes{
    {CrosshairLayer::kDot, "Dot"},
    {CrosshairLayer::kPlus, "Plus"},
    {CrosshairLayer::kCircle, "Circle"},
    {CrosshairLayer::kDiamond, "Diamond"},
    {CrosshairLayer::kImage, "Image"},
};

class CrosshairEditorScreen : public UiScreen {
 public:
  explicit CrosshairEditorScreen() : UiScreen() {
    LoadCrosshairList();
  }

 protected:
  void DrawScreen() override {
    const ScreenInfo& screen = app_.screen_info();
    ImVec2 char_size = ImGui::CalcTextSize("A");
    char_x_ = char_size.x;

    BeginMainWindow("CrosshairEditor");
    notification_popup_.Draw();
    auto to_delete = delete_confirmation_dialog_.Draw("Delete");
    if (to_delete) {
      app_.settings_manager().DeleteCrosshair(*to_delete);
      LoadCrosshairList();
    }
    if (editing_single_crosshair_) {
      DrawCrosshairEditor();
    } else {
      DrawCrosshairListEditor();
    }
    ImGui::End();
  }

 private:
  bool BeginMainWindow(const std::string& name) {
    float width = app_.screen_info().width * 0.6;
    float height = app_.screen_info().height * 0.8;

    ImGui::SetNextWindowPos(ImVec2((app_.screen_info().width - width) / 2.0,
                                   (app_.screen_info().height - height) / 2.0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    return ImGui::Begin(
        name.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
  }

  void OpenExistingCrosshair(const std::string& name) {
    current_crosshair_name_ = name;
    original_crosshair_name_ = name;
    is_new_crosshair_ = false;
    crosshair_ = app_.settings_manager().GetCrosshair(name);
    editing_single_crosshair_ = true;
  }

  void OpenCrosshairCopy(const std::string& name) {
    current_crosshair_name_ = MakeUniqueName(name + " Copy", crosshair_names_);
    original_crosshair_name_ = current_crosshair_name_;
    is_new_crosshair_ = true;
    crosshair_ = app_.settings_manager().GetCrosshair(name);
    editing_single_crosshair_ = true;
  }

  void OpenNewCrosshair() {
    current_crosshair_name_ = MakeUniqueName("New crosshair", crosshair_names_);
    original_crosshair_name_ = current_crosshair_name_;
    is_new_crosshair_ = true;
    crosshair_ = GetDefaultCrosshair();
    editing_single_crosshair_ = true;
  }

  void DrawCrosshairListEditor() {
    ImGui::IdGuard cid("CrosshairListEditor");
    ImGui::LoopId loop_id;
    if (ImGui::Button(std::format("{} Back", icons::kArrowBack))) {
      PopSelf();
    }
    Line();
    if (ImGui::Button(std::format("{} Crosshair", icons::kAdd))) {
      OpenNewCrosshair();
    }
    Line();
    ImGui::BeginChild("CrosshairListContent");
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Crosshairs"));
    ImGui::SameLine();
    auto folder = app_.file_system().GetUserDataPath("resources/crosshairs");
    if (ImGui::Button(icons::kOpenInNew)) {
      OpenFolderInExplorer(folder);
    }
    ImGui::HelpTooltip(std::format("Open \"{}\"", folder.string()));
    ImGui::Spacing();
    ImGui::Indent();
    for (const std::string& crosshair_name : crosshair_names_) {
      auto lid = loop_id.Get();
      if (ImGui::Button(crosshair_name)) {
        OpenExistingCrosshair(crosshair_name);
      }
      const char* popup_id = "CrosshairItemMenu";
      if (ImGui::BeginPopupContextItem(popup_id)) {
        if (ImGui::Selectable(Tr("Copy"))) {
          OpenCrosshairCopy(crosshair_name);
        }
        if (ImGui::Selectable(Tr("Edit"))) {
          OpenExistingCrosshair(crosshair_name);
        }
        if (ImGui::Selectable(Tr("Delete"))) {
          delete_confirmation_dialog_.NotifyOpen(std::format("Delete \"{}\"?", crosshair_name),
                                                 crosshair_name);
        }
        ImGui::EndPopup();
      }
      ImGui::OpenPopupOnItemClick(popup_id, ImGuiPopupFlags_MouseButtonRight);
      ImGui::SameLine();
      if (ImGui::MenuButton()) {
        ImGui::OpenPopup(popup_id);
      }

      // Draw the crosshair too?
    }
    ImGui::Unindent();
    ImGui::EndChild();
  }

  void Line() {
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();
  }

  bool SaveCurrentCrosshair() {
    if (current_crosshair_name_.empty()) {
      notification_popup_.NotifyOpen("Missing name");
      return false;
    }
    bool crosshair_exists = app_.settings_manager().CrosshairExists(current_crosshair_name_);
    bool is_rename = !is_new_crosshair_ && current_crosshair_name_ != original_crosshair_name_;
    if (is_new_crosshair_ || is_rename) {
      if (crosshair_exists) {
        notification_popup_.NotifyOpen(
            std::format("Crosshair \"{}\" already exists", current_crosshair_name_));
        return false;
      }
    }

    if (is_rename) {
      app_.settings_manager().RenameCrosshair(original_crosshair_name_, current_crosshair_name_);
    }

    if (!app_.settings_manager().SaveCrosshair(current_crosshair_name_, crosshair_)) {
      notification_popup_.NotifyOpen(
          std::format("Unable to save crosshair \"{}\"", current_crosshair_name_));
      return false;
    }
    LoadCrosshairList();
    return true;
  }

  void LoadCrosshairList() {
    crosshair_names_ = app_.settings_manager().ListCrosshairs();
    std::sort(crosshair_names_.begin(),
              crosshair_names_.end(),
              [](const std::string& lhs, const std::string& rhs) { return lhs < rhs; });
  }

  void BackToCrosshairList() {
    crosshair_ = GetDefaultCrosshair();
    current_crosshair_name_ = "";
    is_new_crosshair_ = false;
    editing_single_crosshair_ = false;
  }

  void DrawCrosshairEditor() {
    if (ImGui::Button(std::format("{} Back", icons::kArrowBack))) {
      BackToCrosshairList();
    }
    ImGui::SameLine();
    if (ImGui::Button(std::format("{} Save", icons::kSave))) {
      if (SaveCurrentCrosshair()) {
        BackToCrosshairList();
      }
    }

    Line();

    Crosshair& c = crosshair_;

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Name"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(char_x_ * 20);
    ImGui::InputText("##NameInput", &current_crosshair_name_);

    if (c.layers_size() == 0) {
      c.add_layers();
    }

    int remove_at_i = -1;
    int move_up_i = -1;
    int move_down_i = -1;
    int copy_i = -1;
    for (int i = 0; i < c.layers_size(); ++i) {
      ImGui::IdGuard lid("Layer", i);
      const char* item_menu_id = "layer_item_menu";
      if (ImGui::BeginPopupContextItem(item_menu_id)) {
        if (ImGui::Selectable(Tr("Move up"))) {
          move_up_i = i;
        }
        if (ImGui::Selectable(Tr("Move down"))) {
          move_down_i = i;
        }
        if (ImGui::Selectable(Tr("Copy"))) {
          copy_i = i;
        }
        if (ImGui::Selectable(Tr("Delete"))) {
          remove_at_i = i;
        }
        ImGui::EndPopup();
      }

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", TrFormat("Layer {}", i + 1).c_str());
      ImGui::SameLine();
      if (ImGui::MenuButton()) {
        ImGui::OpenPopup(item_menu_id);
      }
      CrosshairLayer& l = *c.mutable_layers(i);
      ImGui::Indent();
      DrawCrosshairLayerEditor(l);
      ImGui::Unindent();
    }

    if (ImGui::Button(std::format("{} Layer", icons::kAdd))) {
      c.add_layers();
    }

    // Draw the crosshair
    ImVec2 current_pos = ImGui::GetCursorScreenPos();
    ImVec2 available = ImGui::GetContentRegionAvail();

    float height_spacing = ImGui::GetFrameHeight() * 2;
    ImVec2 center = current_pos;
    center.x += (available.x / 2.0f);
    center.y += height_spacing;

    Theme theme;
    *theme.mutable_crosshair()->mutable_color() = ToStoredColor(0.9);
    *theme.mutable_crosshair()->mutable_outline_color() = ToStoredColor(0);

    float w = available.x * 0.9;
    ImVec2 back_min = center;
    ImVec2 back_max = center;

    back_min.x -= w / 2.0;
    back_max.x += w / 2.0;

    back_min.y -= height_spacing;
    back_max.y += height_spacing;

    ImGui::GetWindowDrawList()->AddRectFilled(back_min, back_max, ToImCol32(ToStoredColor(0.3)));
    app_.crosshair_renderer().Draw(c, 30, theme, center);

    auto* list = c.mutable_layers();
    if (remove_at_i >= 0) {
      list->erase(list->begin() + remove_at_i);
    } else if (move_up_i > 0) {
      int i1 = move_up_i;
      int i2 = move_up_i - 1;
      std::swap((*list)[i1], (*list)[i2]);
    } else if (move_down_i >= 0) {
      int i1 = move_down_i;
      int i2 = move_down_i + 1;
      if (i2 < list->size()) {
        std::swap((*list)[i1], (*list)[i2]);
      }
    } else if (copy_i >= 0) {
      *list->Add() = (*list)[copy_i];
    }
  }

  void DrawCrosshairLayerEditor(CrosshairLayer& l) {
    if (ImGui::BeginTable("CrosshairLayerColumns", 2)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, char_x_ * 30);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Type"));
      ImGui::SameLine();
      CrosshairLayer::TypeCase type = l.type_case();
      if (type == CrosshairLayer::TYPE_NOT_SET) {
        type = CrosshairLayer::kDot;
      }
      ImGui::SimpleTypeDropdown("CrosshairType", &type, kCrosshairTypes, char_x_ * 12);

      ImGui::InputFloat(ImGui::InputFloatParams("ScaleInput")
                            .set_label("Scale")
                            .set_step(0.05, 0.2)
                            .set_width(char_x_ * 12)
                            .set_default(1)
                            .set_min(0.01),
                        PROTO_FLOAT_FIELD(CrosshairLayer, &l, scale));

      ImGui::InputFloat(ImGui::InputFloatParams("OpacityInput")
                            .set_label("Opacity")
                            .set_step(0.02, 0.2)
                            .set_width(char_x_ * 12)
                            .set_default(1)
                            .set_range(0.01, 1),
                        PROTO_FLOAT_FIELD(CrosshairLayer, &l, alpha));

      bool use_outline_color = l.use_outline_color() || l.circle().use_outline_color();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Use outline color"));
      ImGui::SameLine();
      ImGui::Checkbox("##UseOutline", &use_outline_color);
      l.set_use_outline_color(use_outline_color);
      ImGui::SameLine();
      ImGui::HelpMarker("Replace the layer's color with the outline color from the theme.");

      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Color"));
      bool has_color = l.has_override_color();
      ImGui::SameLine();
      ImGui::Checkbox("##HasColor", &has_color);
      ImGui::SameLine();
      ImGui::HelpMarker(Tr("Override the color defined by the theme."));
      if (has_color) {
        ImGui::SameLine();
        ImGui::InputStoredColor("##Color", l.mutable_override_color(), char_x_);
      } else {
        l.clear_override_color();
      }

      bool supports_outlines = type != CrosshairLayer::kImage;
      if (supports_outlines) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", Tr("Outline color"));
        bool has_outline_color = l.has_override_outline_color();
        ImGui::SameLine();
        ImGui::Checkbox("##HasOutlineColor", &has_outline_color);
        if (has_outline_color) {
          ImGui::SameLine();
          ImGui::InputStoredColor("##OutlineColor", l.mutable_override_outline_color(), char_x_);
        } else {
          l.clear_override_outline_color();
        }
      } else {
        l.clear_override_outline_color();
      }

      ImGui::TableNextColumn();
      if (type == CrosshairLayer::kDot) {
        DrawCrosshairDotEditor(l.mutable_dot());
      }
      if (type == CrosshairLayer::kPlus) {
        DrawCrosshairPlusEditor(l.mutable_plus());
      }
      if (type == CrosshairLayer::kDiamond) {
        DrawCrosshairDiamondEditor(l.mutable_diamond());
      }
      if (type == CrosshairLayer::kImage) {
        DrawCrosshairImageEditor(l.mutable_image());
      }
      if (type == CrosshairLayer::kCircle) {
        DrawCrosshairCircleEditor(l.mutable_circle());
      }
      ImGui::EndTable();
    }
  }

  void DrawCrosshairDotEditor(DotCrosshair* c) {
    ImGui::InputFloat(ImGui::InputFloatParams("OutlineThicknessInput")
                          .set_label("Outline thickness")
                          .set_is_optional()
                          .set_default(1)
                          .set_step(0.1, 1)
                          .set_width(char_x_ * 9)
                          .set_zero_is_unset(),
                      PROTO_FLOAT_FIELD(DotCrosshair, c, outline_thickness));
  }

  void DrawCrosshairCircleEditor(CircleCrosshair* c) {
    ImGui::IdGuard cid("CircleCrosshair");

    ImGui::InputFloat(ImGui::InputFloatParams("Thickness")
                          .set_label("Thickness")
                          .set_step(0.5, 1)
                          .set_width(char_x_ * 9)
                          .set_default(1.5)
                          .set_min(0.1),
                      PROTO_FLOAT_FIELD(CircleCrosshair, c, thickness));
  }

  void DrawCrosshairImageEditor(ImageCrosshair* c) {
    ImGui::IdGuard cid("ImageCrosshair");
    ImGui::Text("%s", Tr("File name"));
    ImGui::SameLine();
    ImGui::InputText("##FileNameInput", c->mutable_file_name());
  }

  void DrawCrosshairPlusEditor(PlusCrosshair* c) {
    ImGui::IdGuard cid("PlusCrosshair");

    ImGui::InputFloat(ImGui::InputFloatParams("HorizontalSize")
                          .set_label("Horizontal size")
                          .set_step(0.1, 0.5)
                          .set_width(char_x_ * 9)
                          .set_default(1)
                          .set_min(0),
                      PROTO_FLOAT_FIELD(PlusCrosshair, c, horizontal_size));
    ImGui::SameLine();
    ImGui::InputFloat(ImGui::InputFloatParams("HorizontalGapSize")
                          .set_label("gap")
                          .set_step(0.1, 0.5)
                          .set_width(char_x_ * 9)
                          .set_zero_is_unset()
                          .set_min(0),
                      PROTO_FLOAT_FIELD(PlusCrosshair, c, horizontal_gap_size));

    ImGui::InputFloat(ImGui::InputFloatParams("VerticalSize")
                          .set_label("Vertical size")
                          .set_step(0.1, 0.5)
                          .set_width(char_x_ * 9)
                          .set_default(1)
                          .set_min(0),
                      PROTO_FLOAT_FIELD(PlusCrosshair, c, vertical_size));

    ImGui::SameLine();
    ImGui::InputFloat(ImGui::InputFloatParams("VerticalGapSize")
                          .set_label("gap")
                          .set_step(0.1, 0.5)
                          .set_width(char_x_ * 9)
                          .set_zero_is_unset()
                          .set_min(0),
                      PROTO_FLOAT_FIELD(PlusCrosshair, c, vertical_gap_size));

    ImGui::InputFloat(ImGui::InputFloatParams("Thickness")
                          .set_label("Thickness")
                          .set_step(0.1, 1)
                          .set_width(char_x_ * 10)
                          .set_min(0.1)
                          .set_default(1),
                      PROTO_FLOAT_FIELD(PlusCrosshair, c, thickness));

    ImGui::InputFloat(ImGui::InputFloatParams("OutlineThickness")
                          .set_label("Outline thickness")
                          .set_step(0.1, 1)
                          .set_width(char_x_ * 9)
                          .set_is_optional()
                          .set_default(1),
                      PROTO_FLOAT_FIELD(PlusCrosshair, c, outline_thickness));

    ImGui::InputFloat(ImGui::InputFloatParams("Rounding")
                          .set_label("Rounding")
                          .set_step(0.5, 1)
                          .set_width(char_x_ * 9)
                          .set_min(0),
                      PROTO_FLOAT_FIELD(PlusCrosshair, c, rounding));
  }

  void DrawCrosshairDiamondEditor(DiamondCrosshair* c) {
    ImGui::IdGuard cid("DiamondCrosshair");

    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Horizontal size")
                          .set_step(0.1, 0.5)
                          .set_width(char_x_ * 9)
                          .set_default(1)
                          .set_min(0),
                      PROTO_FLOAT_FIELD(DiamondCrosshair, c, horizontal_size));
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Vertical size")
                          .set_step(0.1, 0.5)
                          .set_width(char_x_ * 9)
                          .set_default(1)
                          .set_min(0),
                      PROTO_FLOAT_FIELD(DiamondCrosshair, c, vertical_size));
    ImGui::InputFloat(ImGui::InputFloatParams::WithLabelAsId("Outline thickness")
                          .set_step(0.1, 1)
                          .set_width(char_x_ * 9)
                          .set_is_optional()
                          .set_default(1),
                      PROTO_FLOAT_FIELD(DiamondCrosshair, c, outline_thickness));
  }

  float char_x_ = 0;
  std::vector<std::string> crosshair_names_;

  Crosshair crosshair_;
  std::string original_crosshair_name_;
  std::string current_crosshair_name_;
  bool is_new_crosshair_ = false;
  ImGui::NotificationPopup notification_popup_{"Notification"};
  ImGui::ConfirmationDialog<std::string> delete_confirmation_dialog_{"DeleteConfirmationDialog"};
  bool editing_single_crosshair_ = false;
};

}  // namespace

std::unique_ptr<UiScreen> CreateCrosshairEditorScreen() {
  return std::make_unique<CrosshairEditorScreen>();
}

}  // namespace aim
