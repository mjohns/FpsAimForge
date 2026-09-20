#include "aim/database/aim_db.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <string>

#include "aim/common/log.h"
#include "aim/common/times.h"
#include "gmock/gmock.h"
#include "google/protobuf/message.h"
#include "gtest/gtest.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

using namespace aim;
using google::protobuf::Message;
using ::protobuf_matchers::EqualsProto;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::ResultOf;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

namespace {

auto EqualsStats(const StatsDbRow& expected) {
  return AllOf(Field(&StatsDbRow::stats_id, Eq(expected.stats_id)),
               Field(&StatsDbRow::epoch_seconds, Eq(expected.epoch_seconds)),
               Field(&StatsDbRow::score, Eq(expected.score)),
               Field(&StatsDbRow::mm_per_360, Eq(expected.mm_per_360)),
               Field(&StatsDbRow::info, EqualsProto(expected.info)));
}

auto EqualsPlayTimes(int total, int partial) {
  return AllOf(Field(&PlayTimes::complete_run_time_seconds, Eq(total)),
               Field(&PlayTimes::partial_run_time_seconds, Eq(partial)));
}

auto EqualsViewName(const std::string& expected) {
  return AllOf(Field(&RecentViewV2::name, Eq(expected)));
}

}  // namespace

class AimDbTest : public ::testing::Test {
 protected:
  std::filesystem::path temp_db_path_;
  std::filesystem::path db_path_;
  std::unique_ptr<AimDb> db_;

  void SetUp() override {
    std::filesystem::path base_temp_path = std::filesystem::temp_directory_path();

    i64 timestamp = GetNowEpochMicros();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1000, 9999);
    int random_suffix = distrib(gen);

    std::string db_file_name =
        "gtest_aim_db_test_db_" + std::to_string(timestamp) + "_" + std::to_string(random_suffix);
    db_path_ = base_temp_path / db_file_name;
    db_ = CreateAimDb(db_path_);
  }

  void TearDown() override {
    if (db_) {
      db_ = {};
    }
    // Logger::get()->info("Db path {}", db_path_.string());
    if (std::filesystem::exists(db_path_)) {
      std::filesystem::remove(db_path_);
    }
    Logger::getInstance().logger()->flush();
  }
};

TEST_F(AimDbTest, GetScenarioId) {
  i64 id1 = db_->GetScenarioId("s1");
  EXPECT_THAT(db_->GetScenarioId("s1"), Eq(id1));

  i64 id2 = db_->GetScenarioId("s2");
  EXPECT_THAT(db_->GetScenarioId("s2"), Eq(id2));
  EXPECT_THAT(id2, Ne(id1));

  db_->RenameScenario("s2", "s3");

  i64 id3 = db_->GetScenarioId("s3");
  EXPECT_THAT(id3, Eq(id2));

  i64 new_id2 = db_->GetScenarioId("s2");
  EXPECT_THAT(new_id2, Ne(id2));

  db_->RenameScenario("missing", "s4");
  i64 id4 = db_->GetScenarioId("s4");

  EXPECT_THAT(
      db_->GetScenarioIdMap(),
      UnorderedElementsAre(Pair("s1", id1), Pair("s3", id3), Pair("s2", new_id2), Pair("s4", id4)));
}

TEST_F(AimDbTest, GetScenarioNamesWithPrefix) {
  db_->GetScenarioId("Scenario Foo");
  db_->GetScenarioId("Scenario Foo L1");
  db_->GetScenarioId("Scenario Foo L2");
  db_->GetScenarioId("Scenario Foo L2 25cm");
  db_->GetScenarioId("Scenario Bar L2 25cm");
  db_->GetScenarioId("Scenario Bar");
  db_->GetScenarioId("Scenario Baz");

  EXPECT_THAT(db_->GetScenarioNamesWithPrefix("Scenario Baz"),
              UnorderedElementsAre("Scenario Baz"));

  EXPECT_THAT(db_->GetScenarioNamesWithPrefix("Scenario Bar"),
              UnorderedElementsAre("Scenario Bar", "Scenario Bar L2 25cm"));

  EXPECT_THAT(db_->GetScenarioNamesWithPrefix("Scenario Foo"),
              UnorderedElementsAre(
                  "Scenario Foo", "Scenario Foo L1", "Scenario Foo L2", "Scenario Foo L2 25cm"));
}

TEST_F(AimDbTest, GetPlaylistNameMap) {
  i64 id1 = db_->GetPlaylistId("p1");
  ASSERT_THAT(id1, Gt(0));
  i64 id2 = db_->GetPlaylistId("p2");
  ASSERT_THAT(id2, Gt(0));
  i64 id3 = db_->GetPlaylistId("p3");
  ASSERT_THAT(id3, Gt(0));
  i64 id4 = db_->GetPlaylistId("p4");
  ASSERT_THAT(id4, Gt(0));
  ASSERT_THAT(db_->GetPlaylistId("p4"), Eq(id4));

  EXPECT_THAT(
      db_->GetPlaylistIdMap(),
      UnorderedElementsAre(Pair("p1", id1), Pair("p2", id2), Pair("p3", id3), Pair("p4", id4)));

  db_->RenamePlaylist("p3", "p5");
  i64 id5 = db_->GetPlaylistId("p5");

  ASSERT_THAT(id5, Eq(id3));

  i64 new_id3 = db_->GetPlaylistId("p3");
  ASSERT_THAT(new_id3, Ne(id3));

  EXPECT_THAT(
      db_->GetPlaylistIdMap(),
      UnorderedElementsAre(
          Pair("p1", id1), Pair("p2", id2), Pair("p5", id5), Pair("p4", id4), Pair("p3", new_id3)));
}

TEST_F(AimDbTest, RenameBasePlaylist) {
  i64 id1 = db_->GetPlaylistId("p1");
  i64 id2 = db_->GetPlaylistId("p1 15cm");
  i64 id3 = db_->GetPlaylistId("p1 25cm");

  i64 other_id1 = db_->GetPlaylistId("p2");
  i64 other_id2 = db_->GetPlaylistId("p3");

  db_->RenamePlaylist("p1", "p4");

  EXPECT_THAT(db_->GetPlaylistId("p4"), Eq(id1));
  EXPECT_THAT(db_->GetPlaylistId("p4 15cm"), Eq(id2));
  EXPECT_THAT(db_->GetPlaylistId("p4 25cm"), Eq(id3));

  EXPECT_THAT(db_->GetPlaylistId("p2"), Eq(other_id1));
  EXPECT_THAT(db_->GetPlaylistId("p3"), Eq(other_id2));
}

TEST_F(AimDbTest, GetGuideNameMap) {
  i64 id1 = db_->GetGuideId("p1");
  ASSERT_THAT(id1, Gt(0));
  i64 id2 = db_->GetGuideId("p2");
  ASSERT_THAT(id2, Gt(0));
  i64 id3 = db_->GetGuideId("p3");
  ASSERT_THAT(id3, Gt(0));
  i64 id4 = db_->GetGuideId("p4");
  ASSERT_THAT(id4, Gt(0));
  ASSERT_THAT(db_->GetGuideId("p4"), Eq(id4));

  EXPECT_THAT(
      db_->GetGuideIdMap(),
      UnorderedElementsAre(Pair("p1", id1), Pair("p2", id2), Pair("p3", id3), Pair("p4", id4)));

  db_->RenameGuide("p3", "p5");
  i64 id5 = db_->GetGuideId("p5");
  ASSERT_THAT(id5, Eq(id3));

  i64 new_id3 = db_->GetGuideId("p3");
  ASSERT_THAT(new_id3, Ne(id3));

  EXPECT_THAT(
      db_->GetGuideIdMap(),
      UnorderedElementsAre(
          Pair("p1", id1), Pair("p2", id2), Pair("p5", id5), Pair("p4", id4), Pair("p3", new_id3)));
}

TEST_F(AimDbTest, GetScenarioNameFromId) {
  i64 id1 = db_->GetScenarioId("s1");
  i64 id2 = db_->GetScenarioId("s2");

  EXPECT_THAT(db_->GetScenarioName(id1), StrEq("s1"));
  EXPECT_THAT(db_->GetScenarioName(id2), StrEq("s2"));
  EXPECT_THAT(db_->GetScenarioName(-1), StrEq(""));
}

TEST_F(AimDbTest, CreateAndUpdateScenarioSettings) {
  ScenarioSettings settings;
  i64 id = db_->GetScenarioId("Scenario");
  EXPECT_THAT(db_->GetScenarioSettings(id), EqualsProto(settings));

  settings.set_cm_per_360(35);
  db_->UpdateScenarioSettings(id, settings);
  EXPECT_THAT(db_->GetScenarioSettings(id), EqualsProto(settings));

  settings.set_cm_per_360(45);
  db_->UpdateScenarioSettings(id, settings);
  EXPECT_THAT(db_->GetScenarioSettings(id), EqualsProto(settings));
}

TEST_F(AimDbTest, GetScenarioSettings_ValueNull) {
  i64 id = db_->GetScenarioId("Scenario");
  EXPECT_THAT(db_->GetScenarioSettings(id), EqualsProto(ScenarioSettings{}));
}

TEST_F(AimDbTest, GetScenarioNameMap_NoEntries) {
  EXPECT_THAT(db_->GetScenarioIdMap(), IsEmpty());
}

TEST_F(AimDbTest, GetScenarioNameMap) {
  i64 id1 = db_->GetScenarioId("Scenario1");
  ASSERT_THAT(id1, Gt(0));
  i64 id2 = db_->GetScenarioId("Scenario2");
  ASSERT_THAT(id2, Gt(0));
  i64 id3 = db_->GetScenarioId("Scenario3");
  ASSERT_THAT(id3, Gt(0));
  i64 id4 = db_->GetScenarioId("Scenario4");
  ASSERT_THAT(id4, Gt(0));

  EXPECT_THAT(db_->GetScenarioIdMap(),
              UnorderedElementsAre(Pair("Scenario1", id1),
                                   Pair("Scenario2", id2),
                                   Pair("Scenario3", id3),
                                   Pair("Scenario4", id4)));
}

TEST_F(AimDbTest, ReadAndWriteStats) {
  i64 scenario1 = db_->GetScenarioId("Scenario1");
  i64 scenario2 = db_->GetScenarioId("Scenario2");
  i64 scenario3 = db_->GetScenarioId("Scenario3");

  EXPECT_THAT(db_->GetLatestStatsId(scenario1), Eq(0));

  StatsDbRow stats1_1;
  stats1_1.mm_per_360 = 350;
  stats1_1.score = 9.9;
  stats1_1.info.set_num_hits(10);
  ASSERT_TRUE(db_->AddStats(scenario1, &stats1_1));
  ASSERT_THAT(db_->GetStats(scenario1), ElementsAre(EqualsStats(stats1_1)));
  EXPECT_THAT(db_->GetLatestStatsId(scenario1), Eq(stats1_1.stats_id));

  EXPECT_THAT(stats1_1.stats_id, Eq(1));
  EXPECT_THAT(stats1_1.epoch_seconds, Gt(0));

  ASSERT_THAT(db_->GetStats(scenario2), IsEmpty());
  ASSERT_THAT(db_->GetStats(scenario3), IsEmpty());

  StatsDbRow stats1_2;
  stats1_2.mm_per_360 = 450;
  stats1_2.score = 10.1;
  stats1_2.info.set_num_hits(100);
  ASSERT_TRUE(db_->AddStats(scenario1, &stats1_2));
  ASSERT_THAT(db_->GetStats(scenario1), ElementsAre(EqualsStats(stats1_1), EqualsStats(stats1_2)));
  EXPECT_THAT(db_->GetLatestStatsId(scenario1), Eq(stats1_2.stats_id));

  EXPECT_THAT(db_->GetLatestStatsId(scenario2), Eq(0));

  db_->DeleteStats(scenario1, stats1_2.stats_id);
  ASSERT_THAT(db_->GetStats(scenario1), ElementsAre(EqualsStats(stats1_1)));

  StatsDbRow stats2_1;
  stats2_1.mm_per_360 = 360;
  stats2_1.score = 2454;
  stats2_1.info.set_num_hits(11);
  ASSERT_TRUE(db_->AddStats(scenario2, &stats2_1));
  ASSERT_THAT(db_->GetStats(scenario2), ElementsAre(EqualsStats(stats2_1)));

  ASSERT_TRUE(db_->AddStats(scenario2, &stats2_1));
  ASSERT_THAT(db_->GetStats(scenario2).size(), Eq(2));

  db_->DeleteAllStats(scenario2);
  ASSERT_THAT(db_->GetStats(scenario2), IsEmpty());

  ASSERT_THAT(db_->GetStats(scenario1), ElementsAre(EqualsStats(stats1_1)));
  EXPECT_THAT(db_->GetLatestStatsId(scenario1), Eq(stats1_1.stats_id));
}

TEST_F(AimDbTest, PlayTime) {
  i64 scenario1 = db_->GetScenarioId("Scenario1");
  i64 scenario2 = db_->GetScenarioId("Scenario2");
  i64 scenario3 = db_->GetScenarioId("Scenario3");

  auto make_details = [](ShotType::TypeCase shot_type, bool is_complete, int cm_per_360) {
    PlayTimeDetails d;
    d.shot_type = shot_type;
    d.is_complete_run = is_complete;
    d.cm_per_360 = cm_per_360;
    return d;
  };

  ASSERT_TRUE(
      db_->AddPlayTime(scenario1, 1, make_details(ShotType::TypeCase::kClickMulti, true, 35)));

  TotalPlaytime playtime = db_->GetTotalPlaytime();
  EXPECT_THAT(playtime.total.complete_run_time_seconds, Eq(1));
  EXPECT_THAT(playtime.total.partial_run_time_seconds, Eq(0));

  EXPECT_THAT(playtime.play_times_by_shot_type[ShotType::TypeCase::kClickMulti],
              EqualsPlayTimes(1, 0));
  EXPECT_THAT(playtime.play_times_by_cm_per_360[35], EqualsPlayTimes(1, 0));

  ASSERT_TRUE(
      db_->AddPlayTime(scenario1, 12.5, make_details(ShotType::TypeCase::kClickMulti, true, 35)));
  ASSERT_TRUE(
      db_->AddPlayTime(scenario1, 2, make_details(ShotType::TypeCase::kClickMulti, true, 45)));
  ASSERT_TRUE(
      db_->AddPlayTime(scenario1, 3, make_details(ShotType::TypeCase::kClickMulti, false, 45)));
  ASSERT_TRUE(
      db_->AddPlayTime(scenario2, 4, make_details(ShotType::TypeCase::kClickSingle, false, 45)));
  ASSERT_TRUE(
      db_->AddPlayTime(scenario2, 60, make_details(ShotType::TypeCase::kTrackingKill, true, 55)));

  playtime = db_->GetTotalPlaytime();
  EXPECT_THAT(playtime.total, EqualsPlayTimes(76, 7));

  EXPECT_THAT(playtime.play_times_by_shot_type[ShotType::TypeCase::kClickMulti],
              EqualsPlayTimes(16, 3));
  EXPECT_THAT(playtime.play_times_by_shot_type[ShotType::TypeCase::kTrackingKill],
              EqualsPlayTimes(60, 0));
  EXPECT_THAT(playtime.play_times_by_shot_type[ShotType::TypeCase::kClickSingle],
              EqualsPlayTimes(0, 4));

  EXPECT_THAT(playtime.play_times_by_cm_per_360[35], EqualsPlayTimes(14, 0));
  EXPECT_THAT(playtime.play_times_by_cm_per_360[45], EqualsPlayTimes(2, 7));
  EXPECT_THAT(playtime.play_times_by_cm_per_360[55], EqualsPlayTimes(60, 0));
}

TEST_F(AimDbTest, RecentViews) {
  db_->UpdateRecentView(ObjectType::SCENARIO, "s1");
  db_->UpdateRecentView(ObjectType::SCENARIO, "s2");
  db_->UpdateRecentView(ObjectType::CROSSHAIR, "c1");
  db_->UpdateRecentView(ObjectType::CROSSHAIR, "c2");
  db_->UpdateRecentView(ObjectType::GUIDE, "g1");
  db_->UpdateRecentView(ObjectType::GUIDE, "g2");

  EXPECT_THAT(db_->GetRecentViews(ObjectType::PLAYLIST, 10), IsEmpty());
  EXPECT_THAT(db_->GetRecentViews(ObjectType::SCENARIO, 10),
              ElementsAre(EqualsViewName("s2"), EqualsViewName("s1")));
  EXPECT_THAT(db_->GetRecentViews(ObjectType::CROSSHAIR, 10),
              ElementsAre(EqualsViewName("c2"), EqualsViewName("c1")));

  db_->UpdateRecentView(ObjectType::CROSSHAIR, "c3");
  db_->UpdateRecentView(ObjectType::CROSSHAIR, "c1");

  EXPECT_THAT(db_->GetRecentViews(ObjectType::CROSSHAIR, 10),
              ElementsAre(EqualsViewName("c1"), EqualsViewName("c3"), EqualsViewName("c2")));
  EXPECT_THAT(db_->GetRecentViews(ObjectType::CROSSHAIR, 3),
              ElementsAre(EqualsViewName("c1"), EqualsViewName("c3"), EqualsViewName("c2")));
  EXPECT_THAT(db_->GetRecentViews(ObjectType::CROSSHAIR, 2),
              ElementsAre(EqualsViewName("c1"), EqualsViewName("c3")));

  db_->DeleteRecentView(ObjectType::CROSSHAIR, "c2");
  EXPECT_THAT(db_->GetRecentViews(ObjectType::CROSSHAIR, 10),
              ElementsAre(EqualsViewName("c1"), EqualsViewName("c3")));

  db_->DeleteRecentView(ObjectType::SCENARIO, "s2");
  EXPECT_THAT(db_->GetRecentViews(ObjectType::SCENARIO, 10), ElementsAre(EqualsViewName("s1")));

  EXPECT_THAT(db_->GetRecentViews(ObjectType::GUIDE, 10),
              ElementsAre(EqualsViewName("g2"), EqualsViewName("g1")));
  db_->UpdateRecentView(ObjectType::GUIDE, "g3");
  db_->UpdateRecentView(ObjectType::GUIDE, "g1");
  EXPECT_THAT(db_->GetRecentViews(ObjectType::GUIDE, 10),
              ElementsAre(EqualsViewName("g1"), EqualsViewName("g3"), EqualsViewName("g2")));

  db_->DeleteRecentView(ObjectType::GUIDE, "g3");
  EXPECT_THAT(db_->GetRecentViews(ObjectType::GUIDE, 10),
              ElementsAre(EqualsViewName("g1"), EqualsViewName("g2")));

  EXPECT_THAT(db_->GetRecentViews(ObjectType::GUIDE, 1), ElementsAre(EqualsViewName("g1")));
}

TEST_F(AimDbTest, LabeledItems) {
  EXPECT_THAT(db_->GetLabeledItems(1, ObjectType::SCENARIO), IsEmpty());

  i64 s1 = db_->GetScenarioId("Scenario1");
  i64 s2 = db_->GetScenarioId("Scenario2");
  i64 s3 = db_->GetScenarioId("Scenario3");
  i64 s4 = db_->GetScenarioId("Scenario4");
  i64 s5 = db_->GetScenarioId("Scenario5");

  i64 p1 = db_->GetPlaylistId("Playlist1");
  i64 p2 = db_->GetPlaylistId("Playlist2");
  i64 p3 = db_->GetPlaylistId("Playlist3");
  i64 p4 = db_->GetPlaylistId("Playlist4");
  i64 p5 = db_->GetPlaylistId("Playlist5");

  db_->AddLabeledItem(1, ObjectType::SCENARIO, s1);
  EXPECT_THAT(db_->GetLabeledItems(1, ObjectType::SCENARIO), UnorderedElementsAre("Scenario1"));

  db_->AddLabeledItem(1, ObjectType::SCENARIO, s2);
  db_->AddLabeledItem(1, ObjectType::SCENARIO, s3);

  EXPECT_THAT(db_->GetLabeledItems(1, ObjectType::SCENARIO),
              UnorderedElementsAre("Scenario1", "Scenario2", "Scenario3"));

  db_->AddLabeledItem(2, ObjectType::SCENARIO, s4);
  db_->AddLabeledItem(2, ObjectType::SCENARIO, 456456456);
  db_->AddLabeledItem(2, ObjectType::SCENARIO, s5);
  db_->AddLabeledItem(2, ObjectType::PLAYLIST, p4);

  EXPECT_THAT(db_->GetLabeledItems(2, ObjectType::SCENARIO),
              UnorderedElementsAre("Scenario4", "Scenario5"));
  EXPECT_THAT(db_->GetLabeledItems(2, ObjectType::PLAYLIST), UnorderedElementsAre("Playlist4"));
  EXPECT_THAT(db_->GetLabeledItems(1, ObjectType::SCENARIO),
              UnorderedElementsAre("Scenario1", "Scenario2", "Scenario3"));

  db_->RemoveLabeledItem(2, ObjectType::SCENARIO, s5);
  EXPECT_THAT(db_->GetLabeledItems(2, ObjectType::SCENARIO), UnorderedElementsAre("Scenario4"));
}

TEST_F(AimDbTest, TestRenameScenario) {
  i64 s1 = db_->GetScenarioId("s1");
  i64 s2 = db_->GetScenarioId("s2");

  db_->UpdateRecentView(ObjectType::SCENARIO, "s1");
  db_->UpdateRecentView(ObjectType::SCENARIO, "s2");

  db_->UpdateRecentView(ObjectType::CROSSHAIR, "c1");
  db_->UpdateRecentView(ObjectType::CROSSHAIR, "c2");

  // Should not change a playlist
  db_->UpdateRecentView(ObjectType::PLAYLIST, "s1");

  db_->RenameScenario("s1", "s1_new");

  EXPECT_THAT(db_->GetRecentViews(ObjectType::SCENARIO, 10),
              ElementsAre(EqualsViewName("s2"), EqualsViewName("s1_new")));
  EXPECT_THAT(db_->GetRecentViews(ObjectType::PLAYLIST, 10), ElementsAre(EqualsViewName("s1")));
  EXPECT_THAT(db_->GetScenarioId("s1_new"), Eq(s1));
  EXPECT_THAT(db_->GetScenarioId("s2"), Eq(s2));
  EXPECT_THAT(db_->GetScenarioId("s1"), Ne(s1));

  EXPECT_THAT(db_->GetRecentViews(ObjectType::CROSSHAIR, 10),
              ElementsAre(EqualsViewName("c2"), EqualsViewName("c1")));
}

TEST_F(AimDbTest, TestRenameScenario_WithDynamicSuffixes) {
  i64 s1 = db_->GetScenarioId("s1");
  i64 s1_25 = db_->GetScenarioId("s1 25cm");
  i64 s2 = db_->GetScenarioId("s2");
  db_->UpdateRecentView(ObjectType::SCENARIO, "s1");
  db_->UpdateRecentView(ObjectType::SCENARIO, "s1 25cm");
  db_->UpdateRecentView(ObjectType::SCENARIO, "s1 35cm");
  db_->UpdateRecentView(ObjectType::SCENARIO, "s2");

  db_->RenameScenario("s1", "s1_new");

  EXPECT_THAT(db_->GetRecentViews(ObjectType::SCENARIO, 10),
              ElementsAre(EqualsViewName("s2"),
                          EqualsViewName("s1_new 35cm"),
                          EqualsViewName("s1_new 25cm"),
                          EqualsViewName("s1_new")));

  EXPECT_THAT(db_->GetScenarioId("s1_new"), Eq(s1));
  EXPECT_THAT(db_->GetScenarioId("s1_new 25cm"), Eq(s1_25));
  EXPECT_THAT(db_->GetScenarioId("s2"), Eq(s2));
  EXPECT_THAT(db_->GetScenarioId("s1"), Ne(s1));
  EXPECT_THAT(db_->GetScenarioId("s1 25cm"), Ne(s1_25));
}
