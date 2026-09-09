#include "imgui_ext.h"

#include "aim/common/field.h"
#include "aim/common/mat_icons.h"
#include "aim/common/util.h"
#include "aim/proto/common.pb.h"
#include "imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlgpu3.h"
#include "imgui/misc/cpp/imgui_stdlib.h"

namespace ImGui {
namespace {

constexpr float kMenuButtonWidthMultiplier = 0.5;

static double RoundDouble5(double value) {
  double multiplier = 100000;
  return std::round(value * multiplier) / multiplier;
}

}  // namespace

void TextDisabled(const std::string& val) {
  TextDisabled("%s", val.c_str());
}

void TextWrapped(const std::string& val) {
  TextWrapped("%s", val.c_str());
}

void Text(const std::string& val) {
  Text("%s", val.c_str());
}

bool Button(const std::string& label, const ImVec2& size) {
  return Button(label.c_str(), size);
}

bool Selectable(const std::string& label, bool selected) {
  return Selectable(label.c_str(), selected);
}

float GetMenuButtonWidth() {
  return ImGui::CalcTextSize(aim::icons::kMoreVert).x * kMenuButtonWidthMultiplier;
}

float GetIconButtonWidth(const char* icon, float scale) {
  return ImGui::CalcTextSize(icon).x * scale;
}

bool IconButtonImpl(const char* icon, float scale, bool is_circle) {
  // auto cleanup = absl::MakeCleanup([]() { ImGui::PopStyleVar(2); });

  auto full_size = ImGui::CalcTextSize(icon);
  // full_size.y = ImGui::GetFrameHeight();
  auto size = full_size;
  size.x *= scale;

  auto pos = GetCursorPos();
  pos.x -= ((full_size.x - size.x) * 0.5);
  IdGuard cid(std::format("SelectIcon{}", icon));
  if (is_circle) {
    ImVec2 frame_padding = ImGui::GetStyle().FramePadding;
    float total_width = size.x + 2 * frame_padding.x;
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, total_width / 2.0f);
  }
  bool selected = Selectable("##SelectableIcon", false, 0, size);
  if (is_circle) {
    ImGui::PopStyleVar();
  }
  ImGui::SameLine();
  ImGui::SetCursorPos(pos);
  ImGui::Text("%s", icon);
  return selected;
}

bool IconButton(const char* icon, float scale) {
  return IconButtonImpl(icon, scale, false);
}

bool CircleIconButton(const char* icon, float scale) {
  return IconButtonImpl(icon, scale, true);
}

bool MenuButton() {
  // auto cleanup = absl::MakeCleanup([]() { ImGui::PopStyleVar(2); });

  auto full_size = ImGui::CalcTextSize(aim::icons::kMoreVert);
  // full_size.y = ImGui::GetFrameHeight();
  auto size = full_size;
  size.x *= kMenuButtonWidthMultiplier;

  // bool selected = ImGui::InvisibleButton("##menu_button", size);
  // ImVec2 pos = ImGui::GetCursorScreenPos();
  // if (ImGui::IsItemHovered()) {
  //   ImU32 hover_color = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
  //   ImGui::GetWindowDrawList()->AddRectFilled(
  //       pos, ImVec2(pos.x + size.x, pos.y + size.y), hover_color);
  // }
  //
  // ImGui::SetCursorScreenPos(pos);
  // ImGui::Text("%s", aim::icons::kMoreVert);
  //
  // return selected;
  //
  auto pos = GetCursorPos();
  pos.x -= ((full_size.x - size.x) * 0.5);
  bool selected = Selectable("##selectable_menu", false, 0, size);
  ImGui::SameLine();
  ImGui::SetCursorPos(pos);
  ImGui::Text("%s", aim::icons::kMoreVert);
  return selected;
}

bool ClearButton() {
  return IconButton(aim::icons::kClear);
}

bool SimpleDropdown(const std::string& id,
                    std::string* value,
                    const std::vector<std::string>& values,
                    float input_width,
                    int* selected_index,
                    bool* opened) {
  ImGui::IdGuard cid(id);
  if (input_width > 0) {
    ImGui::PushItemWidth(input_width);
  }
  bool item_was_selected = false;
  ImGuiComboFlags combo_flags = ImGuiComboFlags_HeightLarge;
  if (opened != nullptr) {
    *opened = false;
  }
  if (ImGui::BeginCombo("##Combo", value->c_str(), combo_flags)) {
    if (opened != nullptr) {
      *opened = true;
    }
    for (int i = 0; i < values.size(); ++i) {
      ImGui::IdGuard lid(i);
      const auto& item = values[i];
      bool is_selected = item == *value;
      if (ImGui::Selectable(item.c_str(), is_selected)) {
        *value = item;
        item_was_selected = true;
        if (selected_index != nullptr) {
          *selected_index = i;
        }
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  if (input_width > 0) {
    ImGui::PopItemWidth();
  }
  return item_was_selected;
}

void NotificationPopup::NotifyOpen(const std::string& text) {
  text_ = text;
  popup_.Open();
}

bool NotificationPopup::Draw() {
  bool confirmed = false;
  if (popup_.Begin()) {
    ImGui::Text(text_);

    float button_width = ImGui::CalcTextSize("OK").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - button_width) * 0.5f);

    if (ImGui::Button("Ok")) {
      confirmed = true;
      text_ = "";
      popup_.Close();
    }
    popup_.End();
  }
  return confirmed;
}

void HelpMarker(const std::string& text) {
  ImGui::TextDisabled("%s", aim::icons::kHelp);
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::Text(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void InfoMarker(const std::string& text) {
  ImGui::Text("%s", aim::icons::kInfo);
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::Text(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void HelpTooltip(const std::string& text) {
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::Text(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void HelpTooltip(std::function<std::string()> get_text) {
  if (ImGui::BeginItemTooltip()) {
    std::string text = get_text();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::Text(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void DrawItemBounds() {
  ImVec2 rect_min = ImGui::GetItemRectMin();
  ImVec2 rect_max = ImGui::GetItemRectMax();

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  draw_list->AddLine(ImVec2(rect_min.x, rect_min.y),
                     ImVec2(rect_max.x, rect_min.y),
                     ImGui::GetColorU32(ImGuiCol_DragDropTarget),
                     2.0f);
  draw_list->AddLine(ImVec2(rect_min.x, rect_max.y),
                     ImVec2(rect_max.x, rect_max.y),
                     ImGui::GetColorU32(ImGuiCol_DragDropTarget),
                     2.0f);
}

void SetCursorAtBottom(float item_height) {
  if (item_height < 0) {
    item_height = ImGui::GetFrameHeight();
  }
  float content_region_avail_height = ImGui::GetContentRegionAvail().y;
  float bottom_target_y = ImGui::GetCursorPosY() + content_region_avail_height - item_height;
  ImGui::SetCursorPosY(bottom_target_y);
}

void SetCursorAtRight(float item_width) {
  float available = ImGui::GetContentRegionAvail().x;
  float target_x = ImGui::GetCursorPosX() + available - item_width;
  ImGui::SetCursorPosX(target_x);
}

void SetButtonCursorAtRight(const std::string& text) {
  float size = ImGui::CalcTextSize(text.c_str()).x;
  SetCursorAtRight(size + 2.0f * ImGui::GetStyle().FramePadding.x);
}

float GetWidthWithPadding(const std::string& text) {
  float size = ImGui::CalcTextSize(text.c_str()).x;
  return size + 2.0f * ImGui::GetStyle().FramePadding.x;
}

void InputFloat(const InputFloatParams& params, aim::Field<float> field) {
  IdGuard cid(params.id);
  if (params.label.size() > 0) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text(params.label);
    ImGui::SameLine();
  }
  if (params.is_optional) {
    bool has_field = field.has();
    ImGui::Checkbox("##HasField", &has_field);
    if (!has_field) {
      field.clear();
      return;
    }
    if (params.optional_secondary_label.size() > 0) {
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::Text(params.optional_secondary_label);
    }
    ImGui::SameLine();
  }

  double value = field.get();
  if (params.default_value.has_value() && !field.has()) {
    value = *params.default_value;
  }
  if (params.width > 0) {
    ImGui::SetNextItemWidth(params.width);
  }
  ImGui::InputDouble("##ValueInput", &value, params.step, params.fast_step, params.format);

  if (params.min_value.has_value()) {
    if (value < *params.min_value) {
      value = *params.min_value;
    }
  }
  if (params.max_value.has_value()) {
    if (value > *params.max_value) {
      value = *params.max_value;
    }
  }

  if (params.zero_is_unset) {
    if (value > 0) {
      field.set(RoundDouble5(value));
    } else {
      field.clear();
    }
  } else {
    field.set(RoundDouble5(value));
  }
}

void InputJitteredFloat(const InputFloatParams& params, aim::JitteredField<float> field) {
  InputFloat(params, field.value);

  InputFloatParams jitter_params(params.id + "JitterInput");
  jitter_params.set_label("")
      .set_min(0)
      .set_is_optional(false)
      .set_zero_is_unset()
      .set_step(params.step, params.fast_step)
      .set_width(params.width);
  jitter_params.format = params.format;

  if (!params.is_optional || field.value.has()) {
    ImGui::SameLine();
    ImGui::Text("+/-");
    ImGui::SameLine();
    InputFloat(jitter_params, field.jitter);
  } else {
    field.jitter.clear();
  }
}

void InputBool(const InputBoolParams& params, aim::Field<bool> field) {
  IdGuard cid(params.id);
  if (params.label.size() > 0) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text(params.label);
    ImGui::SameLine();
  }

  bool value = field.get();
  ImGui::Checkbox("##Checkbox", &value);
  field.set(value);
}

void InputBool(const std::string& label, aim::Field<bool> field) {
  InputBool(ImGui::InputBoolParams::WithLabelAsId(label), field);
}

void InputInt(const InputIntParams& params, aim::Field<int> field) {
  IdGuard cid(params.id);
  if (params.label.size() > 0) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text(params.label);
    ImGui::SameLine();
  }
  if (params.is_optional) {
    bool has_field = field.has();
    ImGui::Checkbox("##HasField", &has_field);
    if (!has_field) {
      field.clear();
      return;
    }
    ImGui::SameLine();
  }

  int value = field.get();
  if (params.default_value.has_value() && !field.has()) {
    value = *params.default_value;
  }
  if (params.width > 0) {
    ImGui::SetNextItemWidth(params.width);
  }
  ImGui::InputInt("##ValueInput", &value, params.step, params.fast_step);

  if (params.min_value.has_value()) {
    if (value < *params.min_value) {
      value = *params.min_value;
    }
  }
  if (params.max_value.has_value()) {
    if (value > *params.max_value) {
      value = *params.max_value;
    }
  }

  if (params.zero_is_unset) {
    if (value > 0) {
      field.set(value);
    } else {
      field.clear();
    }
  } else {
    field.set(value);
  }
}

void InputStoredColor(const std::string& id, aim::StoredColor* stored_color, float char_x) {
  ImGui::IdGuard cid(id);

  float color[3];
  aim::StoredColor raw_stored_color = *stored_color;
  raw_stored_color.clear_multiplier();
  aim::StoredRgb c = ToStoredRgb(raw_stored_color);
  color[0] = c.r() / 255.0;
  color[1] = c.g() / 255.0;
  color[2] = c.b() / 255.0;
  if (ImGui::ColorEdit3(
          "##ColorEditor", color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
    aim::StoredRgb result = aim::FloatToStoredRgb(color[0], color[1], color[2]);
    if (stored_color->has_hex()) {
      stored_color->set_hex(ToHexString(result));
      stored_color->clear_r();
      stored_color->clear_b();
      stored_color->clear_g();
    } else {
      stored_color->set_r(result.r());
      stored_color->set_g(result.g());
      stored_color->set_b(result.b());
    }
  }

  ImGui::SameLine();
  ImGui::Text("%s", aim::icons::kClose);
  ImGui::HelpTooltip("Multiply color by value");

  ImGui::SameLine();
  ImGui::InputFloat(ImGui::InputFloatParams("ColorMultiplier")
                        .set_step(0.01, 0.2)
                        .set_max(2)
                        .set_width(char_x * 10)
                        .set_zero_is_unset(),
                    PROTO_FLOAT_FIELD(aim::StoredColor, stored_color, multiplier));
}

void InputOptionalStoredColor(const std::string& id,
                              aim::PtrField<aim::StoredColor> stored_color,
                              float char_x) {
  ImGui::IdGuard cid(id);
  bool has_value = stored_color.has();
  ImGui::Checkbox("##UseStoredColor", &has_value);
  if (has_value) {
    ImGui::SameLine();
    InputStoredColor("StoredColorEditor", stored_color.get_mutable(), char_x);
  } else {
    stored_color.clear();
  }
}

void SpacedSeparator() {
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
}

float GetDefaultCharSizeX() {
  return ImGui::CalcTextSize("A").x;
}

void ReadonlyLabeledFloat(const std::string& label, float value, int width_multiple) {
  IdGuard cid("RoFloat_" + label);
  ImGui::AlignTextToFramePadding();
  ImGui::Text(label);
  ImGui::SameLine();
  float char_x = GetDefaultCharSizeX();
  ImGui::SetNextItemWidth(char_x * width_multiple);
  ImGui::InputFloat("##ValueOut", &value, 0.0f, 0.0f, "%.3g", ImGuiInputTextFlags_ReadOnly);
}

bool Chip(const std::string& label, bool selected) {
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
  ImVec2 size = ImGui::CalcTextSize(label.c_str());
  ImVec2 frame_padding = ImGui::GetStyle().FramePadding;
  bool clicked = ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, size);
  ImGui::PopStyleVar();
  return clicked;
}

bool SelectableButton(const std::string& label) {
  ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
  ImVec2 size = ImGui::CalcTextSize(label.c_str());
  bool clicked = ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, size);
  ImGui::PopStyleVar();
  return clicked;
}

bool BeginDefaultPopupModal(const char* id, bool* draw) {
  ImGui::SetNextWindowPos(
      ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  return ImGui::BeginPopupModal(id,
                                draw,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
}

std::optional<std::string> MultilineTextEntryDialog::Draw() {
  std::optional<std::string> result;
  auto* viewport = ImGui::GetMainViewport();
  ImVec2 work_size = viewport->WorkSize;
  if (popup_.Begin()) {
    ImGui::InputTextMultiline("##DescriptionInput",
                              &text_,
                              ImVec2(work_size.x * 0.4, work_size.y * 0.5),
                              ImGuiInputTextFlags_AllowTabInput);
    ImGui::Spacing();
    if (ImGui::Button("Set")) {
      result = text_;
      popup_.Close();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      popup_.Close();
    }
    popup_.End();
  }
  return result;
}

bool Popup::Begin() {
  return BeginInternal(false);
}

bool Popup::BeginModal() {
  return BeginInternal(true);
}

bool Popup::BeginInternal(bool is_modal) {
  if (do_open_) {
    do_open_ = false;
    ImGui::OpenPopup(id_.c_str());
  }
  ImGui::SetNextWindowPos(
      ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  auto flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
  if (is_modal) {
    return ImGui::BeginPopupModal(id_.c_str(), nullptr, flags);
  }
  return ImGui::BeginPopup(id_.c_str(), flags);
}

void Popup::End() {
  ImGui::EndPopup();
}

void Popup::Close() {
  ImGui::CloseCurrentPopup();
}

void Popup::Open() {
  do_open_ = true;
}

void NewSdlFrame() {
  ImGui_ImplSDLGPU3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

}  // namespace ImGui
