#include "bundle_ui.h"

#include "absl/cleanup/cleanup.h"
#include "absl/strings/ascii.h"
#include "aim/common/files.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/mat_icons.h"
#include "aim/common/system.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"
#include "aim/proto/bundle.pb.h"
#include "aim/ui/bundle_pack_update_dialog.h"
#include "aim/ui/object_browser.h"
#include "imgui.h"
#include "aim/i18n/i18n.h"
// i18n: Tr() returns translated printf-style literals; the format string is
// non-literal by design, validated against the translation table in i18n.cc.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
#endif

namespace aim {
namespace {

struct CachedBundleDetails {
  i64 num_playlists = 0;
  i64 num_scenarios = 0;
};

class AddBundlePackDialog {
 public:
  void NotifyOpen() {
    bundle_pack_name_ = "";
    popup_.Open();
  }

  std::string Draw() {
    ImGui::IdGuard cid("AddBundlePackDialogContent");
    if (!popup_.Begin()) {
      return "";
    }
    auto cleanup = absl::MakeCleanup([this]() { popup_.End(); });

    std::optional<std::string> added_name;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bundle pack name");
    ImGui::SameLine();
    ImGui::InputText("##BundlePackNameInput", &bundle_pack_name_);
    absl::StripAsciiWhitespace(&bundle_pack_name_);

    bool name_taken = app_.bundle_manager().GetBundlePackInfo(bundle_pack_name_).has_value();
    bool is_invalid = bundle_pack_name_.empty() || name_taken;

    if (is_invalid) {
      ImGui::BeginDisabled();
      if (name_taken) {
        ImGui::TextFmt("Bundle pack \"{}\" already exists", bundle_pack_name_);
      }
    }

    if (ImGui::Button(Tr("Add"))) {
      popup_.Close();
      return bundle_pack_name_;
    }
    if (is_invalid) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Cancel"))) {
      popup_.Close();
    }

    return "";
  }

 private:
  Application& app_ = GetUiApp();
  std::string bundle_pack_name_;
  ImGui::Popup popup_{"AddBundlePackDialog"};
};

class AddBundleDialog {
 public:
  explicit AddBundleDialog(const std::string& id) : popup_(id) {}

  void NotifyOpen(std::optional<std::string> bundle_to_copy) {
    bundle_to_copy_ = bundle_to_copy;
    bundle_name_ = "";
    popup_.Open();
  }

  std::optional<std::string> Draw(Application& app) {
    ImGui::IdGuard cid("AddBundleDialogContent");
    if (!popup_.Begin()) {
      return {};
    }

    std::optional<std::string> added_name;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", Tr("Bundle name"));
    ImGui::SameLine();
    ImGui::InputText("##BundleNameInput", &bundle_name_);

    bool name_taken = app.bundle_manager().GetBundleInfo(bundle_name_).has_value();
    bool is_valid = IsValidBundleName(bundle_name_) && !name_taken;

    if (!is_valid) {
      ImGui::BeginDisabled();
    }

    if (!bundle_name_.empty() && !is_valid) {
      if (name_taken) {
        ImGui::Text("%s", TrFormat("Bundle name \"{}\" already exists", bundle_name_).c_str());
      } else {
        ImGui::Text("%s", TrFormat("Bundle name \"{}\" is invalid. Can only contains letters, numbers, and _", bundle_name_).c_str());
      }
    }

    if (ImGui::Button(bundle_to_copy_ ? "Copy" : "Add")) {
      BundleInfo info;
      info.set_bundle_name(bundle_name_);
      app.bundle_manager().UpdateBundleInfo(info);

      if (bundle_to_copy_) {
        app.bundle_manager().CopyBundle(*bundle_to_copy_, bundle_name_);
      }

      added_name = bundle_name_;
      popup_.Close();
    }
    if (!is_valid) {
      ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Cancel"))) {
      popup_.Close();
    }

    popup_.End();
    return added_name;
  }

 private:
  std::string bundle_name_;
  std::optional<std::string> bundle_to_copy_;
  ImGui::Popup popup_;
};

class UpdateBundlePackInfoDialog {
 public:
  explicit UpdateBundlePackInfoDialog(const BundlePackInfo& info)
      : original_info_(info), info_(info) {
    popup_.Open();
  }

  void Draw() {
    ImGui::IdGuard cid("UpdateBundlePack");
    notification_popup_.Draw();
    if (!popup_.BeginModal()) {
      return;
    }
    auto cleanup = absl::MakeCleanup([this]() { popup_.End(); });

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Bundle pack name");
    ImGui::SameLine();
    ImGui::InputText("##BundlePackNameInput", info_.mutable_bundle_pack_name());

    ImGui::AlignTextToFramePadding();
    ImGui::Text("URL");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##UrlInput", info_.mutable_source_url());
    absl::StripAsciiWhitespace(info_.mutable_source_url());

    std::string repo_name = GetGithubRepoNameFromUrl(info_.source_url());
    if (!repo_name.empty()) {
      ImGui::InputBool("Automatically check for updates",
                       PROTO_BOOL_FIELD(BundlePackInfo, &info_, automatically_check_for_updates));
    } else {
      info_.clear_automatically_check_for_updates();
    }

    ImGui::InputBool("Archived", PROTO_BOOL_FIELD(BundlePackInfo, &info_, archived));
    ImGui::SameLine();
    ImGui::HelpMarker("Bundle pack will not be loaded");

    ImGui::SpacedSeparator();

    if (ImGui::Button(Tr("Save"))) {
      std::string error_msg = Save();
      if (!error_msg.empty()) {
        notification_popup_.NotifyOpen(error_msg);
      } else {
        popup_.Close();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Cancel"))) {
      popup_.Close();
    }
  }

 private:
  std::string Save() {
    if (info_.bundle_pack_name().empty()) {
      return "Missing name";
    }

    bool name_changed = original_info_.bundle_pack_name() != info_.bundle_pack_name();
    if (name_changed) {
      auto existing_pack = app_.bundle_manager().GetBundlePackInfo(info_.bundle_pack_name());
      if (existing_pack) {
        return std::format("Bundle pack \"{}\" already exists", info_.bundle_pack_name());
      }
      auto base_path = app_.file_system().GetUserDataPath("bundles");
      auto source_file = base_path / (original_info_.bundle_pack_name() + ".pack.bin");
      if (std::filesystem::exists(source_file)) {
        std::error_code ec;
        std::filesystem::rename(base_path / (original_info_.bundle_pack_name() + ".pack.bin"),
                                base_path / (info_.bundle_pack_name() + ".pack.bin"),
                                ec);
        if (ec) {
          return "Failed to rename existing bundle file";
        }
      }
    }
    std::string old_name_to_delete = name_changed ? original_info_.bundle_pack_name() : "";
    app_.bundle_manager().UpdateBundlePackInfo(info_, old_name_to_delete);
    if (info_.archived() != original_info_.archived()) {
      app_.bundle_manager().LoadBundlesFromDisk();
    }
    return "";
  }

  Application& app_ = GetUiApp();
  const BundlePackInfo original_info_;
  BundlePackInfo info_;
  ImGui::Popup popup_{"UpdateBundlePackPopup"};
  ImGui::NotificationPopup notification_popup_{"Notification"};
};

class BundleUiComponentImpl : public BundleUiComponent {
 public:
  explicit BundleUiComponentImpl(UiScreen& screen) : screen_(screen), app_(screen.app()) {}

  void Show() override {
    ImGui::IdGuard cid("BundleUiComponent");

    auto to_delete = delete_confirmation_dialog_.Draw("Delete");
    if (to_delete) {
      screen_.app().bundle_manager().DeleteBundle(*to_delete);
      selected_bundle_name_ = "";
    };

    std::vector<std::string> updated_bundle_packs;
    bundle_pack_update_dialog_->Draw(&updated_bundle_packs);

    auto added_name = add_dialog_.Draw(app_);
    if (added_name) {
      selected_bundle_name_ = *added_name;
    }

    float char_x = ImGui::GetDefaultCharSizeX();
    float default_list_width = char_x * 20;
    float default_content_width = char_x * 40;
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("BundleColumns", 4, flags)) {
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, char_x * 20);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, default_content_width);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, char_x * 20);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

      ImGui::TableNextColumn();

      ImGui::Spacing();

      if (ImGui::Button(std::format("{} Bundle", icons::kAdd))) {
        add_dialog_.NotifyOpen({});
      }

      ImGui::SpacedSeparator();

      ImGui::BeginChild("BundleListColumn");
      DrawBundlesList();
      ImGui::EndChild();

      ImGui::TableNextColumn();
      DrawBundleRightPanel();

      ImGui::TableNextColumn();
      DrawBundlePackList();

      ImGui::TableNextColumn();
      DrawBundlePackRightPanel();

      ImGui::EndTable();
    }
  }

 private:
  void DrawBundlePackList() {
    ImGui::IdGuard cid("BundlePackList");

    auto added_name = add_pack_dialog_.Draw();
    if (!added_name.empty()) {
      selected_bundle_name_ = added_name;
      BundlePackInfo info;
      info.set_bundle_pack_name(added_name);
      app_.bundle_manager().UpdateBundlePackInfo(info);
    }

    ImGui::Spacing();
    if (ImGui::Button(std::format("{} Bundle pack", icons::kAdd))) {
      add_pack_dialog_.NotifyOpen();
    }
    ImGui::SpacedSeparator();

    ImGui::BeginChild("BundlePackListColumn");
    ImGui::LoopId loop_id;
    for (const std::string& bundle_pack : app_.bundle_manager().GetBundlePackNames()) {
      auto id_guard = loop_id.Get();
      if (ImGui::Selectable(" " + bundle_pack, bundle_pack == selected_bundle_pack_name_)) {
        selected_bundle_pack_name_ = bundle_pack;
      }
    }
    ImGui::EndChild();
  }

  void DrawBundlePackRightPanel() {
    ImGui::IdGuard cid("BundlePackRightPanel");
    auto to_delete = delete_pack_confirmation_dialog_.Draw("Delete");
    if (to_delete) {
      app_.bundle_manager().DeleteBundlePack(*to_delete);
      selected_bundle_pack_name_ = kDefaultBundlePackName;
    };
    if (update_bundle_pack_info_dialog_) {
      update_bundle_pack_info_dialog_->Draw();
    }
    auto pack = app_.bundle_manager().GetBundlePackInfo(selected_bundle_pack_name_);
    if (!pack) {
      return;
    }
    const bool is_default_pack = selected_bundle_pack_name_ == kDefaultBundlePackName;
    BundlePackInfo info = *pack;
    bool open_edit = false;
    ImGui::AlignTextToFramePadding();
    ImGui::TextFmt("Bundle pack name: {}", info.bundle_pack_name());

    ImGui::AlignTextToFramePadding();
    ImGui::Text("URL");
    ImGui::SameLine();
    float open_icon_right_size = ImGui::GetDefaultCharSizeX() * 2.1;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - open_icon_right_size);
    ImGui::InputText("##UrlInput", info.mutable_source_url(), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::IconButton(icons::kOpenInNew)) {
      OpenUrlInBrowser(info.source_url());
    }
    ImGui::HelpTooltip("Open url in browser");

    std::string repo_name = GetGithubRepoNameFromUrl(info.source_url());
    if (!repo_name.empty()) {
      if (ImGui::InputBool(
              "Automatically check for updates",
              PROTO_BOOL_FIELD(BundlePackInfo, &info, automatically_check_for_updates))) {
        open_edit = true;
      }
    }

    if (ImGui::InputBool("Archived", PROTO_BOOL_FIELD(BundlePackInfo, &info, archived))) {
      open_edit = true;
    }
    ImGui::SameLine();
    ImGui::HelpMarker("Bundle pack will not be loaded");

    ImGui::SpacedSeparator();

    if (ImGui::Button(std::format("{} Edit", icons::kEdit))) {
      open_edit = true;
    }

    if (!info.source_url().empty()) {
      if (ImGui::Button(std::format("{} Download update", icons::kDownload))) {
        BundlePackUpdateDialog::Item item;
        item.bundle_pack_name = info.bundle_pack_name();
        item.url = info.source_url();
        bundle_pack_update_dialog_->NotifyOpen({item});
      }
    }
    DrawOpenBundlesFolderButton();

    ImGui::SpacedSeparator();

    if (is_default_pack) {
      if (ImGui::Button(std::format("{} Reset default pack", icons::kRefresh))) {
        auto src_pack_path =
            app_.file_system().GetBasePath("resources/bundles") / kDefaultBundlePackFilename;
        auto dest_pack_path =
            app_.file_system().GetUserDataPath("bundles") / kDefaultBundlePackFilename;
        if (std::filesystem::exists(src_pack_path)) {
          std::filesystem::copy(
              src_pack_path, dest_pack_path, std::filesystem::copy_options::overwrite_existing);
          app_.bundle_manager().LoadBundlesFromDisk();
        }
      }
      ImGui::SameLine();
      ImGui::HelpMarker("Reset Default.pack.bin to version shipped with application binary.");
    } else {
      if (ImGui::Button(std::format("{} Delete", icons::kDelete))) {
        delete_pack_confirmation_dialog_.NotifyOpen(
            std::format("Delete \"{}\"?", info.bundle_pack_name()), info.bundle_pack_name());
      }
    }

    if (open_edit) {
      update_bundle_pack_info_dialog_ = std::make_unique<UpdateBundlePackInfoDialog>(info);
    }
  }

  void DrawBundleRightPanel() {
    if (!selected_bundle_name_.empty()) {
      DrawSelectedBundle();
    }
  }

  void DrawOpenBundlesFolderButton() {
    if (ImGui::Button(std::format("{} Folder", icons::kOpenInNew))) {
      OpenFolderInExplorer(app_.file_system().GetUserDataPath("bundles"));
    }
    ImGui::HelpTooltip(Tr("Open bundles folder in file explorer"));
  }

  void DrawSelectedBundle() {
    auto maybe_bundle_info = app_.bundle_manager().GetBundleInfo(selected_bundle_name_);
    if (!maybe_bundle_info) {
      ImGui::Text(Tr("Could not find bundle: %s"), selected_bundle_name_.c_str());
      return;
    }
    BundleInfo info = *maybe_bundle_info;

    ImGui::Text(Tr("Bundle name: %s"), selected_bundle_name_.c_str());

    bool need_update = false;
    bool reload_bundles_from_disk = false;

    if (selected_bundle_name_ == kUserBundleName) {
      // Force user bundle being writable if necessary.
      if (info.readonly()) {
        info.set_readonly(false);
        need_update = true;
      }
    } else {
      bool readonly = info.readonly();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s", Tr("Readonly"));
      ImGui::SameLine();
      if (ImGui::Checkbox("##ReadonlyInput", &readonly)) {
        info.set_readonly(readonly);
        need_update = true;
      }

      bool archived = info.archived();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Archived");
      ImGui::SameLine();
      if (ImGui::Checkbox("##ArchivedInput", &archived)) {
        info.set_archived(archived);
        need_update = true;
        reload_bundles_from_disk = true;
      }
      ImGui::SameLine();
      ImGui::HelpMarker("Bundle will not be loaded");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    CachedBundleDetails details = GetBundleDetails(selected_bundle_name_);
    ImGui::Text("%s", TrFormat("{} scenarios", details.num_scenarios).c_str());
    if (details.num_playlists > 0) {
      ImGui::Text("%s", TrFormat("{} playlists", details.num_playlists).c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(std::format("{} Copy", icons::kContentCopy))) {
      add_dialog_.NotifyOpen(selected_bundle_name_);
    }
    DrawOpenBundlesFolderButton();

    ImGui::SpacedSeparator();

    if (ImGui::Button(std::format("{} Delete", icons::kDelete))) {
      delete_confirmation_dialog_.NotifyOpen(std::format("Delete \"{}\"?", selected_bundle_name_),
                                             selected_bundle_name_);
    }

    if (need_update) {
      app_.bundle_manager().UpdateBundleInfo(info);
    }
    if (reload_bundles_from_disk) {
      app_.bundle_manager().LoadBundlesFromDisk();
    }
  }

  void DrawBundlesList() {
    ImGui::LoopId loop_id;
    auto bundle_infos = app_.bundle_manager().GetBundleInfos();
    for (const BundleInfo& bundle : bundle_infos) {
      auto id_guard = loop_id.Get();
      std::string display_text = " " + bundle.bundle_name();
      std::string help_text;
      if (bundle.archived()) {
        display_text = std::format("{} {}", display_text, icons::kArchive);
        help_text = "Archived";
      } else if (bundle.readonly()) {
        display_text = std::format("{} {}", display_text, icons::kEditOff);
        help_text = "Readonly";
      }
      if (ImGui::Selectable(display_text.c_str(), bundle.bundle_name() == selected_bundle_name_)) {
        selected_bundle_name_ = bundle.bundle_name();
      }
      if (help_text.size() > 0) {
        ImGui::HelpTooltip(help_text, 0.5);
      }
    }
  }

  CachedBundleDetails GetBundleDetails(const std::string& bundle_name) {
    auto it = bundle_details_map_.find(bundle_name);
    if (it != bundle_details_map_.end()) {
      return it->second;
    }

    CachedBundleDetails& details = bundle_details_map_[bundle_name];
    std::string bundle_name_prefix = bundle_name + " ";
    for (const std::string& name : *app_.playlist_manager().playlist_names()) {
      if (name.starts_with(bundle_name_prefix)) {
        details.num_playlists++;
      }
    }
    for (const std::string& name : *app_.scenario_manager().scenario_names()) {
      if (name.starts_with(bundle_name_prefix)) {
        details.num_scenarios++;
      }
    }
    return details;
  }

  UiScreen& screen_;
  Application& app_;

  ImGui::ConfirmationDialog<std::string> delete_confirmation_dialog_{"DeleteConfirmationDialog"};
  ImGui::ConfirmationDialog<std::string> delete_pack_confirmation_dialog_{
      "DeletePackConfirmationDialog"};
  std::string selected_bundle_name_ = "AF";
  std::string selected_bundle_pack_name_ = kDefaultBundlePackName;
  AddBundleDialog add_dialog_{"AddBundleDialog"};
  AddBundlePackDialog add_pack_dialog_;
  std::unordered_map<std::string, CachedBundleDetails> bundle_details_map_;
  std::unique_ptr<BundlePackUpdateDialog> bundle_pack_update_dialog_ =
      CreateBundlePackUpdateDialog();
  std::unique_ptr<UpdateBundlePackInfoDialog> update_bundle_pack_info_dialog_;
};

}  // namespace

std::unique_ptr<BundleUiComponent> CreateBundleUiComponent(UiScreen* screen) {
  return std::make_unique<BundleUiComponentImpl>(*screen);
}

}  // namespace aim
