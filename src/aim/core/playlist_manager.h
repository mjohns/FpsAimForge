#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "aim/common/name_util.h"
#include "aim/common/random.h"
#include "aim/proto/bundle.pb.h"
#include "aim/proto/playlist.pb.h"

namespace aim {

class ScenarioManager;

struct CopyPlaylistOptions {
  std::string remove_prefix;
  std::string add_prefix;
  bool deep_copy = false;
  bool as_references = false;
};

struct PlaylistItemProgress {
  PlaylistItem item;
  int runs_done = 0;

  bool IsDone() const {
    return runs_done >= item.num_plays();
  }
};

struct Playlist {
  std::string name;
  NameInfo playlist_name_info;

  PlaylistDef* mutable_def() {
    return &def_;
  }

  const PlaylistDef& def() const {
    return def_;
  }

  std::vector<PlaylistItem> items() const;

 private:
  PlaylistDef def_;
};

std::vector<PlaylistItem> GetPlaylistItems(const PlaylistDef& def);

struct PlaylistRun {
  Playlist playlist;

  void IncrementRunDone(const std::string& scenario_name);
  void Shuffle(Random& rand);

  // Returns the next scenario in the playlist and updates index if necessary. This will be the
  // current scenario if the number of specified runs aren't complete.
  std::optional<std::string> Next();
  std::optional<int> NextIndex();

  int current_index = -1;
  std::vector<PlaylistItemProgress> progress_list;
};

class PlaylistManager {
 public:
  PlaylistManager() {}
  virtual ~PlaylistManager() {}

  virtual void StartReload() = 0;
  virtual void LoadPlaylistsFromBundle(const std::string& bundle_name,
                                       const BundleFile& bundle) = 0;
  virtual void FinishReload() = 0;
  virtual std::unordered_set<std::string> GetDirtyBundles() = 0;
  virtual void ClearDirtyBundles() = 0;

  virtual void AddPlaylistsForBundle(const std::string& bundle_name, BundleFile* bundle_file) = 0;

  virtual void SetCurrentPlaylist(const std::string& name) = 0;
  virtual const std::string& current_playlist_name() const = 0;

  virtual std::shared_ptr<PlaylistRun> GetRun(const std::string& name) = 0;
  virtual void ClearRun(const std::string& name) = 0;
  virtual std::shared_ptr<PlaylistRun> GetCurrentRun() = 0;

  virtual std::shared_ptr<std::vector<std::string>> playlist_names() const = 0;

  virtual std::vector<std::string> FindPlaylistsContainingScenario(
      const std::string& scenario_name) const = 0;

  virtual std::optional<Playlist> GetPlaylist(const std::string& playlist_name) const = 0;

  virtual void AddScenarioToPlaylist(const std::string& playlist_name,
                                     const std::string& scenario_name) = 0;

  virtual void UpdatePlaylist(const std::string& name, const PlaylistDef& def) = 0;

  virtual bool DeletePlaylist(const std::string& name) = 0;

  virtual bool RenamePlaylist(const std::string& old_name, const std::string& new_name) = 0;

  virtual void RenameScenarioInAllPlaylists(const std::string& old_name,
                                            const std::string& new_name) = 0;

  virtual std::vector<std::string> GetAllRelativeNamesInBundle(const std::string& bundle_name) = 0;

  virtual std::optional<std::string> CopyPlaylist(const std::string& source_playlist_name,
                                                  const std::string& target_playlist_name,
                                                  ScenarioManager* scenario_manager,
                                                  CopyPlaylistOptions options) = 0;

  virtual void RegisterRenameListener(
      std::function<void(const std::string& old_name, const std::string& new_name)> listener) = 0;
};

std::unique_ptr<PlaylistManager> CreatePlaylistManager();

}  // namespace aim
