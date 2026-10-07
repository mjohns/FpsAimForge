#include "bundle_manager.h"

#include <cctype>
#include <filesystem>

#include "absl/algorithm/container.h"
#include "absl/strings/strip.h"
#include "aim/common/collections.h"
#include "aim/common/files.h"
#include "aim/common/log.h"
#include "aim/core/file_system.h"
#include "aim/core/guide_manager.h"
#include "aim/core/playlist_manager.h"
#include "aim/core/scenario_manager.h"

namespace aim {
namespace {

constexpr const char* kBundleFileNameSuffix = ".bundle.json";
constexpr const char* kBundlePackFileNameSuffix = ".bundle.pack";

bool IsValidBundleNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

void AddBundlesFromDirectory(
    const std::filesystem::path& base_dir,
    std::unordered_map<std::string, std::filesystem::path>* bundle_path_map,
    std::unordered_map<std::string, std::filesystem::path>* bundle_pack_path_map,
    std::vector<std::string>* error_messages) {
  if (!std::filesystem::exists(base_dir)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(base_dir)) {
    std::string filename = entry.path().filename().string();
    if (filename.ends_with(kBundleFileNameSuffix)) {
      std::string bundle_name(absl::StripSuffix(filename, kBundleFileNameSuffix));
      if (IsValidBundleName(bundle_name)) {
        (*bundle_path_map)[bundle_name] = entry.path();
      } else {
        error_messages->push_back(std::format("Invalid bundle name \"{}\"", bundle_name));
      }
    }
    if (filename.ends_with(kBundlePackFileNameSuffix)) {
      std::string bundle_pack_name(absl::StripSuffix(filename, kBundlePackFileNameSuffix));
      (*bundle_pack_path_map)[bundle_pack_name] = entry.path();
    }
  }
}

bool BundleInfoNameLessThan(const BundleInfo& lhs, const BundleInfo& rhs) {
  return lhs.bundle_name() < rhs.bundle_name();
}

bool BundlePackInfoNameLessThan(const BundlePackInfo& lhs, const BundlePackInfo& rhs) {
  return lhs.bundle_pack_name() < rhs.bundle_pack_name();
}

std::unordered_map<std::string, BundlePack> LoadBundlePackFiles(
    const std::unordered_map<std::string, std::filesystem::path> bundle_pack_path_map,
    const std::unordered_map<std::string, BundlePackInfo> info_map,
    std::vector<std::string>* error_messages) {
  std::unordered_map<std::string, BundlePack> result;
  for (auto& entry : bundle_pack_path_map) {
    std::string pack_name = entry.first;
    auto it = info_map.find(pack_name);
    if (it != info_map.end()) {
      bool archived = it->second.archived();
      if (archived) {
        continue;
      }
    }

    BundlePack pack;
    if (!ReadBinaryMessageFromFile(entry.second, &pack)) {
      std::string message = std::format(
          "Unable to parse bundle pack {} at path {}", pack_name, entry.second.string());
      Logger::get()->warn(message);
      error_messages->push_back(message);
      continue;
    }

    result[pack_name] = pack;
  }

  return result;
}

std::unordered_map<std::string, BundleFile> GetBundlesFromPacks(
    const std::unordered_map<std::string, BundlePack>& pack_map,
    const std::unordered_map<std::string, BundleInfo>& bundle_info_map) {
  std::unordered_map<std::string, BundleFile> result;
  for (const auto& entry : pack_map) {
    for (const auto& bundle : entry.second.items()) {
      auto it = bundle_info_map.find(bundle.name());
      if (it == bundle_info_map.end()) {
        // No special options for bundle. Always include.
        result[bundle.name()] = bundle.file();
        continue;
      }
      const BundleInfo& info = it->second;
      if (info.archived()) {
        continue;
      }
      const std::string& pack_name = entry.first;
      if (info.has_bundle_pack_name() && info.bundle_pack_name() != pack_name) {
        // Not the correct pack for this bundle.
        continue;
      }
      result[bundle.name()] = bundle.file();
    }
  }
  return result;
}

BundleInfoFile NormalizeBundleInfoFile(
    const BundleInfoFile& original_file,
    const std::unordered_map<std::string, std::filesystem::path>& bundle_path_map,
    const std::unordered_map<std::string, std::filesystem::path>& bundle_pack_path_map) {
  BundleInfoFile file = original_file;

  std::unordered_set<std::string> existing_bundle_names;
  for (auto& bundle : file.bundles()) {
    existing_bundle_names.insert(bundle.bundle_name());
  }

  std::unordered_set<std::string> existing_bundle_pack_names;
  for (auto& pack : file.bundle_packs()) {
    existing_bundle_names.insert(pack.bundle_pack_name());
  }

  // Add all missing bundles as readonly.
  for (const auto& entry : bundle_path_map) {
    const std::string& bundle_name = entry.first;
    if (!existing_bundle_names.contains(bundle_name)) {
      auto* item = file.add_bundles();
      item->set_bundle_name(bundle_name);
      if (bundle_name != kUserBundleName) {
        item->set_readonly(true);
      }
    }
  }

  for (const auto& entry : bundle_path_map) {
    const std::string& bundle_name = entry.first;
    if (!existing_bundle_names.contains(bundle_name)) {
      auto* item = file.add_bundles();
      item->set_bundle_name(bundle_name);
      item->set_readonly(true);
    }
  }

  absl::c_sort(*file.mutable_bundles(), &BundleInfoNameLessThan);
  absl::c_sort(*file.mutable_bundle_packs(), &BundlePackInfoNameLessThan);
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
    bundle_pack_info_map_.clear();

    std::vector<std::string> error_messages;

    BundleInfoFile bundle_info_file;
    if (std::filesystem::exists(bundle_info_file_path_)) {
      if (!ReadJsonMessageFromFile(bundle_info_file_path_, &bundle_info_file)) {
        Logger::get()->warn("Unable to parse bundles.json");
        error_messages.push_back("Unable to parse bundles.json");
      }
    }

    std::unordered_map<std::string, std::filesystem::path> bundle_path_map;
    std::unordered_map<std::string, std::filesystem::path> bundle_pack_path_map;
    AddBundlesFromDirectory(fs_->GetBasePath("resources/bundles"),
                            &bundle_path_map,
                            &bundle_pack_path_map,
                            &error_messages);
    AddBundlesFromDirectory(
        fs_->GetUserDataPath("bundles"), &bundle_path_map, &bundle_pack_path_map, &error_messages);

    for (const auto& bundle_info : bundle_info_file.bundles()) {
      bundle_info_map_[bundle_info.bundle_name()] = bundle_info;
      if (bundle_info.bundle_name() == kUserBundleName) {
        bundle_info_map_[bundle_info.bundle_name()].clear_readonly();
      }
    }
    for (const auto& bundle_pack_info : bundle_info_file.bundle_packs()) {
      bundle_pack_info_map_[bundle_pack_info.bundle_pack_name()] = bundle_pack_info;
    }

    std::unordered_map<std::string, BundlePack> pack_map =
        LoadBundlePackFiles(bundle_pack_path_map, bundle_pack_info_map_, &error_messages);

    std::unordered_map<std::string, BundleFile> bundle_map =
        GetBundlesFromPacks(pack_map, bundle_info_map_);

    // Individual bundle files always overwrite the same bundle found in a pack.
    // If a bundle becomes writable and changes are made, the changes will always be in
    // an individual bundle file within the user bundles folder.
    for (auto& entry : bundle_path_map) {
      std::string bundle_name = entry.first;
      std::filesystem::path bundle_path = entry.second;

      BundleFile bundle_file;
      if (ReadJsonMessageFromFile(bundle_path, &bundle_file)) {
        bundle_map[bundle_name] = bundle_file;
      } else {
        error_messages.push_back(std::format("Unable to parse bundle \"{}\"", bundle_name));
      }
    }

    scenario_manager_->StartReload();
    playlist_manager_->StartReload();
    guide_manager_->StartReload();

    for (auto& entry : bundle_map) {
      std::string bundle_name = entry.first;
      BundleInfo& info = bundle_info_map_[bundle_name];
      if (info.bundle_name().empty()) {
        // Initialize unknown bundles to be readonly.
        info.set_bundle_name(bundle_name);
        info.set_readonly(true);
      }

      if (info.archived()) {
        continue;
      }

      scenario_manager_->LoadScenariosFromBundle(bundle_name, entry.second);
      playlist_manager_->LoadPlaylistsFromBundle(bundle_name, entry.second);
      guide_manager_->LoadGuidesFromBundle(bundle_name, entry.second);
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

  std::string GetDefaultWritableBundleName() override {
    auto bundles = GetWritableBundleNames();
    return bundles.size() > 0 ? bundles[0] : kUserBundleName;
  }

  void UpdateBundleInfo(const BundleInfo& original_info) override {
    if (!IsValidBundleName(original_info.bundle_name())) {
      assert(false && "Saving bundle with invalid name");
      return;
    }
    BundleInfo& info = bundle_info_map_[original_info.bundle_name()];
    info = original_info;
    if (info.bundle_name() == kUserBundleName) {
      info.clear_readonly();
    }
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
      if (it->second.bundle_name() == kUserBundleName) {
        // Make sure user bundle is never marked readonly.
        it->second.clear_readonly();
      }
      return it->second;
    }
    return {};
  }

  std::vector<BundleInfo> GetBundleInfos() override {
    std::vector<BundleInfo> result;
    result.reserve(bundle_info_map_.size());
    for (const auto& entry : bundle_info_map_) {
      BundleInfo info = entry.second;
      if (info.bundle_name() == kUserBundleName) {
        info.clear_readonly();
      }
      result.push_back(info);
    }
    absl::c_sort(result, &BundleInfoNameLessThan);
    return result;
  }

  bool IsBundleReadonly(const std::string& bundle_name) override {
    if (bundle_name == kUserBundleName) {
      return false;
    }
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
    for (auto& entry : bundle_pack_info_map_) {
      *file.add_bundle_packs() = entry.second;
    }
    absl::c_sort(*file.mutable_bundles(), &BundleInfoNameLessThan);
    absl::c_sort(*file.mutable_bundle_packs(), &BundlePackInfoNameLessThan);
    WriteJsonMessageToFile(bundle_info_file_path_, file);
  }

  FileSystem* fs_;
  PlaylistManager* playlist_manager_;
  ScenarioManager* scenario_manager_;
  GuideManager* guide_manager_;
  std::filesystem::path bundle_info_file_path_;

  std::unordered_map<std::string, BundleInfo> bundle_info_map_;
  std::unordered_map<std::string, BundlePackInfo> bundle_pack_info_map_;
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
