#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "aim/common/simple_types.h"

namespace aim {

// Return yyyymmdd strings for backups to create and delete.
struct SimpleBackupActions {
  bool make_new_backup = false;
  std::vector<std::string> delete_backups;
};

struct BackupOptions {
  int max_backups = 10;
  int backup_every_n_days = 1;
};

// BackupActions GetBackupActions(const std::filesystem::path& backup_dir,
//                                const std::string& name_prefix,
//                                const BackupOptions& options,
//                                i64 now_micros);

SimpleBackupActions GetSimpleBackupActions(const std::vector<std::string>& existing_backups,
                               const BackupOptions& options,
                               const std::string& now_date);

}  // namespace aim
