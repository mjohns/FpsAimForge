#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "aim/common/simple_types.h"
#include "aim/core/file_system.h"
#include "aim/database/local_store_db.h"

namespace aim {

class LocalStore {
 public:
  explicit LocalStore(FileSystem* fs);
  AIM_NO_COPY(LocalStore);

  void Put(const std::string& key, const std::string& value);
  void Remove(const std::string& key);
  std::string Get(const std::string& key);

  void PutInt(const std::string& key, int value);
  std::optional<int> GetInt(const std::string& key);

  void PutBool(const std::string& key, bool value);
  std::optional<bool> GetOptionalBool(const std::string& key);
  bool GetBool(const std::string& key);

 private:
  std::unique_ptr<LocalStoreDb> local_store_db_;
  std::unordered_map<std::string, std::string> value_cache_;
};

}  // namespace aim
