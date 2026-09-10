#include "guide_editor_screen.h"

#include "absl/strings/ascii.h"
#include "aim/common/collections.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/resource_name.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/guide_manager.h"
#include "aim/core/history_manager.h"
#include "aim/proto/guide.pb.h"
#include "aim/ui/drag_and_drop.h"
#include "aim/ui/search_selector.h"
#include "aim/ui/select_object_dialog.h"
#include "aim/ui/select_variation_dialog.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"

namespace aim {
namespace {

class SectionEditor {
 public:
  void Draw(GuideSection& section) {
    ImGui::InputTextMultiline("##DescriptionInput",
                              section.mutable_text(),
                              ImVec2(0, 0),
                              ImGuiInputTextFlags_AllowTabInput);
    ImGui::SpacedSeparator();
    DrawPlaylistsEditor(section);
    ImGui::SpacedSeparator();
    DrawLinkedGuidesEditor(section);
  }

 private:
  void DrawLinkedGuidesEditor(GuideSection& section) {
    ImGui::IdGuard cid("LinkedGuides");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Guides");

    if (section.guides_size() > 0 &&
        ImGui::BeginTable(
            "Guides", 3, ImGui::kDefaultTableFlags, ImVec2(ImGui::GetDefaultCharSizeX() * 60, 0))) {
      float drag_width = guide_drag_and_drop_.GetDragWidth();
      float menu_width = ImGui::GetIconButtonWidth(icons::kMoreVert);

      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, drag_width);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, menu_width);

      ImGui::LoopId loop_id;
      ListUpdater list_updater;
      for (int i = 0; i < section.guides_size(); ++i) {
        auto lid = loop_id.Get("Guide");
        ImGui::TableNextRow();
        const std::string& guide_name = section.guides(i);

        ImGui::TableNextColumn();
        guide_drag_and_drop_.DrawDragHandle(i, guide_name);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##ItemEditor", section.mutable_guides(i));

        const char* item_menu = "GuideItemMenu";
        if (ImGui::BeginPopupContextItem(item_menu)) {
          list_updater.DrawMenuItems(i);
          ImGui::EndPopup();
        }
        ImGui::OpenPopupOnItemClick(item_menu, ImGuiPopupFlags_MouseButtonRight);

        ImGui::TableNextColumn();
        if (ImGui::IconButton(icons::kMoreVert)) {
          ImGui::OpenPopup(item_menu);
        }
      }

      guide_drag_and_drop_.Update(section.mutable_guides());
      list_updater.Update(section.mutable_guides());
      ImGui::EndTable();
    }

    SelectObjectDialog::Result add_result;
    if (add_guide_dialog_->Draw(&add_result)) {
      for (const std::string& selected : add_result.selected_objects) {
        section.add_guides(selected);
      }
    }

    ImGui::Spacing();
    if (ImGui::Button(std::format("{} Guide", icons::kAdd))) {
      add_guide_dialog_->NotifyOpen();
    }
  }

  void DrawPlaylistsEditor(GuideSection& section) {
    ImGui::IdGuard cid("Playlists");

    std::string updated_item_name;
    if (select_variation_dialog_.Draw(&updated_item_name)) {
      if (IsValidIndex(*section.mutable_playlists(), editing_variation_i_)) {
        (*section.mutable_playlists())[editing_variation_i_] = updated_item_name;
      }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Playlists");

    if (section.playlists_size() > 0 &&
        ImGui::BeginTable("Playlists",
                          3,
                          ImGui::kDefaultTableFlags,
                          ImVec2(ImGui::GetDefaultCharSizeX() * 60, 0))) {
      float drag_width = playlist_drag_and_drop_.GetDragWidth();
      float menu_width = ImGui::GetIconButtonWidth(icons::kMoreVert);

      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, drag_width);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, menu_width);

      ImGui::LoopId loop_id;
      ListUpdater list_updater;
      for (int i = 0; i < section.playlists_size(); ++i) {
        auto lid = loop_id.Get("Playlist");
        ImGui::TableNextRow();
        const std::string& playlist_name = section.playlists(i);

        ImGui::TableNextColumn();
        playlist_drag_and_drop_.DrawDragHandle(i, playlist_name);

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##PlaylistItemEditor", section.mutable_playlists(i));

        const char* item_menu = "PlaylistItemMenu";
        if (ImGui::BeginPopupContextItem(item_menu)) {
          list_updater.DrawCopyMenuItem(i);
          if (ImGui::Selectable(std::format("{} Select variation", icons::kTune))) {
            editing_variation_i_ = i;
            select_variation_dialog_.NotifyOpen(playlist_name);
          }
          list_updater.DrawMoveMenuItems(i);
          ImGui::SpacedSeparator();
          list_updater.DrawDeleteMenuItem(i);
          ImGui::EndPopup();
        }
        ImGui::OpenPopupOnItemClick(item_menu, ImGuiPopupFlags_MouseButtonRight);

        ImGui::TableNextColumn();
        if (ImGui::IconButton(icons::kMoreVert)) {
          ImGui::OpenPopup(item_menu);
        }
      }

      ImGui::EndTable();

      playlist_drag_and_drop_.Update(section.mutable_playlists());
      list_updater.Update(section.mutable_playlists());
    }

    SelectObjectDialog::Result add_result;
    if (add_playlist_dialog_->Draw(&add_result)) {
      for (const std::string& selected_playlist : add_result.selected_objects) {
        section.add_playlists(selected_playlist);
      }
    }

    ImGui::Spacing();
    if (ImGui::Button(std::format("{} Playlist", icons::kAdd))) {
      add_playlist_dialog_->NotifyOpen();
    }
  }

  std::unique_ptr<SelectObjectDialog> add_playlist_dialog_ =
      CreateSelectObjectDialog("AddPlaylistDialog", ObjectType::PLAYLIST);
  std::unique_ptr<SelectObjectDialog> add_guide_dialog_ =
      CreateSelectObjectDialog("AddGuideDialog", ObjectType::GUIDE);
  DragAndDrop playlist_drag_and_drop_;
  DragAndDrop guide_drag_and_drop_;
  int editing_variation_i_ = -1;
  SelectVariationDialog select_variation_dialog_ =
      SelectVariationDialog::ForPlaylists("SelectPlaylistVariation");
};

class GuideEditorScreen : public UiScreen {
 public:
  GuideEditorScreen(const GuideEditorOptions& opts) : UiScreen(), options_(opts) {
    bundle_names_ = app_.bundle_manager().GetWritableBundleNames();
    if (!opts.name.empty()) {
      original_name_ = ResourceName::Parse(opts.name);
      name_ = *original_name_;

      auto initial_guide = app_.guide_manager().GetGuide(opts.name);
      if (initial_guide) {
        original_guide_ = initial_guide->def;
        updated_guide_ = original_guide_;
      } else {
        notification_popup_.NotifyOpen(std::format("Guide \"{}\" does not exist.", opts.name));
        exit_after_notification_ = true;
      }
    }

    if (updated_guide_.sections().empty()) {
      updated_guide_.add_sections();
    }
  }

  void DrawScreen() override {
    ImGui::IdGuard cid("GuideEditor");
    bool notification_confirmed = notification_popup_.Draw();
    if (notification_confirmed && exit_after_notification_) {
      PopSelf();
    }

    DrawTopBar();
    if (BeginMainWindow("MainEditor", 0.9)) {
      DrawEditor();
    }
    ImGui::End();
  }

  void DrawEditor() {
    ImVec2 char_size = ImGui::CalcTextSize("A");
    char_x_ = char_size.x;

    ImGui::LoopId loop_id;
    GuideDef& def = updated_guide_;

    ListUpdater list_updater;

    section_editors_.resize(def.sections_size());
    for (int i = 0; i < def.sections_size(); ++i) {
      auto lid = loop_id.Get("Section");
      GuideSection* section = def.mutable_sections(i);

      ImGui::AlignTextToFramePadding();
      ImGui::TextFmt("Section {}", i + 1);

      const char* menu_id = "GuideSectionMenu";
      if (ImGui::BeginPopupContextItem(menu_id)) {
        list_updater.DrawMenuItems(i);
        ImGui::EndPopup();
      }

      ImGui::Indent();
      ImGui::SameLine();
      if (ImGui::MenuButton()) {
        ImGui::OpenPopup(menu_id);
      }

      if (i != 0) {
        ImGui::SpacedSeparator();
      }
      section_editors_[i].Draw(*section);
      ImGui::Unindent();
    }

    if (ImGui::Button(std::format("{} Add section", icons::kAdd))) {
      updated_guide_.add_sections();
    }

    list_updater.Update(def.mutable_sections());
  }

 private:
  void DrawTopBar() {
    float middle = app_.screen_info().width / 2.0;
    ImGui::SetNextWindowPos(ImVec2(middle, char_x_ / 3.0), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (!ImGui::Begin("TopBar",
                      nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::End();
      return;
    }

    // bool notification_confirmed = notification_popup_.Draw();
    // if (notification_confirmed && exit_after_notification_) {
    //   PopSelf();
    // }

    ImGui::SimpleDropdown("BundlePicker", name_.mutable_bundle_name(), bundle_names_, char_x_ * 11);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(char_x_ * 40);
    ImGui::InputText("##RelativeNameInput", name_.mutable_relative_name());

    ImGui::SameLine();
    std::string save_text = options_.is_new_guide ? std::format("{} Create", icons::kSave)
                                                  : std::format("{} Update", icons::kSave);
    if (ImGui::Button(save_text, ImVec2(char_x_ * 8, 0))) {
      if (SaveGuide()) {
        PopSelf();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      PopSelf();
    }

    ImGui::End();
  }

  bool SaveGuide() {
    if (name_.bundle_name().size() == 0 || name_.relative_name().size() == 0) {
      SetErrorMessage("Missing guide name");
      return false;
    }

    absl::StripAsciiWhitespace(name_.mutable_relative_name());

    auto& mgr = app_.guide_manager();

    bool is_rename = original_name_.has_value() && original_name_->full_name() != name_.full_name();
    if (is_rename || options_.is_new_guide) {
      // Make sure new name is not taken.
      auto existing_with_name = mgr.GetGuide(name_.full_name());
      if (existing_with_name.has_value()) {
        SetErrorMessage(std::format("Guide \"{}\" already exists", name_.full_name()));
        return false;
      }
    }

    if (is_rename) {
      mgr.RenameGuide(original_name_->full_name(), name_.full_name());
    }

    mgr.UpdateGuide(name_.full_name(), updated_guide_);
    if (!app_.bundle_manager().SaveDirtyBundles()) {
      SetErrorMessage("Unable to save bundle to disk.");
      return false;
    }

    {
      // Make sure we preserve the original level/sens in the name for the current scenario.
      // app_.scenario_manager().SetCurrentScenario(current_name.GetFullName());
      app_.history_manager().UpdateRecentView(ObjectType::GUIDE, name_.full_name());
    }

    return true;
  }

  void SetErrorMessage(const std::string& msg) {
    notification_popup_.NotifyOpen(msg);
  }

  bool BeginMainWindow(const std::string& name, float width_multiple) {
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

  Application& app_ = GetUiApp();
  float char_x_ = 0;
  std::string playlist_search_text_;
  std::string guide_search_text_;
  GuideDef original_guide_;
  GuideDef updated_guide_;
  GuideEditorOptions options_;
  std::vector<std::string> bundle_names_;
  std::optional<ResourceName> original_name_;
  ResourceName name_;
  ImGui::NotificationPopup notification_popup_{"Notification"};
  bool exit_after_notification_ = false;
  std::vector<SectionEditor> section_editors_;
};

}  // namespace

std::unique_ptr<UiScreen> CreateGuideEditorScreen(const GuideEditorOptions& options) {
  return std::make_unique<GuideEditorScreen>(options);
}

}  // namespace aim
