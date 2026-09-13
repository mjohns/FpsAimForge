#include "aim/common/backup.h"

#include <fstream>
#include <random>

#include "aim/common/times.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace aim;

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

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

  bool WriteBackup(const std::string& name, std::string content) {
    std::ofstream file(temp_dir_path_ / name);
    if (!file.is_open()) {
      return false;
    }
    file << content;
    file.close();
    return true;
  }

  std::string ReadBackup(const std::string& name) {
    std::ifstream file(temp_dir_path_ / name);
    if (!file.is_open()) {
      return "";
    }
    std::string content =
        std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    file.close();
    return content;
  }

  auto EqualsBackup(const std::string& name, const std::string& date) {
    auto expected_path = temp_dir_path_ / name;
    return AllOf(Field(&ExistingBackup::date, StrEq(date)),
                 Field(&ExistingBackup::path, Eq(expected_path)));
  }
};

TEST_F(BackupTest, TestSimpleBackup_NoExistingBackups) {
  BackupOptions options;
  options.max_backups = 1;
  SimpleBackupActions actions = GetSimpleBackupActions({}, options, "2026-09-11");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_TRUE(actions.make_new_backup);
}

TEST_F(BackupTest, TestSimpleBackup_NotTimeForNewBackup) {
  BackupOptions options;
  options.max_backups = 10;
  options.backup_every_n_days = 1;
  SimpleBackupActions actions = GetSimpleBackupActions({"2026-09-11"}, options, "2026-09-11");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_FALSE(actions.make_new_backup);

  options.backup_every_n_days = 2;
  actions = GetSimpleBackupActions({"2026-09-11"}, options, "2026-09-12");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_FALSE(actions.make_new_backup);

  options.backup_every_n_days = 2;
  actions = GetSimpleBackupActions({"2026-09-13"}, options, "2026-09-12");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_FALSE(actions.make_new_backup);
}

TEST_F(BackupTest, TestSimpleBackup_BackupNoDeletions) {
  BackupOptions options;
  options.max_backups = 3;
  options.backup_every_n_days = 1;
  SimpleBackupActions actions =
      GetSimpleBackupActions({"2026-09-11", "2026-09-10"}, options, "2026-09-12");
  EXPECT_THAT(actions.delete_backups, IsEmpty());
  EXPECT_TRUE(actions.make_new_backup);
}

TEST_F(BackupTest, TestSimpleBackup_BackupDeleteOne) {
  BackupOptions options;
  options.max_backups = 2;
  options.backup_every_n_days = 1;
  SimpleBackupActions actions =
      GetSimpleBackupActions({"2026-09-11", "2026-09-10"}, options, "2026-09-12");
  EXPECT_THAT(actions.delete_backups, ElementsAre("2026-09-10"));
  EXPECT_TRUE(actions.make_new_backup);
}

TEST_F(BackupTest, TestSimpleBackup_BackupDeleteMany) {
  BackupOptions options;
  options.max_backups = 2;
  options.backup_every_n_days = 1;
  SimpleBackupActions actions = GetSimpleBackupActions(
      {"2026-09-11", "2026-09-10", "2026-09-08", "2025-09-12"}, options, "2026-09-12");
  EXPECT_THAT(actions.delete_backups, ElementsAre("2025-09-12", "2026-09-08", "2026-09-10"));
  EXPECT_TRUE(actions.make_new_backup);
}

TEST_F(BackupTest, TestParseYyyymmddFromBackupName) {
  const std::string prefix = "aim_";
  EXPECT_THAT(ParseDateFromBackupName("aim_2012-01-22.db", prefix), Optional(StrEq("2012-01-22")));
  EXPECT_THAT(ParseDateFromBackupName("aim_2012-01-22", prefix), Optional(StrEq("2012-01-22")));
  EXPECT_THAT(ParseDateFromBackupName("aim_2012-01-2.db", prefix), Eq(std::nullopt));
  EXPECT_THAT(ParseDateFromBackupName("aim_", prefix), Eq(std::nullopt));
  EXPECT_THAT(ParseDateFromBackupName("aimfoo_2012-01-22", prefix), Eq(std::nullopt));
}

TEST_F(BackupTest, GetExistingBackups) {
  ASSERT_TRUE(WriteBackup("aim_2025-01-01.db", "1"));
  ASSERT_TRUE(WriteBackup("aim_2025-01-02.db", "2"));
  ASSERT_TRUE(WriteBackup("aim_2025-01-03.db", "3"));
  ASSERT_TRUE(WriteBackup("aim_2025-01-0", "bad"));

  ASSERT_TRUE(WriteBackup("other_aim_2025-01-03.db", "4"));
  ASSERT_TRUE(WriteBackup("other_aim_2025-01-04.db", "5"));

  EXPECT_THAT(GetExistingBackups(temp_dir_path_, "aim_"),
              UnorderedElementsAre(EqualsBackup("aim_2025-01-01.db", "2025-01-01"),
                                   EqualsBackup("aim_2025-01-02.db", "2025-01-02"),
                                   EqualsBackup("aim_2025-01-03.db", "2025-01-03")));

  EXPECT_THAT(GetExistingBackups(temp_dir_path_, "other_aim_"),
              UnorderedElementsAre(EqualsBackup("other_aim_2025-01-03.db", "2025-01-03"),
                                   EqualsBackup("other_aim_2025-01-04.db", "2025-01-04")));
}

TEST_F(BackupTest, GetBackupActions) {
  ASSERT_TRUE(WriteBackup("aim_2025-01-01.db", "1"));
  ASSERT_TRUE(WriteBackup("aim_2025-01-02.db", "2"));
  ASSERT_TRUE(WriteBackup("aim_2025-01-03.db", "3"));

  ASSERT_TRUE(WriteBackup("aim_2025-01-0", "bad"));
  ASSERT_TRUE(WriteBackup("other_aim_2025-01-03.db", "4"));
  ASSERT_TRUE(WriteBackup("other_aim_2025-01-04.db", "5"));

  BackupOptions options;
  options.backup_every_n_days = 1;
  options.max_backups = 2;
  BackupActions actions = GetBackupActions(temp_dir_path_, "aim_", options, "2025-01-04");
  EXPECT_TRUE(actions.make_new_backup);
  EXPECT_THAT(actions.delete_backups,
              UnorderedElementsAre(Eq(temp_dir_path_ / "aim_2025-01-01.db"),
                                   Eq(temp_dir_path_ / "aim_2025-01-02.db")));
}
