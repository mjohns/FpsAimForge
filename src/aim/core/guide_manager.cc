#include "guide_manager.h"

#include <algorithm>
#include <cassert>
#include <unordered_set>

#include "absl/container/flat_hash_map.h"
#include "aim/common/name_util.h"
#include "aim/common/resource_name.h"

namespace aim {
namespace {

class GuideManagerImpl : public GuideManager {
 public:
  explicit GuideManagerImpl() {
    guide_names_ = std::make_shared<std::vector<std::string>>();
  }

  void StartReload() override {
    guide_map_.clear();
  }

  void LoadGuidesFromBundle(const std::string& bundle_name, const BundleFile& bundle) override {
    for (const BundleGuide& guide : bundle.guides()) {
      ResourceName name(bundle_name, guide.name());
      std::string full_name = name.full_name();
      auto& item = guide_map_[full_name];
      item.name = full_name;
      item.def = guide.def();
    }
  }

  void FinishReload() override {
    UpdateGuideListFromMap();
  }

  void AddGuidesForBundle(const std::string& bundle_name, BundleFile* bundle_file) override {
    for (const std::string& full_name : *guide_names_) {
      ResourceName name = ResourceName::Parse(full_name);
      if (name.bundle_name() == bundle_name) {
        BundleGuide& bundle_guide = *bundle_file->add_guides();
        bundle_guide.set_name(name.relative_name());
        *bundle_guide.mutable_def() = guide_map_[full_name].def;
      }
    }
  }

  std::shared_ptr<std::vector<std::string>> guide_names() const override {
    return guide_names_;
  }

  std::optional<GuideItem> GetGuide(const std::string& guide_name) override {
    auto it = guide_map_.find(guide_name);
    if (it != guide_map_.end()) {
      return it->second;
    }
    return {};
  }

  std::vector<std::string> GetAllRelativeNamesInBundle(const std::string& bundle_name) override {
    std::vector<std::string> names;
    for (const GuideItem& playlist : *guides_) {
      ResourceName name = ResourceName::Parse(playlist.name);
      if (name.bundle_name() == bundle_name) {
        names.push_back(name.relative_name());
      }
    }
    return names;
  }

  void SetCurrentGuide(const std::string& name) override {
    current_guide_name_ = name;
  }

  const std::string& current_guide_name() const override {
    return current_guide_name_;
  }

  void UpdateGuide(const std::string& name, const GuideDef& def) override {
    auto& g = guide_map_[name];
    g.name = name;
    g.def = def;
    UpdateGuideListFromMap();
    dirty_bundles_.insert(GetBundleName(name));
  }

  bool DeleteGuide(const std::string& name) override {
    guide_map_.erase(name);
    UpdateGuideListFromMap();
    dirty_bundles_.insert(GetBundleName(name));
    return true;
  }

  bool RenameGuide(const std::string& old_name, const std::string& new_name) override {
    if (current_guide_name_ == old_name) {
      current_guide_name_ = new_name;
    }
    if (guide_map_.contains(new_name)) {
      return false;
    }
    auto it = guide_map_.find(old_name);
    if (it != guide_map_.end()) {
      GuideDef def = it->second.def;
      guide_map_.erase(it);

      auto& new_guide = guide_map_[new_name];
      new_guide.name = new_name;
      new_guide.def = def;

      UpdateGuideListFromMap();
    }
    RenameGuideInAllGuides(old_name, new_name);

    for (auto& listener : rename_listeners_) {
      listener(old_name, new_name);
    }

    return true;
  }

  std::unordered_set<std::string> GetDirtyBundles() override {
    return dirty_bundles_;
  }

  void ClearDirtyBundles() override {
    dirty_bundles_.clear();
  }

  void RenamePlaylistInAllGuides(const std::string& old_name,
                                 const std::string& new_name) override {
    std::string old_base_name = GetPlaylistNameInfo(old_name).base_name;
    std::string new_base_name = GetPlaylistNameInfo(new_name).base_name;

    auto guides_copy = guides_;
    for (const GuideItem& guide : *guides_copy) {
      bool changed = false;
      GuideDef def = guide.def;
      for (auto& section : *def.mutable_sections()) {
        for (std::string& playlist : *section.mutable_playlists()) {
          NameInfo item_name_info = GetPlaylistNameInfo(playlist);
          if (item_name_info.base_name == old_base_name) {
            changed = true;
            item_name_info.base_name = new_base_name;
            playlist = item_name_info.GetFullName();
          }
        }
      }

      if (changed) {
        UpdateGuide(guide.name, def);
      }
    }
  }

  // Renames references to the guide from within guides.
  void RenameGuideInAllGuides(const std::string& old_name, const std::string& new_name) {
    auto guides_copy = guides_;
    for (const GuideItem& guide : *guides_copy) {
      bool changed = false;
      GuideDef def = guide.def;
      for (auto& section : *def.mutable_sections()) {
        for (std::string& guide : *section.mutable_guides()) {
          if (guide == old_name) {
            changed = true;
            guide = new_name;
          }
        }
      }

      if (changed) {
        UpdateGuide(guide.name, def);
      }
    }
  }

  void RegisterRenameListener(std::function<void(const std::string& old_name,
                                                 const std::string& new_name)> listener) override {
    rename_listeners_.push_back(std::move(listener));
  }

 private:
  void UpdateGuideListFromMap() {
    auto new_guides = std::make_shared<std::vector<GuideItem>>();
    auto new_guide_names = std::make_shared<std::vector<std::string>>();
    new_guides->reserve(guide_map_.size());
    new_guide_names->reserve(guide_map_.size());
    for (auto& entry : guide_map_) {
      new_guides->push_back(entry.second);
      new_guide_names->push_back(entry.second.name);
    }
    std::sort(new_guides->begin(),
              new_guides->end(),
              [](const GuideItem& lhs, const GuideItem& rhs) { return lhs.name < rhs.name; });
    std::sort(new_guide_names->begin(),
              new_guide_names->end(),
              [](const std::string& lhs, const std::string& rhs) { return lhs < rhs; });

    guides_ = new_guides;
    guide_names_ = new_guide_names;
  }

  std::shared_ptr<std::vector<GuideItem>> guides_;
  std::shared_ptr<std::vector<std::string>> guide_names_;
  absl::flat_hash_map<std::string, GuideItem> guide_map_;
  std::unordered_set<std::string> dirty_bundles_;
  std::vector<std::function<void(const std::string& old_name, const std::string& new_name)>>
      rename_listeners_;
  std::string current_guide_name_;
};

}  // namespace

std::unique_ptr<GuideManager> CreateGuideManager() {
  return std::make_unique<GuideManagerImpl>();
}

}  // namespace aim
