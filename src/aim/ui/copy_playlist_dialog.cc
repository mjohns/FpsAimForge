#include "copy_playlist_dialog.h"

#include "aim/common/imgui_ext.h"
#include "aim/common/object_type.h"
#include "aim/common/resource_name.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/history_manager.h"
#include "aim/core/playlist_manager.h"
#include "imgui.h"

namespace aim {

bool CopyPlaylistDialog::Draw(Application& app) {
  ImGui::IdGuard cid("CopyPlaylistDialogContent");
  bool did_copy = false;
  bool show_popup = source_.has_value();
  if (show_popup) {
    if (ImGui::BeginDefaultPopupModal(id_.c_str(), &show_popup)) {
      ImGui::TextFmt("Copy \"{}\" to", source_->name);
      ImGui::SimpleDropdown("BundlePicker",
                            new_name_.mutable_bundle_name(),
                            bundle_names_,
                            ImGui::GetFrameHeight() * 9);
      ImGui::SameLine();
      ImGui::InputText("##RelativeNameInput", new_name_.mutable_relative_name());

      ImGui::Indent();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Make new copies of all scenarios");
      ImGui::SameLine();
      ImGui::Checkbox("##DeepCopy", &deep_copy_);

      if (deep_copy_) {
        ImGui::Indent();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("As references");
        ImGui::SameLine();
        ImGui::Checkbox("##AsReferences", &as_references_);
        ImGui::SameLine();
        ImGui::HelpMarker(
            "Versions in new playlist will have a different name (stats, settings, ..), but will "
            "change when the underlying scenario changes.");

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Add name prefix*");
        ImGui::SameLine();
        ImGui::InputText("##AddPrefix", &add_prefix_);
        ImGui::SameLine();
        ImGui::HelpMarker(
            "Adds the following prefix to all newly created scenario names after the bundle "
            "name");

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Remove name prefix");
        ImGui::SameLine();
        ImGui::InputText("##RemovePrefix", &remove_prefix_);
        ImGui::SameLine();
        ImGui::HelpMarker("Strips the following prefix from the name of scenarios being copied");

        ImGui::Unindent();
      }

      ImGui::Unindent();
      ImGui::Spacing();
      if (ImGui::Button("Copy")) {
        // Do copy
        CopyPlaylistOptions opts;
        opts.add_prefix = add_prefix_;
        opts.remove_prefix = remove_prefix_;
        opts.deep_copy = deep_copy_;
        opts.as_references = deep_copy_ && as_references_;
        std::optional<std::string> final_name = app.playlist_manager().CopyPlaylist(
            source_->name, new_name_.full_name(), &app.scenario_manager(), opts);
        if (final_name) {
          new_name_ = ResourceName::Parse(*final_name);
          did_copy = app.bundle_manager().SaveDirtyBundles();
        }
        if (did_copy) {
          app.history_manager().UpdateRecentView(ObjectType::PLAYLIST, new_name_.full_name());
          app.playlist_manager().SetCurrentPlaylist(new_name_.full_name());
        }
        ImGui::CloseCurrentPopup();
        source_ = {};
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        source_ = {};
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
  if (open_) {
    ImGui::OpenPopup(id_.c_str());
    open_ = false;
    deep_copy_ = false;
    bundle_names_ = app.bundle_manager().GetWritableBundleNames();
    new_name_ = ResourceName::Parse(source_->name);
    if (app.bundle_manager().IsBundleReadonly(new_name_.bundle_name())) {
      *new_name_.mutable_bundle_name() = app.bundle_manager().GetDefaultWritableBundleName();
    }
    *new_name_.mutable_relative_name() += " Copy";
  }
  return did_copy;
}

}  // namespace aim
