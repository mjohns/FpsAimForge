#pragma once

#include <string>

#include "aim/proto/bundle.pb.h"

namespace aim {

class PlaylistManager;
class ScenarioManager;
class GuideManager;
class FileSystem;

inline const char* kUserBundleName = "USER";

class BundleManager {
 public:
  BundleManager() {}
  virtual ~BundleManager() {}

  // Returns error messages if there were issues loading the bundles.
  virtual std::vector<std::string> LoadBundlesFromDisk() = 0;

  virtual std::vector<std::string> GetBundleNames() = 0;
  virtual std::vector<std::string> GetWritableBundleNames() = 0;
  virtual std::string GetDefaultWritableBundleName() = 0;
  virtual std::vector<BundleInfo> GetBundleInfos() = 0;
  virtual bool IsBundleReadonly(const std::string& bundle_name) = 0;
  virtual std::optional<BundleInfo> GetBundleInfo(const std::string& bundle_name) = 0;
  virtual void UpdateBundleInfo(const BundleInfo& info) = 0;

  virtual void DeleteBundle(const std::string& bundle_name) = 0;
  virtual bool SaveBundle(const std::string& bundle_name) = 0;
  virtual bool SaveDirtyBundles() = 0;
  virtual std::unordered_set<std::string> GetDirtyBundles() = 0;

  virtual bool CopyBundle(const std::string& source_bundle_name,
                          const std::string& target_bundle_name) = 0;
};

std::unique_ptr<BundleManager> CreateBundleManager(FileSystem* fs,
                                                   PlaylistManager* playlist_manager,
                                                   ScenarioManager* scenario_manager,
                                                   GuideManager* guide_manager);

bool IsValidBundleName(const std::string& bundle_name);

}  // namespace aim
