#include "bundle_pack_update_dialog.h"

#include <future>

#include "absl/cleanup/cleanup.h"
#include "absl/container/linked_hash_map.h"
#include "aim/common/files.h"
#include "aim/common/http.h"
#include "aim/common/imgui_ext.h"
#include "aim/common/log.h"
#include "aim/common/mat_icons.h"
#include "aim/core/application.h"
#include "aim/core/bundle_manager.h"
#include "aim/ui/ui_app.h"
#include "imgui.h"
#include "aim/i18n/i18n.h"

namespace aim {
namespace {

bool UpdateBundlePackOnBackgroundThread(const std::filesystem::path& write_path, std::string url) {
  std::string repo = GetGithubRepoNameFromUrl(url);
  std::vector<HttpHeader> headers;
  if (!repo.empty()) {
    url = std::format("https://api.github.com/repos/{}/zipball/main", repo);
    headers.push_back(kGithubApiVersionHeader);
  }

  FileDownload download;
  if (!DownloadFile(url, headers, &download)) {
    return false;
  }
  std::shared_ptr<BundlePack> pack = ConvertZipToBundlePack(download.content);
  if (!pack) {
    Logger::get()->warn("Failed to convert bundle pack zip file. {}", url);
    return false;
  }

  if (pack->items_size() <= 0) {
    Logger::get()->warn("Downloaded bundle pack has no items. {}", url);
    return false;
  }

  return WriteBinaryMessageToFile(write_path, *pack);
}

class BundlePackUpdateDialogImpl : public BundlePackUpdateDialog {
 public:
  void NotifyOpen(const std::vector<Item>& items) override {
    shown_updating_screen_count_ = 0;
    download_stopwatch_ = {};
    download_stopwatch_.Start();
    update_futures_.clear();
    complete_packs_.clear();
    items_ = items;
    for (const Item& item : items) {
      auto file_path = app_.file_system().GetUserDataPath("bundles") /
                       std::format("{}.pack.bin", item.bundle_pack_name);
      update_futures_[item.bundle_pack_name] =
          std::async(std::launch::async, UpdateBundlePackOnBackgroundThread, file_path, item.url);
    }
    popup_.Open();
  }

  void Draw(std::vector<std::string>* updated_bundle_packs_out) override {
    ImGui::IdGuard cid("BundlePackUpdateDialog");
    if (!popup_.BeginModal()) {
      return;
    }
    auto end_cleanup = absl::MakeCleanup([this] { popup_.End(); });

    for (const Item& item : items_) {
      auto it = update_futures_.find(item.bundle_pack_name);
      if (it == update_futures_.end()) {
        continue;
      }
      std::future_status status = it->second.wait_for(std::chrono::seconds(0));
      if (status != std::future_status::ready) {
        continue;
      }
      // Ready
      complete_packs_[item.bundle_pack_name] = it->second.get();
      update_futures_.erase(it);
    }

    if (!update_futures_.empty()) {
      // Still some pending.
      DrawCompletePacksList();
      float elapsed_seconds = download_stopwatch_.GetElapsedSeconds();
      for (auto& entry : update_futures_) {
        ImGui::TextFmt("Downloading {} - {:.1f}s", entry.first, elapsed_seconds);
      }
      if (elapsed_seconds > 10) {
        ImGui::SpacedSeparator();
        if (ImGui::Button(Tr("Cancel"))) {
          ClosePopupAndCleanup();
        }
      }
      return;
    }

    // All are complete. Transition to updating screen and then after that is rendered reload
    // bundles from disk.
    bool any_failed = false;
    std::vector<std::string> updated_packs;
    for (auto& entry : complete_packs_) {
      bool updated_successfully = entry.second;
      if (updated_successfully) {
        updated_packs.push_back(entry.first);
      } else {
        any_failed = true;
      }
    }

    bool any_updated = !updated_packs.empty();

    if (any_updated && shown_updating_screen_count_ < 5) {
      DrawCompletePacksList();
      ImGui::SpacedSeparator();
      ImGui::Text("Loading bundles");
      shown_updating_screen_count_++;
      return;
    }

    if (any_updated) {
      app_.bundle_manager().LoadBundlesFromDisk();
    }

    if (!any_failed) {
      *updated_bundle_packs_out = updated_packs;
      ClosePopupAndCleanup();
      return;
    }

    // Some failed. Leave the popup open showing which failed.
    DrawCompletePacksList();
    ImGui::SpacedSeparator();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Some updates failed");
    ImGui::SameLine();
    if (ImGui::Button(Tr("Ok"))) {
      *updated_bundle_packs_out = updated_packs;
      ClosePopupAndCleanup();
    }
  }

  void DrawCompletePacksList() {
    for (auto& entry : complete_packs_) {
      if (entry.second) {
        ImGui::TextFmt("{} {}", entry.first, icons::kVerified);
      } else {
        ImGui::TextFmt("{} {}", entry.first, icons::kCancel);
      }
    }
  }

  void ClosePopupAndCleanup() {
    update_futures_.clear();
    complete_packs_.clear();
    popup_.Close();
  }

 private:
  Application& app_ = GetUiApp();
  ImGui::Popup popup_{"BundlePackUpdateDialog"};
  absl::linked_hash_map<std::string, std::future<bool>> update_futures_;
  absl::linked_hash_map<std::string, bool> complete_packs_;
  Stopwatch download_stopwatch_;
  std::vector<Item> items_;
  int shown_updating_screen_count_ = 0;
};

}  // namespace

std::unique_ptr<BundlePackUpdateDialog> CreateBundlePackUpdateDialog() {
  return std::make_unique<BundlePackUpdateDialogImpl>();
}

}  // namespace aim
