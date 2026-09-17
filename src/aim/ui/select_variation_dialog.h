#pragma once

#include <string>

#include "aim/common/imgui_ext.h"
#include "aim/common/name_util.h"
#include "aim/common/object_type.h"

namespace aim {

class SelectVariationDialog {
 public:
  SelectVariationDialog(const std::string& id, ObjectType type)
      : popup_(id), type_(type), id_(id) {}

  void NotifyOpen(const std::string& current_name) {
    name_info_ = GetNameInfo(current_name);
    popup_.Open();
  }

  bool Draw(std::string* updated_name);

 private:
  ObjectType type_;
  ImGui::Popup popup_;
  std::string id_;
  NameInfo name_info_;
};

}  // namespace aim
