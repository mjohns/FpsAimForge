#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

#include "aim/proto/bundle.pb.h"
#include "aim/proto/guide.pb.h"

namespace aim {

struct GuideItem {
  std::string name;
  GuideDef def;
};

class GuideManager {
 public:
  GuideManager() {}
  virtual ~GuideManager() {}

  virtual void StartReload() = 0;
  virtual void LoadGuidesFromBundle(const std::string& bundle_name, const BundleFile& bundle) = 0;
  virtual void FinishReload() = 0;
  virtual std::unordered_set<std::string> GetDirtyBundles() = 0;
  virtual void ClearDirtyBundles() = 0;

  virtual void SetCurrentGuide(const std::string& name) = 0;
  virtual const std::string& current_guide_name() const = 0;

  virtual void AddGuidesForBundle(const std::string& bundle_name, BundleFile* bundle_file) = 0;

  virtual std::optional<GuideItem> GetGuide(const std::string& guide_name) = 0;

  std::optional<GuideItem> GetCurrentGuide() {
    return GetGuide(current_guide_name());
  }

  virtual std::string QuickCopyGuide(const std::string& guide_name, const std::string& bundle_name) = 0;

  virtual std::shared_ptr<std::vector<std::string>> guide_names() const = 0;

  virtual std::vector<std::string> GetAllRelativeNamesInBundle(const std::string& bundle_name) = 0;

  virtual void UpdateGuide(const std::string& name, const GuideDef& def) = 0;

  virtual bool DeleteGuide(const std::string& name) = 0;

  virtual bool RenameGuide(const std::string& old_name, const std::string& new_name) = 0;

  virtual void RenamePlaylistInAllGuides(const std::string& old_name,
                                         const std::string& new_name) = 0;

  virtual void RegisterRenameListener(
      std::function<void(const std::string& old_name, const std::string& new_name)> listener) = 0;
};

std::unique_ptr<GuideManager> CreateGuideManager();

}  // namespace aim
