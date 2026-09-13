#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aim {

std::optional<std::string> ParseDateFromBackupName(const std::string& backup_name,
                                                   const std::string& prefix);

struct BackupOptions {
  int max_backups = 10;
  int backup_every_n_days = 1;
};

// yyyy-mm-dd strings for backups to create and delete.
struct SimpleBackupActions {
  bool make_new_backup = false;
  std::vector<std::string> delete_backups;
};

// Provide existing_backups as the yyyy-mm-dd values that exist.
SimpleBackupActions GetSimpleBackupActions(const std::vector<std::string>& existing_backups,
                                           const BackupOptions& options,
                                           const std::string& now_date);

struct ExistingBackup {
  std::filesystem::path path;
  std::string date;
};
std::vector<ExistingBackup> GetExistingBackups(const std::filesystem::path& backup_dir,
                                               const std::string& name_prefix);

std::optional<std::string> ParseDateFromBackupName(const std::string& backup_name,
                                                   const std::string& prefix);

struct BackupActions {
  bool make_new_backup = false;
  std::vector<std::filesystem::path> delete_backups;
};

BackupActions GetBackupActions(const std::filesystem::path& backup_dir,
                               const std::string& name_prefix,
                               const BackupOptions& options,
                               const std::string& now_date);

std::string GetNowBackupDate();

}  // namespace aim
