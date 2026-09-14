#pragma once

#include <memory>
#include <optional>
#include <string>

#include "aim/core/playlist_manager.h"
#include "aim/ui/ui_screen.h"

namespace aim {

void PlaylistRunComponent(const std::string& id, std::shared_ptr<PlaylistRun> playlist_run);

class PlaylistComponent {
 public:
  virtual ~PlaylistComponent() {}

  struct Options {
    bool is_playlist_screen = true;
    bool open_editing = false;
  };
  virtual void Show(std::shared_ptr<PlaylistRun> run, Options options) = 0;
};

std::unique_ptr<PlaylistComponent> CreatePlaylistComponent();

struct PlaylistListResult {
  std::optional<Playlist> open_playlist{};
  std::optional<std::string> edit_playlist{};
};

class PlaylistListComponent {
 public:
  virtual ~PlaylistListComponent() {}

  // Returns whether to open an individual playlist.
  virtual void Show(PlaylistListResult* result) = 0;
};

std::unique_ptr<PlaylistListComponent> CreatePlaylistListComponent(UiScreen* screen);

}  // namespace aim
