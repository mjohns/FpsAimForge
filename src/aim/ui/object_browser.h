#pragma once

#include <memory>
#include <optional>
#include <string>

#include "aim/common/object_type.h"

namespace aim {

class ObjectBrowser {
 public:
  virtual ~ObjectBrowser() {}

  struct Result {
    std::optional<std::string> selected_object_name{};
    std::optional<std::string> copy_object_name{};
    std::optional<std::string> edit_object_name{};
    std::optional<std::string> create_level_playlist_for_scenario{};
  };

  virtual void Draw(Result* result) = 0;
};

std::unique_ptr<ObjectBrowser> CreateObjectBrowser(ObjectType type);

}  // namespace aim
