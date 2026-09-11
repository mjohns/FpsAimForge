#include "bundle_manager.h"

#include <cctype>
#include <filesystem>

#include "absl/algorithm/container.h"
#include "absl/strings/strip.h"
#include "aim/common/collections.h"
#include "aim/common/files.h"
#include "aim/common/proto_util.h"
#include "aim/core/file_system.h"
#include "aim/core/guide_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"

namespace aim {
namespace {

constexpr const char* kBundleFileNameSuffix = ".bundle.json";

bool IsValidBundleNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

void AddBundlesFromDirectory(
    const std::filesystem::path& base_dir,
    std::unordered_map<std::string, std::filesystem::path>* bundle_path_map,
    std::vector<std::string>* error_messages) {
  if (!std::filesystem::exists(base_dir)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(base_dir)) {
    std::string filename = entry.path().filename().string();
    if (!filename.ends_with(kBundleFileNameSuffix)) {
      continue;
    }
    std::string bundle_name(absl::StripSuffix(filename, kBundleFileNameSuffix));
    if (IsValidBundleName(bundle_name)) {
      (*bundle_path_map)[bundle_name] = entry.path();
    } else {
      error_messages->push_back(std::format("Invalid bundle name \"{}\"", bundle_name));
    }
  }
}

bool BundleInfoNameLessThan(const BundleInfo& lhs, const BundleInfo& rhs) {
  return lhs.bundle_name() < rhs.bundle_name();
}

BundleInfoFile NormalizeBundleInfoFile(
    const BundleInfoFile& original_file,
    const std::unordered_map<std::string, std::filesystem::path>& bundle_path_map) {
  BundleInfoFile file = original_file;

  std::unordered_set<std::string> existing_bundle_names;
  for (auto& bundle : file.bundles()) {
    existing_bundle_names.insert(bundle.bundle_name());
  }

  // Add all missing bundles as readonly.
  for (const auto& entry : bundle_path_map) {
    const std::string& bundle_name = entry.first;
    if (!existing_bundle_names.contains(bundle_name)) {
      auto* item = file.add_bundles();
      item->set_bundle_name(bundle_name);
      item->set_readonly(true);
    }
  }

  absl::c_sort(*file.mutable_bundles(), &BundleInfoNameLessThan);
  return file;
}

class BundleManagerImpl : public BundleManager {
 public:
  explicit BundleManagerImpl(FileSystem* fs,
                             PlaylistManager* playlist_manager,
                             ScenarioManager* scenario_manager,
                             GuideManager* guide_manager)
      : fs_(fs),
        playlist_manager_(playlist_manager),
        scenario_manager_(scenario_manager),
        guide_manager_(guide_manager),
        bundle_info_file_path_(fs->GetUserDataPath("bundles/bundles.json")) {}

  std::vector<std::string> LoadBundlesFromDisk() override {
    bundle_info_map_.clear();

    std::vector<std::string> error_messages;

    std::unordered_map<std::string, std::filesystem::path> bundle_path_map;
    AddBundlesFromDirectory(
        fs_->GetBasePath("resources/bundles"), &bundle_path_map, &error_messages);
    AddBundlesFromDirectory(fs_->GetUserDataPath("bundles"), &bundle_path_map, &error_messages);

    BundleInfoFile bundle_info_file;
    if (std::filesystem::exists(bundle_info_file_path_)) {
      if (!ReadJsonMessageFromFile(bundle_info_file_path_, &bundle_info_file)) {
        error_messages.push_back("Unable to parse bundles.json");
        return error_messages;
      }
    }

    BundleInfoFile normalized_bundle_info_file =
        NormalizeBundleInfoFile(bundle_info_file, bundle_path_map);
    if (!IsEquivalentProto(bundle_info_file, normalized_bundle_info_file)) {
      WriteJsonMessageToFile(bundle_info_file_path_, normalized_bundle_info_file);
      bundle_info_file = normalized_bundle_info_file;
    }

    for (const auto& bundle_info : bundle_info_file.bundles()) {
      bundle_info_map_[bundle_info.bundle_name()] = bundle_info;
    }

    scenario_manager_->StartReload();
    playlist_manager_->StartReload();
    guide_manager_->StartReload();
    for (auto& entry : bundle_path_map) {
      std::string bundle_name = entry.first;
      std::filesystem::path bundle_path = entry.second;

      auto maybe_info = GetBundleInfo(bundle_name);
      if (maybe_info && maybe_info->archived()) {
        continue;
      }

      BundleFile bundle_file;
      if (ReadJsonMessageFromFile(bundle_path, &bundle_file)) {
        scenario_manager_->LoadScenariosFromBundle(bundle_name, bundle_file);
        playlist_manager_->LoadPlaylistsFromBundle(bundle_name, bundle_file);
        guide_manager_->LoadGuidesFromBundle(bundle_name, bundle_file);
      } else {
        error_messages.push_back(std::format("Unable to parse bundle \"{}\"", bundle_name));
      }
    }
    scenario_manager_->FinishReload();
    playlist_manager_->FinishReload();
    guide_manager_->FinishReload();
    return error_messages;
  }

  bool SaveBundle(const std::string& bundle_name) override {
    auto existing_bundle_info = GetBundleInfo(bundle_name);
    if (existing_bundle_info) {
      bool is_readonly = existing_bundle_info->readonly();
      if (is_readonly) {
        return false;
      }
    } else {
      // Create the new bundle as writable.
      BundleInfo new_info;
      new_info.set_bundle_name(bundle_name);
      UpdateBundleInfo(new_info);
    }
    BundleFile bundle_file;
    playlist_manager_->AddPlaylistsForBundle(bundle_name, &bundle_file);
    scenario_manager_->AddScenariosForBundle(bundle_name, &bundle_file);
    guide_manager_->AddGuidesForBundle(bundle_name, &bundle_file);
    return WriteJsonMessageToFile(GetMutableBundleFilePath(bundle_name), bundle_file);
  }

  bool CopyBundle(const std::string& source_bundle_name,
                  const std::string& target_bundle_name) override {
    std::string source_bundle_prefix = source_bundle_name + " ";
    std::string target_bundle_prefix = target_bundle_name + " ";

    BundleFile bundle_file;
    playlist_manager_->AddPlaylistsForBundle(source_bundle_name, &bundle_file);
    scenario_manager_->AddScenariosForBundle(source_bundle_name, &bundle_file);
    guide_manager_->AddGuidesForBundle(source_bundle_name, &bundle_file);

    auto change_bundle_name = [&](const std::string& name) {
      std::string_view result_view = name;
      if (!absl::ConsumePrefix(&result_view, source_bundle_prefix)) {
        return name;
      }
      return absl::StrCat(target_bundle_prefix, result_view);
    };

    // Update any internal references to the old bundle name to point to the new bundle.

    for (BundleScenario& s : *bundle_file.mutable_scenarios()) {
      if (s.def().reference_def().scenario_name().starts_with(source_bundle_prefix)) {
        s.mutable_def()->mutable_reference_def()->set_scenario_name(
            change_bundle_name(s.def().reference_def().scenario_name()));
      }
    }

    for (BundlePlaylist& p : *bundle_file.mutable_playlists()) {
      for (PlaylistItem& item : *p.mutable_def()->mutable_items()) {
        if (item.scenario().starts_with(source_bundle_prefix)) {
          item.set_scenario(change_bundle_name(item.scenario()));
        }
      }
      if (p.def().levels().base_scenario().starts_with(source_bundle_prefix)) {
        p.mutable_def()->mutable_levels()->set_base_scenario(
            change_bundle_name(p.def().levels().base_scenario()));
      }
    }

    bool saved = WriteJsonMessageToFile(GetMutableBundleFilePath(target_bundle_name), bundle_file);
    if (saved) {
      LoadBundlesFromDisk();
    }
    return saved;
  }

  std::unordered_set<std::string> GetDirtyBundles() override {
    std::unordered_set<std::string> dirty_bundles;
    InsertAll(&dirty_bundles, scenario_manager_->GetDirtyBundles());
    InsertAll(&dirty_bundles, playlist_manager_->GetDirtyBundles());
    InsertAll(&dirty_bundles, guide_manager_->GetDirtyBundles());
    return dirty_bundles;
  }

  bool SaveDirtyBundles() override {
    auto dirty_bundles = GetDirtyBundles();
    bool some_failed = false;
    for (const std::string& bundle_name : dirty_bundles) {
      if (!SaveBundle(bundle_name)) {
        some_failed = true;
      }
    }

    // TODO: Maybe only clear the bundles that were actually saved.
    scenario_manager_->ClearDirtyBundles();
    playlist_manager_->ClearDirtyBundles();
    guide_manager_->ClearDirtyBundles();

    return !some_failed;
  }

  std::vector<std::string> GetBundleNames() override {
    std::vector<std::string> names;
    for (auto& entry : bundle_info_map_) {
      if (!entry.second.archived()) {
        names.push_back(entry.first);
      }
    }
    if (names.empty()) {
      names.push_back(kUserBundleName);
    }
    absl::c_sort(names);
    return names;
  }
  std::vector<std::string> GetWritableBundleNames() override {
    std::vector<std::string> names;
    for (auto& entry : bundle_info_map_) {
      if (!entry.second.readonly() && !entry.second.archived()) {
        names.push_back(entry.first);
      }
    }
    if (names.empty()) {
      names.push_back(kUserBundleName);
    }
    absl::c_sort(names);
    return names;
  }

  void UpdateBundleInfo(const BundleInfo& info) override {
    if (!IsValidBundleName(info.bundle_name())) {
      assert(false && "Saving bundle with invalid name");
      return;
    }
    bundle_info_map_[info.bundle_name()] = info;
    SaveBundlesJsonFile();
  }

  void DeleteBundle(const std::string& bundle_name) override {
    bundle_info_map_.erase(bundle_name);
    MoveFileToTrash(GetMutableBundleFilePath(bundle_name));
    SaveBundlesJsonFile();
    LoadBundlesFromDisk();
  }

  std::optional<BundleInfo> GetBundleInfo(const std::string& bundle_name) override {
    auto it = bundle_info_map_.find(bundle_name);
    if (it != bundle_info_map_.end()) {
      return it->second;
    }
    return {};
  }

  std::vector<BundleInfo> GetBundleInfos() override {
    std::vector<BundleInfo> result;
    result.reserve(bundle_info_map_.size());
    for (const auto& entry : bundle_info_map_) {
      result.push_back(entry.second);
    }
    absl::c_sort(result, &BundleInfoNameLessThan);
    return result;
  }

  bool IsBundleReadonly(const std::string& bundle_name) override {
    auto it = bundle_info_map_.find(bundle_name);
    if (it != bundle_info_map_.end()) {
      return it->second.readonly();
    }
    return false;
  }

 private:
  std::filesystem::path GetMutableBundleFilePath(const std::string& bundle_name) {
    return fs_->GetUserDataPath("bundles") / (bundle_name + kBundleFileNameSuffix);
  }

  // Saves the bundle metadata file containing info about the bundles avaialable.
  void SaveBundlesJsonFile() {
    BundleInfoFile file;
    for (auto& entry : bundle_info_map_) {
      *file.add_bundles() = entry.second;
    }
    absl::c_sort(*file.mutable_bundles(), &BundleInfoNameLessThan);
    WriteJsonMessageToFile(bundle_info_file_path_, file);
  }

  FileSystem* fs_;
  PlaylistManager* playlist_manager_;
  ScenarioManager* scenario_manager_;
  GuideManager* guide_manager_;
  std::filesystem::path bundle_info_file_path_;
  std::unordered_map<std::string, BundleInfo> bundle_info_map_;
};

}  // namespace

std::unique_ptr<BundleManager> CreateBundleManager(FileSystem* fs,
                                                   PlaylistManager* playlist_manager,
                                                   ScenarioManager* scenario_manager,
                                                   GuideManager* guide_manager) {
  return std::make_unique<BundleManagerImpl>(fs, playlist_manager, scenario_manager, guide_manager);
}

bool IsValidBundleName(const std::string& bundle_name) {
  if (bundle_name.empty()) {
    return false;
  }
  for (char c : bundle_name) {
    if (!IsValidBundleNameChar(c)) {
      return false;
    }
  }
  return true;
}

}  // namespace aim
