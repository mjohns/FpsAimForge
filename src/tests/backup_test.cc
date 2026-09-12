#include "aim/common/backup.h"

#include <random>

#include "aim/common/times.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace aim;

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::StrEq;

class BackupTest : public ::testing::Test {
 protected:
  std::filesystem::path temp_dir_path_;

  void SetUp() override {
    std::filesystem::path base_temp_path = std::filesystem::temp_directory_path();

    i64 timestamp = GetNowEpochMicros();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1000, 9999);
    int random_suffix = distrib(gen);

    std::string unique_dir_name =
        "gtest_temp_backup_test_" + std::to_string(timestamp) + "_" + std::to_string(random_suffix);
    temp_dir_path_ = base_temp_path / unique_dir_name;

    ASSERT_TRUE(std::filesystem::create_directory(temp_dir_path_))
        << "Failed to create temporary directory: " << temp_dir_path_;
  }

  void TearDown() override {
    if (std::filesystem::exists(temp_dir_path_)) {
      ASSERT_TRUE(std::filesystem::remove_all(temp_dir_path_))
          << "Failed to remove temporary directory: " << temp_dir_path_;
    }
  }
};

TEST(BackupTest, TestSimpleBackup_NoExistingBackups) {
  BackupOptions options;
  options.max_backups = 1;
  SimpleBackupActions actions = GetSimpleBackupActions({}, options, "20260911");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_TRUE(actions.make_new_backup);
}

TEST(BackupTest, TestSimpleBackup_NotTimeForNewBackup) {
  BackupOptions options;
  options.max_backups = 10;
  options.backup_every_n_days = 1;
  SimpleBackupActions actions = GetSimpleBackupActions({"20260911"}, options, "20260911");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_FALSE(actions.make_new_backup);

  options.backup_every_n_days = 2;
  actions = GetSimpleBackupActions({"20260911"}, options, "20260912");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_FALSE(actions.make_new_backup);

  options.backup_every_n_days = 2;
  actions = GetSimpleBackupActions({"20260913"}, options, "20260912");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_FALSE(actions.make_new_backup);
}

TEST(BackupTest, TestSimpleBackup_BackupNoDeletions) {
  BackupOptions options;
  options.max_backups = 3;
  options.backup_every_n_days = 1;
  SimpleBackupActions actions =
      GetSimpleBackupActions({"20260911", "20260910"}, options, "20260912");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_TRUE(actions.make_new_backup);
}

TEST(BackupTest, TestSimpleBackup_BackupDeleteOne) {
  BackupOptions options;
  options.max_backups = 2;
  options.backup_every_n_days = 1;
  SimpleBackupActions actions =
      GetSimpleBackupActions({"20260911", "20260910"}, options, "20260912");
  EXPECT_THAT(actions.delete_backups, ElementsAre("20260910"));
  EXPECT_TRUE(actions.make_new_backup);
}

TEST(BackupTest, TestSimpleBackup_BackupDeleteMany) {
  BackupOptions options;
  options.max_backups = 2;
  options.backup_every_n_days = 1;
  SimpleBackupActions actions =
      GetSimpleBackupActions({"20260911", "20260910", "20260908", "20250912"}, options, "20260912");
  EXPECT_THAT(actions.delete_backups, ElementsAre("20250912", "20260908", "20260910"));
  EXPECT_TRUE(actions.make_new_backup);
}
