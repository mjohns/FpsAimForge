#include "aim_db.h"

#include <cassert>
#include <format>
#include <string>

#include "aim/common/log.h"
#include "aim/common/name_util.h"
#include "aim/common/times.h"
#include "aim/database/sqlite_util.h"
#include "sqlite3.h"

namespace aim {
namespace {

const char* kCreatePlaylistsTable = R"AIMS(
CREATE TABLE IF NOT EXISTS Playlists (
    PlaylistId INTEGER PRIMARY KEY AUTOINCREMENT,
    PlaylistName TEXT NOT NULL,
    Info BLOB
);
)AIMS";

const char* kCreatePlaylistsByNameIndex = R"AIMS(
CREATE UNIQUE INDEX IF NOT EXISTS PlaylistsByName ON Playlists(PlaylistName);
)AIMS";

const char* kCreatePlaylistSql = R"AIMS(
INSERT INTO Playlists (PlaylistId, PlaylistName) VALUES (NULL, ?);
)AIMS";

const char* kGetAllPlaylistIdsSql = R"AIMS(
SELECT PlaylistId, PlaylistName FROM Playlists;
)AIMS";

const char* kGetPlaylistIdSql = R"AIMS(
SELECT PlaylistId FROM Playlists
INDEXED BY PlaylistsByName
WHERE PlaylistName = ?;
)AIMS";

const char* kUpdatePlaylistNameSql = R"AIMS(
UPDATE Playlists SET PlaylistName = ? WHERE PlaylistId = ?;
)AIMS";

const char* kCreateGuidesTable = R"AIMS(
CREATE TABLE IF NOT EXISTS Guides (
    GuideId INTEGER PRIMARY KEY AUTOINCREMENT,
    GuideName TEXT NOT NULL,
    Info BLOB
);
)AIMS";

const char* kCreateGuidesByNameIndex = R"AIMS(
CREATE UNIQUE INDEX IF NOT EXISTS GuidesByName ON Guides(GuideName);
)AIMS";

const char* kCreateGuideSql = R"AIMS(
INSERT INTO Guides (GuideId, GuideName) VALUES (NULL, ?);
)AIMS";

const char* kGetAllGuideIdsSql = R"AIMS(
SELECT GuideId, GuideName FROM Guides;
)AIMS";

const char* kGetGuideIdSql = R"AIMS(
SELECT GuideId FROM Guides
INDEXED BY GuidesByName
WHERE GuideName = ?;
)AIMS";

const char* kUpdateGuideNameSql = R"AIMS(
UPDATE Guides SET GuideName = ? WHERE GuideId = ?;
)AIMS";

const char* kCreateScenariosTable = R"AIMS(
CREATE TABLE IF NOT EXISTS Scenarios (
    ScenarioId INTEGER PRIMARY KEY AUTOINCREMENT,
    ScenarioName TEXT NOT NULL,
    Settings BLOB,
    Info BLOB
);
)AIMS";

const char* kCreateScenarioSql = R"AIMS(
INSERT INTO Scenarios (ScenarioId, ScenarioName) VALUES (NULL, ?);
)AIMS";

const char* kGetScenarioIdSql = R"AIMS(
SELECT ScenarioId FROM Scenarios
INDEXED BY ScenariosByName
WHERE ScenarioName = ?;
)AIMS";

const char* kUpdateScenarioSettingsSql = R"AIMS(
UPDATE Scenarios SET Settings = ? WHERE ScenarioId = ?;
)AIMS";

const char* kUpdateScenarioNameSql = R"AIMS(
UPDATE Scenarios SET ScenarioName = ? WHERE ScenarioId = ?;
)AIMS";

const char* kGetAllScenarioIdsSql = R"AIMS(
SELECT ScenarioId, ScenarioName FROM Scenarios;
)AIMS";

const char* kGetScenarioNameSql = R"AIMS(
SELECT ScenarioName FROM Scenarios where ScenarioId = ?;
)AIMS";

const char* kGetScenarioNamesWithPrefixSql = R"AIMS(
SELECT ScenarioName FROM Scenarios
WHERE ScenarioName LIKE ?;
)AIMS";

const char* kGetPlaylistNamesWithPrefixSql = R"AIMS(
SELECT PlaylistName FROM Playlists
WHERE PlaylistName LIKE ?;
)AIMS";

const char* kGetGuideNamesWithPrefixSql = R"AIMS(
SELECT GuideName FROM Guides
WHERE GuideName LIKE ?;
)AIMS";

const char* kGetScenarioSettingsSql = R"AIMS(
SELECT Settings FROM Scenarios WHERE ScenarioId = ?;
)AIMS";

const char* kCreateScenariosByNameIndex = R"AIMS(
CREATE UNIQUE INDEX IF NOT EXISTS ScenariosByName ON Scenarios(ScenarioName);
)AIMS";

const char* kCreateStatsTable = R"AIMS(
CREATE TABLE IF NOT EXISTS Stats (
    ScenarioId INTEGER PRIMARY_KEY,
    StatsId INTEGER PRIMARY KEY AUTOINCREMENT,
    TimestampSeconds INTEGER NOT NULL,
    Score REAL NOT NULL,
    MmPer360 INTEGER NOT NULL,
    Info BLOB,
    FOREIGN KEY (ScenarioId) REFERENCES Scenarios(ScenarioId)
);
)AIMS";

const char* kAddStatsSql = R"AIMS(
INSERT INTO Stats (
  StatsId,
  ScenarioId,
  TimestampSeconds,
  Score,
  MmPer360,
  Info)
VALUES (NULL, ?, ?, ?, ?, ?);
)AIMS";

const char* kGetStatsSql = R"AIMS(
SELECT
  StatsId,
  TimestampSeconds,
  Score,
  MmPer360,
  Info
FROM Stats
WHERE ScenarioId = ?
ORDER BY StatsId ASC;
)AIMS";

const char* kGetMostRecentStatsRunIdSql = R"AIMS(
SELECT StatsId
FROM Stats
WHERE ScenarioId = ?
ORDER BY StatsId DESC LIMIT 1;
)AIMS";

const char* kDeleteStatsRunSql = R"AIMS(
DELETE FROM Stats WHERE ScenarioId = ? AND StatsId = ?;
)AIMS";

const char* kDeleteAllStatsForScenarioSql = R"AIMS(
DELETE FROM Stats WHERE ScenarioId = ?;
)AIMS";

// Used to apply labels like "starred"
const char* kCreateLabeledItemsTable = R"AIMS(
CREATE TABLE IF NOT EXISTS LabeledItems (
    Label INTEGER NOT NULL,
    Type INTEGER NOT NULL,
    Id INTEGER NOT NULL,
    PRIMARY KEY (Label, Type, Id)
);
)AIMS";

const char* kInsertLabeledItemSql = R"AIMS(
INSERT OR REPLACE INTO LabeledItems (Label, Type, Id) VALUES ( ?, ?, ?)
ON CONFLICT DO NOTHING;
)AIMS";

const char* kDeleteLabeledItemSql = R"AIMS(
DELETE FROM LabeledItems WHERE Label = ? AND Type = ? AND Id = ?;
)AIMS";

const char* kGetLabeledScenarioItemsSql = R"AIMS(
SELECT Scenarios.ScenarioName
FROM LabeledItems
JOIN Scenarios ON LabeledItems.Id = Scenarios.ScenarioId
WHERE LabeledItems.Label = ? AND LabeledItems.Type = ?;
)AIMS";

const char* kGetLabeledPlaylistItemsSql = R"AIMS(
SELECT Playlists.PlaylistName
FROM LabeledItems
JOIN Playlists ON LabeledItems.Id = Playlists.PlaylistId
WHERE LabeledItems.Label = ? AND LabeledItems.Type = ?;
)AIMS";

const char* kGetLabeledGuideItemsSql = R"AIMS(
SELECT Guides.GuideName
FROM LabeledItems
JOIN Guides ON LabeledItems.Id = Guides.GuideId
WHERE LabeledItems.Label = ? AND LabeledItems.Type = ?;
)AIMS";

const char* kCreatePlayTimeTable = R"AIMS(
CREATE TABLE IF NOT EXISTS PlayTime (
    PlayTimeId INTEGER PRIMARY KEY AUTOINCREMENT,
    TimestampMinutes INTEGER,
    DurationSeconds INTEGER,
    ScenarioId INTEGER,
    CmPer360 INTEGER,
    IsCompleteRun INTEGER,
    ShotType INTEGER
);
)AIMS";

const char* kAddPlayTimeSql = R"AIMS(
INSERT INTO PlayTime (
	  PlayTimeId,
    TimestampMinutes,
    DurationSeconds,
    ScenarioId,
    CmPer360,
    IsCompleteRun,
    ShotType)
VALUES (NULL, ?, ?, ?, ?, ?, ?);
)AIMS";

const char* kGetPlayTimeByShotTypeSql = R"AIMS(
SELECT ShotType, IsCompleteRun, SUM(DurationSeconds)
FROM PlayTime
GROUP BY 1,2; 
)AIMS";

const char* kGetPlayTimeByCmPer360Sql = R"AIMS(
SELECT CmPer360, IsCompleteRun, SUM(DurationSeconds)
FROM PlayTime
GROUP BY 1,2; 
)AIMS";

const char* kCreateRecentViewsTable = R"AIMS(
CREATE TABLE IF NOT EXISTS RecentViews (
    Type INTEGER,
    Name TEXT,
    TimestampMicros INTEGER,
    PRIMARY KEY (Type, Name)
);
)AIMS";

// RecentViews for Scenarios/Playlists using their stable internal integer id.
const char* kCreateRecentIdViewsTable = R"AIMS(
CREATE TABLE IF NOT EXISTS RecentIdViews (
    Type INTEGER,
    Id INTEGER,
    TimestampMicros INTEGER,
    PRIMARY KEY (Type, Id)
);
)AIMS";

const char* kInsertRecentViewsSql = R"AIMS(
INSERT INTO RecentViews (Type, Name, TimestampMicros) VALUES (?, ?, ?)
ON CONFLICT (Type, Name) DO UPDATE SET TimestampMicros = ?;
)AIMS";

const char* kInsertRecentIdViewsSql = R"AIMS(
INSERT INTO RecentIdViews (Type, Id, TimestampMicros) VALUES (?, ?, ?)
ON CONFLICT (Type, Id) DO UPDATE SET TimestampMicros = ?;
)AIMS";

const char* kDeleteRecentViewSql = R"AIMS(
DELETE FROM RecentViews WHERE Type = ? AND Name = ?;
)AIMS";

const char* kDeleteRecentIdViewSql = R"AIMS(
DELETE FROM RecentIdViews WHERE Type = ? AND Id = ?;
)AIMS";

const char* kGetRecentViewsForTypeSql = R"AIMS(
SELECT Name, TimestampMicros
FROM RecentViews
WHERE Type = ?
ORDER BY TimestampMicros DESC
LIMIT ?;
)AIMS";

const char* kGetRecentViewsForScenarioSql = R"AIMS(
SELECT Scenarios.ScenarioName, RecentIdViews.TimestampMicros
FROM RecentIdViews
JOIN Scenarios ON RecentIdViews.Id = Scenarios.ScenarioId
WHERE RecentIdViews.Type = ?
ORDER BY RecentIdViews.TimestampMicros DESC
LIMIT ?;
)AIMS";

const char* kGetRecentViewsForPlaylistSql = R"AIMS(
SELECT Playlists.PlaylistName, RecentIdViews.TimestampMicros
FROM RecentIdViews
JOIN Playlists ON RecentIdViews.Id = Playlists.PlaylistId
WHERE RecentIdViews.Type = ?
ORDER BY RecentIdViews.TimestampMicros DESC
LIMIT ?;
)AIMS";

const char* kGetRecentViewsForGuideSql = R"AIMS(
SELECT Guides.GuideName, RecentIdViews.TimestampMicros
FROM RecentIdViews
JOIN Guides ON RecentIdViews.Id = Guides.GuideId
WHERE RecentIdViews.Type = ?
ORDER BY RecentIdViews.TimestampMicros DESC
LIMIT ?;
)AIMS";

class AimDbImpl : public AimDb {
 public:
  explicit AimDbImpl(const std::filesystem::path& db_path) {
    char* err_msg = 0;
    std::string db_path_str = db_path.string();
    int rc = sqlite3_open(db_path_str.c_str(), &db_);

    if (rc != SQLITE_OK) {
      Logger::get()->warn("Cannot open aim db: {}", sqlite3_errmsg(db_));
      initialization_error_ = std::format("Cannot open AimDb at path \"{}\".\nsqlite_error: {}",
                                          db_path.string(),
                                          sqlite3_errmsg(db_));
      sqlite3_close(db_);
      db_ = nullptr;
      return;
    }

    std::string error_message;
    if (!ExecuteSqliteQuery(db_, kCreateScenariosTable, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreateScenariosByNameIndex, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreatePlaylistsTable, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreatePlaylistsByNameIndex, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreateGuidesTable, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreateGuidesByNameIndex, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreateStatsTable, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreatePlayTimeTable, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreateRecentViewsTable, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreateRecentIdViewsTable, &error_message)) {
      initialization_error_ = error_message;
    }
    if (!ExecuteSqliteQuery(db_, kCreateLabeledItemsTable, &error_message)) {
      initialization_error_ = error_message;
    }
  }

  std::optional<std::string> GetInitializationError() override {
    if (initialization_error_) {
      return initialization_error_;
    }
    if (db_ == nullptr) {
      return "Unable to create AimDb.";
    }
    return {};
  }

  i64 CreateIdEntry(const std::string& name, const char* create_entry_sql) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, create_entry_sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return -1;
    }

    BindString(stmt, 1, name);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(db_);
  }

  std::unordered_map<std::string, i64> GetPlaylistIdMap() override {
    return GetNameToIdMap(kGetAllPlaylistIdsSql);
  }

  i64 GetId(const std::string& name,
            std::unordered_map<std::string, i64>& partial_id_map,
            const char* get_id_sql,
            const char* create_id_sql) {
    auto it = partial_id_map.find(name);
    if (it != partial_id_map.end()) {
      return it->second;
    }
    auto existing_entry = GetExistingIdFromDb(name, get_id_sql);
    if (existing_entry) {
      partial_id_map[name] = *existing_entry;
      return *existing_entry;
    }
    i64 id = CreateIdEntry(name, create_id_sql);
    partial_id_map[name] = id;
    return id;
  }

  i64 GetPlaylistId(const std::string& name) override {
    return GetId(name, partial_playlist_id_map_, kGetPlaylistIdSql, kCreatePlaylistSql);
  }

  void RenamePlaylist(const std::string& old_name, const std::string& new_name) override {
    NameInfo old_info = GetNameInfo(old_name);
    NameInfo new_info = GetNameInfo(new_name);
    if (old_info.HasDynamicSuffix() || new_info.HasDynamicSuffix()) {
      // The base playlists should be renamed and not the dynamic variations. This determination
      // should be handled at the application layer.
      assert(false && "Renaming non base playlists");
      return;
    }

    std::vector<std::string> candidate_names = GetPlaylistNamesWithPrefix(old_name);
    for (const std::string& candidate_name : candidate_names) {
      NameInfo info = GetNameInfo(candidate_name);
      if (info.base_name == old_name && info.HasDynamicSuffix()) {
        info.base_name = new_name;
        RenameSinglePlaylist(candidate_name, info.GetFullName());
      }
    }

    RenameSinglePlaylist(old_name, new_name);
  }

  i64 RenameSinglePlaylist(const std::string& old_name, const std::string& new_name) {
    i64 existing_id = GetPlaylistId(old_name);

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kUpdatePlaylistNameSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return existing_id;
    }

    BindString(stmt, 1, new_name);

    sqlite3_bind_int64(stmt, 2, existing_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    partial_playlist_id_map_.erase(old_name);
    partial_playlist_id_map_[new_name] = existing_id;
    return existing_id;
  }

  std::unordered_map<std::string, i64> GetGuideIdMap() override {
    return GetNameToIdMap(kGetAllGuideIdsSql);
  }

  i64 GetGuideId(const std::string& name) override {
    return GetId(name, partial_guide_id_map_, kGetGuideIdSql, kCreateGuideSql);
  }

  void RenameGuide(const std::string& old_name, const std::string& new_name) override {
    i64 existing_id = GetGuideId(old_name);

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kUpdateGuideNameSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return;
    }

    BindString(stmt, 1, new_name);

    sqlite3_bind_int64(stmt, 2, existing_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    partial_guide_id_map_.erase(old_name);
    partial_guide_id_map_[new_name] = existing_id;
  }

  i64 GetScenarioId(const std::string& name) override {
    auto it = partial_scenario_id_map_.find(name);
    if (it != partial_scenario_id_map_.end()) {
      return it->second;
    }
    auto existing_entry = GetExistingIdFromDb(name, kGetScenarioIdSql);
    if (existing_entry) {
      partial_scenario_id_map_[name] = *existing_entry;
      return *existing_entry;
    }
    i64 scenario_id = CreateScenarioEntry(name);
    partial_scenario_id_map_[name] = scenario_id;
    return scenario_id;
  }

  i64 CreateScenarioEntry(const std::string& name) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kCreateScenarioSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return -1;
    }

    // This byte string needs to stay around until after step is done.
    std::string settings_content;

    BindString(stmt, 1, name);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(db_);
  }

  void RenameScenario(const std::string& old_name, const std::string& new_name) override {
    NameInfo old_info = GetNameInfo(old_name);
    NameInfo new_info = GetNameInfo(new_name);
    if (old_info.HasDynamicSuffix() || new_info.HasDynamicSuffix()) {
      // The base scenarios should be renamed and not the dynamic variations. This determination
      // should be handled at the application layer.
      assert(false && "Renaming non base scenarios");
      return;
    }

    std::vector<std::string> candidate_names = GetScenarioNamesWithPrefix(old_name);
    for (const std::string& candidate_name : candidate_names) {
      NameInfo info = GetNameInfo(candidate_name);
      if (info.base_name == old_name && info.HasDynamicSuffix()) {
        info.base_name = new_name;
        RenameSingleScenario(candidate_name, info.GetFullName());
      }
    }

    RenameSingleScenario(old_name, new_name);
  }

  std::vector<std::string> GetScenarioNamesWithPrefix(const std::string& prefix) override {
    return GetNamesWithPrefix(prefix, kGetScenarioNamesWithPrefixSql);
  }

  std::vector<std::string> GetGuideNamesWithPrefix(const std::string& prefix) override {
    return GetNamesWithPrefix(prefix, kGetGuideNamesWithPrefixSql);
  }

  std::vector<std::string> GetPlaylistNamesWithPrefix(const std::string& prefix) override {
    return GetNamesWithPrefix(prefix, kGetPlaylistNamesWithPrefixSql);
  }

  std::vector<std::string> GetNamesWithPrefix(const std::string& prefix, const char* sql) {
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }

    std::string like_value = std::format("{}%", prefix);
    BindString(stmt, 1, like_value);

    std::vector<std::string> names;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      names.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }

    sqlite3_finalize(stmt);
    return names;
  }

  void RenameSingleScenario(const std::string& old_name, const std::string& new_name) {
    i64 existing_id = GetScenarioId(old_name);

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kUpdateScenarioNameSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return;
    }

    BindString(stmt, 1, new_name);

    sqlite3_bind_int64(stmt, 2, existing_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    partial_scenario_id_map_.erase(old_name);
    partial_scenario_id_map_[new_name] = existing_id;
  }

  void UpdateScenarioSettings(i64 scenario_id, ScenarioSettings settings) override {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kUpdateScenarioSettingsSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return;
    }

    std::string settings_content = settings.SerializeAsString();
    sqlite3_bind_blob(stmt, 1, settings_content.data(), settings_content.size(), SQLITE_STATIC);

    sqlite3_bind_int64(stmt, 2, scenario_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  ScenarioSettings GetScenarioSettings(i64 scenario_id) override {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kGetScenarioSettingsSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }

    sqlite3_bind_int64(stmt, 1, scenario_id);

    ScenarioSettings settings;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      if (!IsColumnNull(stmt, 0)) {
        const void* blob_data = sqlite3_column_blob(stmt, 0);
        int blob_size = sqlite3_column_bytes(stmt, 0);
        settings.ParseFromArray(blob_data, blob_size);
      }
    }

    sqlite3_finalize(stmt);
    return settings;
  }

  std::unordered_map<std::string, i64> GetScenarioIdMap() override {
    return GetNameToIdMap(kGetAllScenarioIdsSql);
  }

  std::string GetScenarioName(i64 scenario_id) override {
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, kGetScenarioNameSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }
    sqlite3_bind_int64(stmt, 1, scenario_id);

    std::string name;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return name;
  }

  bool AddStats(i64 scenario_id, StatsDbRow* row) override {
    if (row->epoch_seconds < 0) {
      row->epoch_seconds = GetNowEpochSeconds();
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kAddStatsSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return false;
    }

    sqlite3_bind_int64(stmt, 1, scenario_id);
    sqlite3_bind_int64(stmt, 2, row->epoch_seconds);
    sqlite3_bind_double(stmt, 3, row->score);
    sqlite3_bind_int(stmt, 4, row->mm_per_360);

    std::string info_content = row->info.SerializeAsString();
    sqlite3_bind_blob(stmt, 5, info_content.data(), info_content.size(), SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    row->stats_id = sqlite3_last_insert_rowid(db_);

    return true;
  }

  std::vector<StatsDbRow> GetStats(i64 scenario_id) override {
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, kGetStatsSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }
    sqlite3_bind_int64(stmt, 1, scenario_id);

    std::vector<StatsDbRow> all_stats;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      all_stats.push_back({});
      StatsDbRow& stats = all_stats.back();

      stats.stats_id = sqlite3_column_int64(stmt, 0);
      stats.epoch_seconds = sqlite3_column_int64(stmt, 1);
      stats.score = sqlite3_column_double(stmt, 2);
      stats.mm_per_360 = sqlite3_column_int(stmt, 3);
      if (!IsColumnNull(stmt, 4)) {
        const void* blob_data = sqlite3_column_blob(stmt, 4);
        int blob_size = sqlite3_column_bytes(stmt, 4);
        stats.info.ParseFromArray(blob_data, blob_size);
      }
    }

    sqlite3_finalize(stmt);
    return all_stats;
  }

  i64 GetLatestStatsId(i64 scenario_id) override {
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, kGetMostRecentStatsRunIdSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }
    sqlite3_bind_int64(stmt, 1, scenario_id);

    i64 run_id = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      run_id = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return run_id;
  }

  void CopyAllStats(i64 from_scenario_id, i64 to_scenario_id) override {}

  void DeleteStats(i64 scenario_id, i64 stats_run_id) override {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kDeleteStatsRunSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return;
    }
    sqlite3_bind_int64(stmt, 1, scenario_id);
    sqlite3_bind_int64(stmt, 2, stats_run_id);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
      Logger::get()->warn(
          "Failed to delete stats for {} {}: {}", scenario_id, stats_run_id, sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
  }

  void DeleteAllStats(i64 scenario_id) override {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kDeleteAllStatsForScenarioSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return;
    }
    sqlite3_bind_int64(stmt, 1, scenario_id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      Logger::get()->warn("Failed to delete stats for {}: {}", scenario_id, sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
  }

  bool AddPlayTime(i64 scenario_id,
                   float duration_seconds,
                   const PlayTimeDetails& details) override {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kAddPlayTimeSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return false;
    }

    sqlite3_bind_int64(stmt, 1, GetNowEpochMinutes());

    sqlite3_bind_int(stmt, 2, (int)std::round(duration_seconds));
    sqlite3_bind_int64(stmt, 3, scenario_id);
    sqlite3_bind_int(stmt, 4, (int)std::round(details.cm_per_360));
    sqlite3_bind_int(stmt, 5, details.is_complete_run);
    sqlite3_bind_int(stmt, 6, details.shot_type);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return true;
  }

  TotalPlaytime GetTotalPlaytime() override {
    TotalPlaytime result;
    result.play_times_by_shot_type = GetPlayTimeByShotType();
    result.play_times_by_cm_per_360 = GetPlayTimeByIntKey(kGetPlayTimeByCmPer360Sql);
    auto time_by_type = GetPlayTimeByIntKey(kGetPlayTimeByShotTypeSql);
    for (auto& entry : result.play_times_by_shot_type) {
      result.total.complete_run_time_seconds += entry.second.complete_run_time_seconds;
      result.total.partial_run_time_seconds += entry.second.partial_run_time_seconds;
    }
    return result;
  }

  std::unordered_map<ShotType::TypeCase, PlayTimes> GetPlayTimeByShotType() {
    auto time_by_type = GetPlayTimeByIntKey(kGetPlayTimeByShotTypeSql);

    std::unordered_map<ShotType::TypeCase, PlayTimes> result;
    for (auto& entry : time_by_type) {
      ShotType::TypeCase shot_type = static_cast<ShotType::TypeCase>(entry.first);
      result[shot_type] = entry.second;
    }
    return result;
  }

  std::unordered_map<int, PlayTimes> GetPlayTimeByIntKey(const char* querySql) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, querySql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }

    std::unordered_map<int, PlayTimes> play_time_by_type;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int type = sqlite3_column_int(stmt, 0);
      auto& times = play_time_by_type[type];
      bool is_complete_run = sqlite3_column_int64(stmt, 1);
      int duration = sqlite3_column_int(stmt, 2);
      if (is_complete_run) {
        times.complete_run_time_seconds += duration;
      } else {
        times.partial_run_time_seconds += duration;
      }
    }
    sqlite3_finalize(stmt);
    return play_time_by_type;
  }

  void UpdateRecentView(ObjectType type, const std::string& name) override {
    if (type == ObjectType::SCENARIO) {
      return UpdateRecentIdView(type, GetScenarioId(name));
    }
    if (type == ObjectType::PLAYLIST) {
      return UpdateRecentIdView(type, GetPlaylistId(name));
    }
    if (type == ObjectType::GUIDE) {
      return UpdateRecentIdView(type, GetGuideId(name));
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kInsertRecentViewsSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return;
    }

    i64 now_micros = GetNowEpochMicros();

    sqlite3_bind_int(stmt, 1, (int)type);
    BindString(stmt, 2, name);
    sqlite3_bind_int64(stmt, 3, now_micros);
    sqlite3_bind_int64(stmt, 4, now_micros);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  void UpdateRecentIdView(ObjectType type, i64 id) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kInsertRecentIdViewsSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return;
    }

    i64 now_micros = GetNowEpochMicros();

    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_int64(stmt, 2, id);
    sqlite3_bind_int64(stmt, 3, now_micros);
    sqlite3_bind_int64(stmt, 4, now_micros);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  void DeleteRecentView(ObjectType type, const std::string& name) override {
    if (type == ObjectType::SCENARIO) {
      return DeleteRecentIdView(type, GetScenarioId(name));
    }
    if (type == ObjectType::PLAYLIST) {
      return DeleteRecentIdView(type, GetPlaylistId(name));
    }
    if (type == ObjectType::GUIDE) {
      return DeleteRecentIdView(type, GetGuideId(name));
    }

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kDeleteRecentViewSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return;
    }

    sqlite3_bind_int(stmt, 1, (int)type);
    BindString(stmt, 2, name);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  void DeleteRecentIdView(ObjectType type, i64 id) {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kDeleteRecentIdViewSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return;
    }

    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_int64(stmt, 2, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  std::vector<RecentViewV2> GetRecentViews(ObjectType t, int limit) override {
    const char* sql = kGetRecentViewsForTypeSql;
    if (t == ObjectType::SCENARIO) {
      sql = kGetRecentViewsForScenarioSql;
    } else if (t == ObjectType::PLAYLIST) {
      sql = kGetRecentViewsForPlaylistSql;
    } else if (t == ObjectType::GUIDE) {
      sql = kGetRecentViewsForGuideSql;
    }
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }

    sqlite3_bind_int(stmt, 1, (int)t);
    sqlite3_bind_int(stmt, 2, limit);

    std::vector<RecentViewV2> views;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      views.push_back({});
      RecentViewV2& view = views.back();
      view.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      view.view_time_micros = sqlite3_column_int64(stmt, 1);
    }

    sqlite3_finalize(stmt);
    return views;
  }

  void AddLabeledItem(int label, ObjectType type, i64 object_id) override {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kInsertLabeledItemSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to prepare statement: {}", sqlite3_errmsg(db_));
      return;
    }

    sqlite3_bind_int(stmt, 1, label);
    sqlite3_bind_int(stmt, 2, (int)type);
    sqlite3_bind_int(stmt, 3, object_id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  void RemoveLabeledItem(int label, ObjectType type, i64 object_id) override {
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, kDeleteLabeledItemSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return;
    }
    sqlite3_bind_int(stmt, 1, label);
    sqlite3_bind_int(stmt, 2, (int)type);
    sqlite3_bind_int64(stmt, 3, object_id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
      Logger::get()->warn("Failed to delete labeled item for {}: {}", label, sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
  }

  std::vector<std::string> GetLabeledItems(int label, ObjectType type) override {
    sqlite3_stmt* stmt;

    const char* query = kGetLabeledScenarioItemsSql;
    if (type == ObjectType::PLAYLIST) {
      query = kGetLabeledPlaylistItemsSql;
    } else if (type == ObjectType::GUIDE) {
      query = kGetLabeledGuideItemsSql;
    }

    int rc = sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }
    sqlite3_bind_int(stmt, 1, label);
    sqlite3_bind_int(stmt, 2, (int)type);

    std::vector<std::string> ids;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      std::string id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      ids.push_back(id);
    }

    sqlite3_finalize(stmt);
    return ids;
  }

  ~AimDbImpl() override {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }

 private:
  std::unordered_map<std::string, i64> GetNameToIdMap(const char* sql) {
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }

    std::unordered_map<std::string, i64> name_to_id_map;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      i64 id = sqlite3_column_int64(stmt, 0);
      name_to_id_map[reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))] = id;
    }

    sqlite3_finalize(stmt);
    return name_to_id_map;
  }

  std::optional<i64> GetExistingIdFromDb(const std::string& name, const char* sql) {
    sqlite3_stmt* stmt;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
      Logger::get()->warn("Failed to fetch data: {}", sqlite3_errmsg(db_));
      return {};
    }

    BindString(stmt, 1, name);

    std::optional<i64> id;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      id = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return id;
  }

  std::unordered_map<std::string, i64> partial_scenario_id_map_;
  std::unordered_map<std::string, i64> partial_playlist_id_map_;
  std::unordered_map<std::string, i64> partial_guide_id_map_;
  sqlite3* db_ = nullptr;

  std::optional<std::string> initialization_error_;
};

}  // namespace

std::unique_ptr<AimDb> CreateAimDb(const std::filesystem::path& db_path) {
  return std::make_unique<AimDbImpl>(db_path);
}

}  // namespace aim
