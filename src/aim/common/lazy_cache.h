#pragma once

#include <string>

#include "absl/container/linked_hash_map.h"
#include "aim/common/simple_types.h"
#include "aim/common/times.h"

namespace aim {

struct LazyCacheOptions {
  int num_to_load = 1;
  // Amount of time between rereading cached items.
  i64 cache_stale_time_micros = 0;
  // Override now micros for testing.
  i64 explicit_now_micros = -1;
};

// A cache that lazily loads items in the order they are requested. The loading
// can be broken up across multiple UI rendering frames.
template <typename T>
class LazyCache {
 private:
  struct CacheItem {
    std::optional<T> value;
    i64 update_time_micros = -1;
    std::string key;
  };

 public:
  void Clear() {
    cache_.clear();
  }

  std::optional<T> Get(const std::string& key) {
    CacheItem& item = cache_[key];
    item.key = key;
    return item.value;
  }

  void LoadSomeItems(LazyCacheOptions options,
                     std::function<T(const std::string& key)> item_loader) {
    std::vector<CacheItem*> items;
    items.reserve(cache_.size());
    for (auto& entry : cache_) {
      items.push_back(&entry.second);
    }
    absl::c_stable_sort(items, &SortCacheItems);

    i64 now_micros =
        options.explicit_now_micros > 0 ? options.explicit_now_micros : GetNowEpochMicros();
    for (int i = 0; i < options.num_to_load && i < items.size(); ++i) {
      CacheItem* item = items[i];
      bool needs_refresh = options.cache_stale_time_micros <= 0 || item->update_time_micros < 0 ||
                           now_micros - item->update_time_micros > options.cache_stale_time_micros;
      if (needs_refresh) {
        item->update_time_micros = now_micros;
        item->value = item_loader(item->key);
      }
    }
  }

 private:
  static bool SortCacheItems(const CacheItem* lhs, const CacheItem* rhs) {
    return lhs->update_time_micros < rhs->update_time_micros;
  }

  absl::linked_hash_map<std::string, CacheItem> cache_;
};

}  // namespace aim
