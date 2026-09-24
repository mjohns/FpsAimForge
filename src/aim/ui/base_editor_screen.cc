#include "base_editor_screen.h"

#include "absl/strings/ascii.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/resource_name.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/guide_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/proto/guide.pb.h"
#include "aim/ui/search_selector.h"
#include "imgui.h"

namespace aim {
namespace {

bool ItemExists(const std::string& name, ObjectType type, Application& app) {
  switch (type) {
    case ObjectType::SCENARIO:
      return app.scenario_manager().GetScenario(name).has_value();
    case ObjectType::PLAYLIST:
      return app.playlist_manager().GetPlaylist(name).has_value();
    case ObjectType::GUIDE:
      return app.guide_manager().GetGuide(name).has_value();
    case ObjectType::THEME:
    case ObjectType::CROSSHAIR:
      break;
  }
  assert(false && "Unsupported object type");
  return false;
}

}  // namespace

BaseEditorScreen::BaseEditorScreen(ObjectType type, const BaseEditorOptions& opts)
    : UiScreen(), type_(type) {
  bundle_names_ = app_.bundle_manager().GetWritableBundleNames();
  std::string default_writable_bundle_name =
      bundle_names_.empty() ? kUserBundleName : bundle_names_[0];

  is_new_ = opts.is_new;

  if (opts.name.empty()) {
    if (!is_new_) {
      assert(false && "Missing name for existing item");
      PopSelf();
      return;
    }

    original_name_info_ = GetNameInfo(
        std::format("{} New {}", default_writable_bundle_name, ObjectTypeToString(type_)));
  } else {
    original_name_info_ = GetNameInfo(opts.name);
    // Make sure the bundle is writable.
    if (app_.bundle_manager().IsBundleReadonly(original_name_info_.GetBundleName())) {
      original_name_info_.SetBundleName(default_writable_bundle_name);
    }
  }
  if (opts.force_bundle_name.size() > 0) {
    original_name_info_.SetBundleName(opts.force_bundle_name);
  }

  // Don't edit with dynamic suffix in the name.
  std::string base_name = original_name_info_.base_name;
  original_name_ = ResourceName::Parse(base_name);
  name_ = *original_name_;

  if (is_new_) {
    return;
  }
  if (type_ == ObjectType::GUIDE) {
    auto initial_guide = app_.guide_manager().GetGuide(base_name);
    if (initial_guide) {
      original_guide_ = initial_guide->def;
      updated_guide_ = original_guide_;
    } else {
      notification_popup_.NotifyOpen(std::format("Guide \"{}\" does not exist.", base_name));
      exit_after_notification_ = true;
    }
  }
  if (type_ == ObjectType::PLAYLIST) {
    auto initial_playlist = app_.playlist_manager().GetPlaylist(base_name);
    if (initial_playlist) {
      original_playlist_ = initial_playlist->def();
      updated_playlist_ = original_playlist_;
    } else {
      notification_popup_.NotifyOpen(std::format("Playlist \"{}\" does not exist.", base_name));
      exit_after_notification_ = true;
    }
  }
}

void BaseEditorScreen::DrawScreen() {
  ImGui::IdGuard cid("BaseEditor");
  ImVec2 char_size = ImGui::CalcTextSize("A");
  char_x_ = char_size.x;

  DrawTopBar();

  if (BeginMainWindow("MainEditor", 0.9)) {
    DrawEditor();
  }
  ImGui::End();
}

void BaseEditorScreen::DrawTopBar() {
  float middle = app_.screen_info().width / 2.0;
  ImGui::SetNextWindowPos(ImVec2(middle, char_x_ / 3.0), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  if (!ImGui::Begin("TopBar",
                    nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  bool notification_confirmed = notification_popup_.Draw();
  if (notification_confirmed && exit_after_notification_) {
    PopSelf();
  }

  ImGui::SimpleDropdown("BundlePicker", name_.mutable_bundle_name(), bundle_names_, char_x_ * 11);

  ImGui::SameLine();
  ImGui::SetNextItemWidth(char_x_ * 40);
  ImGui::InputText("##RelativeNameInput", name_.mutable_relative_name());

  ImGui::SameLine();
  std::string save_text =
      is_new_ ? std::format("{} Create", icons::kSave) : std::format("{} Update", icons::kSave);
  if (ImGui::Button(save_text, ImVec2(char_x_ * 8, 0))) {
    if (Save()) {
      PopSelf();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    PopSelf();
  }

  ImGui::End();
}

bool BaseEditorScreen::Save() {
  if (name_.bundle_name().size() == 0 || name_.relative_name().size() == 0) {
    SetErrorMessage("Missing name");
    return false;
  }

  absl::StripAsciiWhitespace(name_.mutable_relative_name());

  NameInfo name_info = GetNameInfo(name_.full_name());
  if (name_info.HasDynamicSuffix()) {
    SetErrorMessage(
        "Unable to save name ending in 'L#' or '#cm'. These are reserved dynamic endings.");
    return false;
  }

  bool is_rename = original_name_.has_value() && original_name_->full_name() != name_.full_name();
  if (is_rename || is_new_) {
    // Make sure new name is not taken.
    if (ItemExists(name_.full_name(), type_, app_)) {
      SetErrorMessage(std::format("\"{}\" already exists", name_.full_name()));
      return false;
    }
  }

  if (is_rename) {
    switch (type_) {
      case ObjectType::GUIDE:
        app_.guide_manager().RenameGuide(original_name_->full_name(), name_.full_name());
      case ObjectType::PLAYLIST:
        app_.playlist_manager().RenamePlaylist(original_name_->full_name(), name_.full_name());
      case ObjectType::SCENARIO:
        app_.scenario_manager().RenameScenario(original_name_->full_name(), name_.full_name());
      default:
        break;
    }
  }

  switch (type_) {
    case ObjectType::GUIDE:
      app_.guide_manager().UpdateGuide(name_.full_name(), updated_guide_);
    case ObjectType::PLAYLIST:
      app_.playlist_manager().UpdatePlaylist(name_.full_name(), updated_playlist_);
    case ObjectType::SCENARIO:
      app_.scenario_manager().UpdateScenario(name_.full_name(), updated_scenario_);
    default:
      break;
  }

  if (!app_.bundle_manager().SaveDirtyBundles()) {
    SetErrorMessage("Unable to save bundle to disk.");
    return false;
  }

  {
    // Make sure we preserve the original level/sens in the name.
    NameInfo current_name = original_name_info_;
    current_name.base_name = name_.full_name();
    std::string final_name = current_name.GetFullName();

    switch (type_) {
      case ObjectType::GUIDE:
        app_.guide_manager().SetCurrentGuide(final_name);
      case ObjectType::PLAYLIST:
        app_.playlist_manager().SetCurrentPlaylist(final_name);
      case ObjectType::SCENARIO:
        app_.scenario_manager().SetCurrentScenario(final_name);
      default:
        break;
    }

    app_.history_manager().UpdateRecentView(type_, final_name);
  }

  return true;
}

bool BaseEditorScreen::BeginMainWindow(const std::string& name, float width_multiple) {
  float padding = char_x_ * 0.3;
  float start_y = ImGui::GetCursorPosY() + ImGui::GetTextLineHeight() * 1;
  float end_y = app_.screen_info().height - padding;
  float width = app_.screen_info().width * width_multiple;
  float height = end_y - start_y;

  ImGui::SetNextWindowPos(ImVec2((app_.screen_info().width - width) / 2.0, start_y));
  ImGui::SetNextWindowSize(ImVec2(width, height));
  return ImGui::Begin(
      name.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
}

}  // namespace aim
