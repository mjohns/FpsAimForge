#include "select_object_dialog.h"

#include <memory>
#include <optional>

#include "aim/common/imgui_ext.h"
#include "aim/ui/object_browser.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"

namespace aim {
namespace {

class SelectObjectDialogImpl : public SelectObjectDialog {
 public:
  SelectObjectDialogImpl(const std::string& id, ObjectType type)
      : popup_(id), id_(id), type_(type) {}

  bool Draw(Result* result) override {
    ImGui::IdGuard cid("SelectObjectDialog_" + id_);
    bool selected = false;

    if (popup_.Begin()) {
      auto screen = app_.screen_info();
      ImGui::BeginChild("SelectObjectContainer", ImVec2(screen.width * 0.5, screen.height * 0.8));
      ObjectBrowser::Result browser_result;
      browser_->Draw(&browser_result);
      if (browser_result.selected_object_name) {
        result->selected_objects.push_back(*browser_result.selected_object_name);
        selected = true;
        popup_.Close();
      }
      ImGui::EndChild();
      popup_.End();
    }
    return selected;
  }

  void NotifyOpen() override {
    popup_.Open();
  }

 private:
  Application& app_ = GetUiApp();
  ImGui::Popup popup_;
  std::string id_;
  ObjectType type_;
  std::unique_ptr<ObjectBrowser> browser_ = CreateObjectBrowser(type_, /*is_selector=*/true);
};

}  // namespace

std::unique_ptr<SelectObjectDialog> CreateSelectObjectDialog(const std::string& id,
                                                             ObjectType type) {
  return std::make_unique<SelectObjectDialogImpl>(id, type);
}

}  // namespace aim
