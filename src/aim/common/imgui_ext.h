#pragma once

#include <format>
#include <functional>
#include <string>
#include <vector>

#include "aim/common/field.h"
#include "aim/proto/common.pb.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"  // IWYU pragma: keep

namespace ImGui {

// Create a new ImGui frame backed by SDL3/GPU.
void NewSdlFrame();

// ImGui::Text but taking same arguments as std::format to product the text.
template <class... _Types>
static void TextFmt(const std::format_string<_Types...> fmt, _Types&&... args) {
  std::string message = std::format(fmt, std::forward<_Types>(args)...);
  Text("%s", message.c_str());
}

// ImGui::TextDisabled but taking same arguments as std::format to product the text.
template <class... _Types>
static void TextDisabledFmt(const std::format_string<_Types...> fmt, _Types&&... args) {
  std::string message = std::format(fmt, std::forward<_Types>(args)...);
  TextDisabled("%s", message.c_str());
}

struct IdGuard {
  IdGuard(std::string id) {
    ImGui::PushID(id.c_str());
  }

  IdGuard(std::string prefix, int num) {
    ImGui::PushID(std::format("{}{}", prefix, num).c_str());
  }

  IdGuard(int id) {
    ImGui::PushID(id);
  }

  ~IdGuard() {
    Pop();
  }

  void Pop() {
    if (!closed) {
      ImGui::PopID();
      closed = true;
    }
  }

  IdGuard(const IdGuard&) = delete;
  IdGuard& operator=(const IdGuard&) = delete;

 private:
  bool closed = false;
};

struct LoopId {
  LoopId() {}

  IdGuard Get() {
    return IdGuard(++i);
  }

  IdGuard Get(const std::string& prefix) {
    return IdGuard(prefix, ++i);
  }

  int i = -1;
};

void TextDisabled(const std::string& val);
void TextWrapped(const std::string& val);

void Text(const std::string& val);

bool Button(const std::string& label, const ImVec2& size = ImVec2(0, 0));

bool Selectable(const std::string& label, bool selected = false);

bool SimpleDropdown(const std::string& id,
                    std::string* value,
                    const std::vector<std::string>& values,
                    float input_width = -1,
                    int* selected_index = nullptr,
                    bool* opened = nullptr);

template <typename T>
bool SimpleTypeDropdown(const std::string& id,
                        T* value,
                        const std::vector<std::pair<T, std::string>>& values,
                        float input_width = -1) {
  ImGui::IdGuard cid(id);
  if (input_width > 0) {
    ImGui::PushItemWidth(input_width);
  }
  std::string initial_value;
  for (auto& v : values) {
    if (v.first == *value) {
      initial_value = v.second;
      break;
    }
  }

  bool item_was_selected = false;
  ImGuiComboFlags combo_flags = ImGuiComboFlags_HeightLarge;
  if (ImGui::BeginCombo("##Combo", initial_value.c_str(), combo_flags)) {
    ImGui::LoopId loop_id;
    for (const auto& item : values) {
      auto id = loop_id.Get();
      bool is_selected = item.first == *value;
      if (ImGui::Selectable(item.second.c_str(), is_selected)) {
        *value = item.first;
        item_was_selected = true;
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

void HelpMarker(const std::string& text);
void InfoMarker(const std::string& text);
void HelpTooltip(const std::string& text);
void HelpTooltip(std::function<std::string()> get_text);

void DrawItemBounds();
void SetCursorAtBottom(float item_height = -1);
void SetCursorAtRight(float item_width);
void SetButtonCursorAtRight(const std::string& text);

struct InputFloatParams {
  explicit InputFloatParams(const std::string& id) : id(id) {}

  static InputFloatParams WithLabelAsId(const std::string& label) {
    return InputFloatParams(label).set_label(label);
  }

  InputFloatParams clone() const {
    return *this;
  }

  InputFloatParams& set_step(float step, float fast_step) {
    this->step = step;
    this->fast_step = fast_step;
    return *this;
  }

  InputFloatParams& set_width(float width) {
    this->width = width;
    return *this;
  }

  InputFloatParams& set_default(float default_value) {
    this->default_value = default_value;
    return *this;
  }

  InputFloatParams& set_zero_is_unset() {
    this->zero_is_unset = true;
    return *this;
  }

  InputFloatParams& set_range(float min, float max) {
    min_value = min;
    max_value = max;
    return *this;
  }

  InputFloatParams& set_min(float min) {
    min_value = min;
    return *this;
  }

  InputFloatParams& set_max(float max) {
    max_value = max;
    return *this;
  }

  InputFloatParams& set_label(const std::string& label) {
    this->label = label;
    return *this;
  }

  InputFloatParams& set_id(const std::string& id) {
    this->id = id;
    return *this;
  }

  InputFloatParams& set_id_and_label(const std::string& label) {
    this->id = label;
    this->label = label;
    return *this;
  }

  InputFloatParams& set_is_optional() {
    is_optional = true;
    return *this;
  }

  InputFloatParams& set_is_optional(bool value) {
    is_optional = value;
    return *this;
  }

  InputFloatParams& set_optional_secondary_label(const std::string& value) {
    optional_secondary_label = value;
    return *this;
  }

  std::string id;
  std::string label;
  // If optional. A secondary label that is show after the checkbox when selected.
  std::string optional_secondary_label;

  float step = 1;
  float fast_step = 5;
  const char* format = "%.6g";
  float width = -1;
  std::optional<float> default_value;

  std::optional<float> min_value;
  std::optional<float> max_value;
  bool zero_is_unset = false;
  bool is_optional = false;
};

void InputFloat(const InputFloatParams& params, aim::Field<float> field);

void InputJitteredFloat(const InputFloatParams& params, aim::JitteredField<float> field);

struct InputBoolParams {
  explicit InputBoolParams(const std::string& id) : id(id) {}

  static InputBoolParams WithLabelAsId(const std::string& label) {
    return InputBoolParams(label).set_label(label);
  }

  InputBoolParams& set_label(const std::string& label) {
    this->label = label;
    return *this;
  }

  std::string id;
  std::string label;
};

void InputBool(const InputBoolParams& params, aim::Field<bool> field);
void InputBool(const std::string& label, aim::Field<bool> field);

struct InputIntParams {
  explicit InputIntParams(const std::string& id) : id(id) {}

  static InputIntParams WithLabelAsId(const std::string& label) {
    return InputIntParams(label).set_label(label);
  }

  InputIntParams& set_step(int step, int fast_step) {
    this->step = step;
    this->fast_step = fast_step;
    return *this;
  }

  InputIntParams& set_width(float width) {
    this->width = width;
    return *this;
  }

  InputIntParams& set_default(int default_value) {
    this->default_value = default_value;
    return *this;
  }

  InputIntParams& set_zero_is_unset() {
    this->zero_is_unset = true;
    return *this;
  }

  InputIntParams& set_range(int min, int max) {
    min_value = min;
    max_value = max;
    return *this;
  }

  InputIntParams& set_min(int min) {
    min_value = min;
    return *this;
  }

  InputIntParams& set_label(const std::string& label) {
    this->label = label;
    return *this;
  }

  InputIntParams& set_id(const std::string& id) {
    this->id = id;
    return *this;
  }

  InputIntParams& set_is_optional() {
    is_optional = true;
    return *this;
  }

  std::string id;
  std::string label;

  int step = 1;
  int fast_step = 5;
  float width = -1;
  std::optional<int> default_value;

  std::optional<int> min_value;
  std::optional<int> max_value;
  bool zero_is_unset = false;
  bool is_optional = false;
};

void InputInt(const InputIntParams& params, aim::Field<int> field);

void InputStoredColor(const std::string& id, aim::StoredColor* stored_color, float char_x);
void InputOptionalStoredColor(const std::string& id,
                              aim::PtrField<aim::StoredColor> stored_color,
                              float char_x);

void SpacedSeparator();

float GetDefaultCharSizeX();

bool Chip(const std::string& label, bool selected);

template <typename T>
bool ChipSelector(const std::string& id,
                  T* current_value,
                  const std::vector<std::pair<T, std::string>>& values) {
  ImGui::IdGuard cid(id);

  T selected_value = *current_value;
  bool is_first = true;
  for (auto& entry : values) {
    T value_type = entry.first;
    const std::string& label = entry.second;
    if (is_first) {
      is_first = false;
    } else {
      ImGui::SameLine();
    }
    if (Chip(label.c_str(), *current_value == value_type)) {
      selected_value = value_type;
    }
  }

  bool item_was_changed = selected_value != *current_value;
  *current_value = selected_value;
  return item_was_changed;
}

bool SelectableButton(const std::string& label);

float GetMenuButtonWidth();
bool MenuButton();
float GetWidthWithPadding(const std::string& text);

bool IconButton(const char* icon, float scale = 0.7);
bool CircleIconButton(const char* icon, float scale = 0.7);
float GetIconButtonWidth(const char* icon, float scale = 0.7);

bool ClearButton();

bool BeginDefaultPopupModal(const char* id, bool* draw);

class Popup {
 public:
  explicit Popup(const std::string& id) : id_(id) {}

  // Begin as popup which closes if clicked outside.
  bool Begin();
  // Begin as popup modal which requires explicit closing.
  bool BeginModal();

  void End();
  void Close();
  void Open();

 private:
  bool BeginInternal(bool is_modal);

  std::string id_;
  bool do_open_ = false;
};

class NotificationPopup {
 public:
  explicit NotificationPopup(const std::string& id) : popup_(id) {}

  void NotifyOpen(const std::string& text);

  // Returns true when notification confirm button clicked.
  bool Draw();

 private:
  std::string text_;
  Popup popup_;
};

class MultilineTextEntryDialog {
 public:
  explicit MultilineTextEntryDialog(const std::string& id) : popup_(id) {}

  void NotifyOpen(const std::string& text) {
    text_ = text;
    popup_.Open();
  }

  std::optional<std::string> Draw();

 private:
  std::string text_;
  Popup popup_;
};

template <typename T>
class ConfirmationDialog {
 public:
  explicit ConfirmationDialog(const std::string& id) : popup_(id) {}

  void NotifyOpen(const std::string& text, const T& value) {
    data_ = value;
    text_ = text;
    popup_.Open();
  }

  std::optional<T> Draw(const std::string& confirm_text) {
    if (!popup_.Begin()) {
      return {};
    }
    std::optional<T> confirmed_result;
    ImGui::Text(text_);

    float button_width = ImGui::CalcTextSize("OK").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - button_width) * 0.5f);

    if (ImGui::Button(confirm_text.c_str())) {
      confirmed_result = data_;
      popup_.Close();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      popup_.Close();
    }
    popup_.End();
    return confirmed_result;
  }

 private:
  std::optional<T> data_{};
  std::string text_;
  Popup popup_;
};

}  // namespace ImGui
