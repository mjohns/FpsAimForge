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

  int now = YyyymmddToEpochDays(now_date);

  std::vector<BackupDate> existing_dates;
  for (const std::string& val : existing_backups) {
    BackupDate d;
    d.date = val;
    d.date_num = YyyymmddToEpochDays(val);
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

}  // namespace aim
