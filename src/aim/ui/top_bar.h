#pragma once

#include <memory>

namespace aim {

class TopBar {
 public:
  virtual ~TopBar() {}

  struct Result {
    bool do_update_clicked = false;
  };
  virtual void DrawEx(bool update_available, Result* result) = 0;

  void Draw() {
    DrawEx(false, nullptr);
  }
};

std::unique_ptr<TopBar> CreateTopBar();

}  // namespace aim
