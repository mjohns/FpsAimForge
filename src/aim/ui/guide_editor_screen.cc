#include "guide_editor_screen.h"

#include "aim/common/collections.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/proto/guide.pb.h"
#include "aim/ui/base_editor_screen.h"
#include "aim/ui/drag_and_drop.h"
#include "aim/ui/select_object_dialog.h"
#include "aim/ui/select_variation_dialog.h"
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
  SelectVariationDialog select_variation_dialog_{"SelectPlaylistVariation"};
};

BaseEditorOptions GetBaseOptions(const GuideEditorOptions& opts) {
  BaseEditorOptions base;
  base.name = opts.name;
  base.is_new = opts.is_new_guide;
  return base;
}

class GuideEditorScreen : public BaseEditorScreen {
 public:
  GuideEditorScreen(const GuideEditorOptions& opts)
      : BaseEditorScreen(ObjectType::GUIDE, GetBaseOptions(opts)) {
    if (updated_guide_.sections().empty()) {
      updated_guide_.add_sections();
    }
  }

 protected:
  void DrawEditor() override {
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

    ImGui::SpacedSeparator();
    if (ImGui::Button(std::format("{} Add section", icons::kAdd))) {
      updated_guide_.add_sections();
    }

    list_updater.Update(def.mutable_sections());
  }

 private:
  std::string playlist_search_text_;
  std::string guide_search_text_;
  std::vector<SectionEditor> section_editors_;
};

}  // namespace

std::unique_ptr<UiScreen> CreateGuideEditorScreen(const GuideEditorOptions& options) {
  return std::make_unique<GuideEditorScreen>(options);
}

}  // namespace aim
