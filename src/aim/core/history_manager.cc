#include "history_manager.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "aim/common/collections.h"

namespace aim {
namespace {

const int kCachedRecentNamesSize = 240;

class HistoryManagerImpl : public HistoryManager {
 public:
  explicit HistoryManagerImpl(AimDb* db) : db_(db) {}

  void UpdateRecentView(ObjectType type, const std::string& name) override {
    db_->UpdateRecentView(type, name);
    needs_reload_set_.insert(type);
  }

  void DeleteRecentView(ObjectType type, const std::string& name) override {
    db_->DeleteRecentView(type, name);
    needs_reload_set_.insert(type);
  }

  void ClearCache() override {
    recents_cache_.clear();
  }

  std::shared_ptr<std::vector<std::string>> GetCachedRecentNames(ObjectType type) override {
    bool needs_reload = needs_reload_set_.contains(type);
    if (needs_reload) {
      needs_reload_set_.erase(type);
      auto recents = std::make_shared<std::vector<std::string>>(
          GetRecentUniqueNames(type, kCachedRecentNamesSize));
      recents_cache_[type] = recents;
      return recents;
    }

    auto it = recents_cache_.find(type);
    if (it != recents_cache_.end()) {
      return it->second;
    }

    // Not in cache. Add value.
    auto recents = std::make_shared<std::vector<std::string>>(
        GetRecentUniqueNames(type, kCachedRecentNamesSize));
    recents_cache_[type] = recents;
    return recents;
  }

  std::vector<RecentViewV2> GetRecentViews(ObjectType type, int limit) override {
    return db_->GetRecentViews(type, limit);
  }

  std::vector<std::string> GetRecentUniqueNames(ObjectType type, int limit) override {
    auto views = GetRecentViews(type, limit);
    std::vector<std::string> result;
    for (auto& view : views) {
      if (!VectorContains(result, view.name)) {
        result.push_back(view.name);
      }
    }
    return result;
  }

 private:
  AimDb* db_;

  std::unordered_map<ObjectType, std::shared_ptr<std::vector<std::string>>> recents_cache_;
  std::unordered_set<ObjectType> needs_reload_set_;
};

}  // namespace

std::unique_ptr<HistoryManager> CreateHistoryManager(AimDb* db) {
  return std::make_unique<HistoryManagerImpl>(db);
}

}  // namespace aim
