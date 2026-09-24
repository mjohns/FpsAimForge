#pragma once

#include <memory>
#include <string>

#include "aim/ui/ui_screen.h"

namespace aim {

struct PlaylistEditorOptions {
  std::string name;
  bool is_new_playlist = false;
};
std::unique_ptr<UiScreen> CreatePlaylistEditorScreen(const PlaylistEditorOptions& options);

}  // namespace aim
