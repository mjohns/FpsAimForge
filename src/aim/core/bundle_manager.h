#pragma once

#include <string>

#include "aim/proto/bundle.pb.h"

namespace aim {

class PlaylistManager;
class ScenarioManager;
class GuideManager;
class FileSystem;

inline const char* kUserBundleName = "USER";
inline const char* kDefaultBundlePackName = "Default";
inline const char* kDefaultBundlePackFilename = "Default.pack.bin";

struct LoadedBundleDetails {
  std::vector<std::string> bundle_packs;
  bool loaded_from_file = false;
  std::string loaded_from_bundle_pack;
};

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

  virtual std::optional<BundlePackInfo> GetBundlePackInfo(const std::string& pack_name) = 0;
  virtual std::vector<std::string> GetBundlePackNames() = 0;
  virtual void UpdateBundlePackInfo(const BundlePackInfo& info,
                                    const std::string& remove_old_pack_name = "") = 0;
  virtual void DeleteBundlePack(const std::string& pack_name) = 0;

  virtual const LoadedBundleDetails& GetLoadedBundleDetails(const std::string& bundle_name) = 0;
};

std::unique_ptr<BundleManager> CreateBundleManager(FileSystem* fs,
                                                   PlaylistManager* playlist_manager,
                                                   ScenarioManager* scenario_manager,
                                                   GuideManager* guide_manager);

bool IsValidBundleName(const std::string& bundle_name);

std::shared_ptr<BundlePack> ConvertZipToBundlePack(const std::string& zip_content);

std::string GetGithubRepoNameFromUrl(const std::string& url);

}  // namespace aim
