#pragma once

#include <string>

#include "aim/common/imgui_ext.h"
#include "aim/common/name_util.h"
#include "aim/common/object_type.h"
#include "aim/common/resource_name.h"
#include "aim/proto/guide.pb.h"
#include "aim/proto/playlist.pb.h"
#include "aim/proto/scenario.pb.h"
#include "aim/ui/ui_screen.h"

namespace aim {

struct BaseEditorOptions {
  std::string name;
  bool is_new = false;
  std::string force_bundle_name;
};

class BaseEditorScreen : public UiScreen {
 public:
  BaseEditorScreen(ObjectType type, const BaseEditorOptions& opts);
  virtual ~BaseEditorScreen() {}

  void DrawScreen() override;

 protected:
  virtual void DrawEditor() = 0;

  void SetErrorMessage(const std::string& msg) {
    notification_popup_.NotifyOpen(msg);
  }

  void DrawTopBar();
  bool BeginMainWindow(const std::string& name, float width_multiple);

  bool Save();

  float char_x_ = 0;

  ObjectType type_;

  // The appropriate defs will be filled in based on type.
  GuideDef original_guide_;
  GuideDef updated_guide_;
  PlaylistDef original_playlist_;
  PlaylistDef updated_playlist_;
  ScenarioDef original_scenario_;
  ScenarioDef updated_scenario_;

  bool is_new_ = false;
  std::vector<std::string> bundle_names_;
  std::optional<ResourceName> original_name_;
  NameInfo original_name_info_;
  ResourceName name_;
  ImGui::NotificationPopup notification_popup_{"Notification"};
  bool exit_after_notification_ = false;
};

}  // namespace aim
