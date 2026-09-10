#include "select_object_dialog.h"

#include <memory>
#include <optional>

#include "aim/common/imgui_ext.h"
#include "aim/common/name_util.h"
#include "aim/core/guide_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/ui/search_selector.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"

namespace aim {
namespace {

class SelectObjectDialogImpl : public SelectObjectDialog {
 public:
  SelectObjectDialogImpl(const std::string& id, ObjectType type)
      : id_(id), popup_(id), type_(type) {}

  bool Draw(Result* result) override {
    ImGui::IdGuard cid("SelectObjectDialog_" + id_);
    bool selected = false;

    if (popup_.Begin()) {
      auto screen = app_.screen_info();
      ImGui::BeginChild("SelectObjectContainer", ImVec2(screen.width * 0.5, screen.height * 0.8));
      selected = DrawPopup(result);
      ImGui::EndChild();
      popup_.End();
    }
    return selected;
  }

  void NotifyOpen() override {
    popup_.Open();
  }

 private:
  std::shared_ptr<std::vector<std::string>> GetNames() {
    switch (type_) {
      case ObjectType::PLAYLIST:
        return app_.playlist_manager().playlist_names();
      case ObjectType::GUIDE:
        return app_.guide_manager().guide_names();
      case ObjectType::SCENARIO:
        return app_.scenario_manager().scenario_names();
      default:
        break;
    }
    return std::make_shared<std::vector<std::string>>();
  }

  bool DrawPopup(Result* result) {
    float char_x = ImGui::GetDefaultCharSizeX();
    ImGui::SetNextItemWidth(char_x * 30);
    if (ImGui::IsWindowAppearing()) {
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputText("###SearchInput", &search_text_);
    if (search_text_.size() > 0) {
      ImGui::SameLine();
      if (ImGui::ClearButton()) {
        search_text_ = "";
      }
    }
    auto names = GetNames();

    ImGui::BeginChild("ResultsContainer", ImVec2(0, 0));
    SearchSelectorOptions options;
    options.max_results = 50;
    options.empty_search_text_matches_all = true;
    std::optional<std::string> selected_playlist = SearchSelector(search_text_, *names, options);
    ImGui::EndChild();
    if (selected_playlist) {
      result->selected_objects.push_back(*selected_playlist);
      popup_.Close();
      return true;
    }

    return false;
  }

  Application& app_ = GetUiApp();
  ImGui::Popup popup_;
  std::string id_;
  ObjectType type_;
  std::string search_text_;
};

}  // namespace

std::unique_ptr<SelectObjectDialog> CreateSelectObjectDialog(const std::string& id,
                                                             ObjectType type) {
  return std::make_unique<SelectObjectDialogImpl>(id, type);
}

}  // namespace aim
