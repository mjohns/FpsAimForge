#include "backup.h"

#include "aim/common/collections.h"
#include "aim/common/times.h"

namespace aim {
namespace {

struct BackupDate {
  std::string date;
  int date_num;
};

static bool SortDates(const BackupDate& lhs, const BackupDate& rhs) {
  return lhs.date_num < rhs.date_num;
}

}  // namespace

SimpleBackupActions GetSimpleBackupActions(const std::vector<std::string>& existing_backups,
                                           const BackupOptions& options,
                                           const std::string& now_date) {
  if (VectorContains(existing_backups, now_date)) {
    // Backup for today exists. Don't do anything.
    return {};
  }
  if (options.backup_every_n_days <= 0 || options.max_backups <= 0) {
    // Not supported
    return {};
  }

  if (existing_backups.size() == 0) {
    SimpleBackupActions actions;
    actions.make_new_backup = true;
    return actions;
  }

  int now = IsoDateToEpochDays(now_date);

  std::vector<BackupDate> existing_dates;
  for (const std::string& val : existing_backups) {
    BackupDate d;
    d.date = val;
    d.date_num = IsoDateToEpochDays(val);
    existing_dates.push_back(d);
  }

  absl::c_sort(existing_dates, &SortDates);

  int most_recent_backup = existing_dates.back().date_num;
  int most_recent_backup_age = now - most_recent_backup;

  if (most_recent_backup_age < options.backup_every_n_days) {
    // Too soon to make a new backup. Do nothing.
    return {};
  }

  // Making a new backup. See how many of old backups we should drop.
  SimpleBackupActions actions;
  actions.make_new_backup = true;

  int target_backups_size = options.max_backups - 1;  // Minus the new backup we are making.
  int num_to_delete = existing_backups.size() - target_backups_size;
  if (num_to_delete <= 0) {
    return actions;
  }

  for (int i = 0; i < num_to_delete && i < existing_dates.size(); ++i) {
    actions.delete_backups.push_back(existing_dates[i].date);
  }

  return actions;
}

std::optional<std::string> ParseDateFromBackupName(const std::string& backup_name,
                                                       const std::string& prefix) {
  if (!backup_name.starts_with(prefix)) {
    return {};
  }

  int date_len = 10;
  if (backup_name.size() < prefix.size() + date_len) {
    return {};
  }

  std::string date = backup_name.substr(prefix.size(), date_len);
  int date_num = IsoDateToEpochDays(date);
  if (date_num > 0) {
    return date;
  }
  return {};
}

std::vector<ExistingBackup> GetExistingBackups(const std::filesystem::path& backup_dir,
                                               const std::string& name_prefix) {
  std::vector<ExistingBackup> backups;
  if (!std::filesystem::exists(backup_dir)) {
    return backups;
  }
  if (!std::filesystem::is_directory(backup_dir)) {
    return backups;
  }
  for (const auto& entry : std::filesystem::directory_iterator(backup_dir)) {
    std::string filename = entry.path().filename().string();
    auto maybe_date = ParseDateFromBackupName(filename, name_prefix);
    if (maybe_date) {
      ExistingBackup backup;
      backup.path = entry.path();
      backup.date = *maybe_date;
      backups.push_back(backup);
    }
  }
  return backups;
}

BackupActions GetBackupActions(const std::filesystem::path& backup_dir,
                               const std::string& name_prefix,
                               const BackupOptions& options,
                               const std::string& now_date) {
  std::vector<ExistingBackup> existing_backups = GetExistingBackups(backup_dir, name_prefix);
  std::vector<std::string> backup_dates;
  for (auto& backup : existing_backups) {
    backup_dates.push_back(backup.date);
  }

  SimpleBackupActions simple_actions = GetSimpleBackupActions(backup_dates, options, now_date);

  BackupActions actions;
  actions.make_new_backup = simple_actions.make_new_backup;

  for (const ExistingBackup& existing_backup : existing_backups) {
    if (VectorContains(simple_actions.delete_backups, existing_backup.date)) {
      actions.delete_backups.push_back(existing_backup.path);
    }
  }

  return actions;
}

std::string GetNowBackupDate() {
  absl::TimeZone tz = absl::LocalTimeZone();
  return EpochSecondsToIsoDate(GetNowEpochSeconds(), tz);
}

}  // namespace aim
