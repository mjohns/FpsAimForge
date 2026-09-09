#include "settings_manager.h"

#include "SDL3/SDL.h"  // IWYU pragma: keep
#include "SDL3/SDL_mouse.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/strip.h"
#include "aim/common/collections.h"
#include "aim/common/files.h"
#include "aim/common/log.h"
#include "aim/common/name_util.h"
#include "aim/common/proto_util.h"
#include "aim/common/util.h"
#include "aim/proto/settings.pb.h"
#include "aim/proto/theme.pb.h"
#include "google/protobuf/json/json.h"
#include "google/protobuf/util/json_util.h"

namespace aim {
namespace {
constexpr const float kDefaultDpi = 800;

std::string GetBaseNameForScenarioSettings(const std::string& full_name) {
  NameInfo name_info = GetScenarioNameInfo(full_name);

  // Clear fields where the settings should be saved for the scenario.
  name_info.level = std::nullopt;
  name_info.cm_per_360 = std::nullopt;
  name_info.duration = std::nullopt;
  return name_info.GetFullName();
}

SoundSettings GetDefaultSoundSettings() {
  SoundSettings sounds;
  sounds.mutable_click_miss()->set_name("miss.ogg");
  sounds.mutable_click_hit()->set_name("hit.ogg");
  sounds.mutable_click_kill()->set_name("kill.ogg");

  sounds.mutable_tracking_hit()->set_name("apa-hit-4.ogg");
  sounds.mutable_tracking_kill()->set_name("kill.ogg");

  sounds.mutable_reload()->set_name("reload.ogg");
  sounds.mutable_metronome()->set_name("metronome.ogg");
  return sounds;
}

Settings GetDefaultSettings() {
  Settings settings;
  settings.set_cm_per_360(45);
  settings.set_current_crosshair_name("Dot");
  settings.set_crosshair_size(15);
  settings.mutable_health_bar()->set_show(true);
  settings.set_theme_name("Marble");

  Keybinds* binds = settings.mutable_keybinds();
  binds->mutable_fire()->set_mapping1("Left Click");
  binds->mutable_restart_scenario()->set_mapping1("R");
  binds->mutable_next_scenario()->set_mapping1("Space");
  binds->mutable_quick_metronome()->set_mapping1("B");
  binds->mutable_adjust_crosshair_size()->set_mapping1("C");
  binds->mutable_quick_settings()->set_mapping1("S");
  binds->mutable_edit_scenario()->set_mapping1("U");

  SoundSettings* sounds = settings.mutable_sounds();
  *sounds = GetDefaultSoundSettings();

  return settings;
}

class SettingsManagerImpl : public SettingsManager {
 public:
  SettingsManagerImpl(const std::filesystem::path& settings_path,
                      const std::filesystem::path& theme_dir,
                      const std::filesystem::path& texture_dir,
                      const std::filesystem::path& crosshair_dir,
                      AimDb* db,
                      HistoryManager* history_manager)
      : settings_path_(settings_path),
        theme_dir_(theme_dir),
        texture_dir_(texture_dir),
        crosshair_dir_(crosshair_dir),
        db_(db),
        history_manager_(history_manager) {}

  ~SettingsManagerImpl() override {
    if (needs_save_) {
      FlushToDisk("");
    }
  }

  absl::Status Initialize() override {
    // WriteJsonMessageToFile(theme_dirs_[0] / "default.json", GetDefaultTheme());

    auto maybe_content = ReadFileContentAsString(settings_path_);
    if (maybe_content.has_value()) {
      google::protobuf::json::ParseOptions opts;
      opts.ignore_unknown_fields = true;
      opts.case_insensitive_enum_parsing = true;
      std::string json = *maybe_content;
      auto status = google::protobuf::util::JsonStringToMessage(json, &settings_, opts);
      if (status.ok()) {
        if (!settings_.has_sounds()) {
          *settings_.mutable_sounds() = GetDefaultSoundSettings();
        }
      }

      return status;
    }

    // Write initial settings to file.
    settings_ = GetDefaultSettings();
    FlushToDisk("");
    return absl::OkStatus();
  }

  std::vector<std::string> ListCrosshairs() override {
    if (!std::filesystem::exists(crosshair_dir_) ||
        !std::filesystem::is_directory(crosshair_dir_)) {
      return {};
    }

    std::vector<std::string> valid_crosshair_names;
    for (const auto& entry : std::filesystem::directory_iterator(crosshair_dir_)) {
      std::string filename = entry.path().filename().string();
      if (std::filesystem::is_regular_file(entry) && filename.ends_with(".json")) {
        std::string name(absl::StripSuffix(filename, ".json"));
        valid_crosshair_names.push_back(name);
      }
    }

    std::vector<std::string> names;
    auto recent_crosshairs = history_manager_->GetRecentUniqueNames(ObjectType::CROSSHAIR, 10);
    for (auto& crosshair_name : recent_crosshairs) {
      if (VectorContains(valid_crosshair_names, crosshair_name)) {
        names.push_back(crosshair_name);
      }
    }

    for (auto& crosshair_name : valid_crosshair_names) {
      if (!VectorContains(names, crosshair_name)) {
        names.push_back(crosshair_name);
      }
    }
    return names;
  }

  SettingsUpdater CreateUpdater() override {
    return SettingsUpdater(this, history_manager_);
  }

  void RenameScenario(const std::string& old_name, const std::string& new_name) override {
    scenario_settings_cache_.erase(old_name);
  }

  std::vector<std::string> ListThemes() override {
    if (!std::filesystem::exists(theme_dir_) || !std::filesystem::is_directory(theme_dir_)) {
      return {};
    }

    std::vector<std::string> all_theme_names;
    for (const auto& entry : std::filesystem::directory_iterator(theme_dir_)) {
      std::string filename = entry.path().filename().string();
      if (std::filesystem::is_regular_file(entry) && filename.ends_with(".json")) {
        std::string name(absl::StripSuffix(filename, ".json"));
        all_theme_names.push_back(name);
      }
    }

    std::vector<std::string> theme_names;
    auto recent_themes = history_manager_->GetRecentViews(ObjectType::THEME, 20);
    for (auto& recent_theme : recent_themes) {
      if (VectorContains(all_theme_names, recent_theme.name)) {
        theme_names.push_back(recent_theme.name);
      }
    }

    for (auto& theme_name : all_theme_names) {
      if (!VectorContains(theme_names, theme_name)) {
        theme_names.push_back(theme_name);
      }
    }
    return theme_names;
  }

  std::vector<std::string> ListTextures() override {
    std::vector<std::string> texture_names;
    for (const auto& entry : std::filesystem::directory_iterator(texture_dir_)) {
      std::string filename = entry.path().filename().string();
      if (std::filesystem::is_regular_file(entry)) {
        texture_names.push_back(filename);
      }
    }
    return texture_names;
  }

  Crosshair GetCrosshair(const std::string& name) override {
    auto it = crosshair_cache_.find(name);
    if (it != crosshair_cache_.end()) {
      return it->second;
    }

    auto path = crosshair_dir_ / std::format("{}.json", name);
    Crosshair crosshair = GetDefaultCrosshair();
    if (std::filesystem::exists(path)) {
      if (!ReadJsonMessageFromFile(path, &crosshair)) {
        Logger::get()->warn("Unable to parse crosshair json file for {}", name);
        crosshair = GetDefaultCrosshair();
      }
    }

    // Cache invalid entries too with the default value.
    crosshair_cache_[name] = crosshair;
    return crosshair;
  }

  bool CrosshairExists(const std::string& name) override {
    return std::filesystem::exists(GetCrosshairPath(name));
  }

  bool ThemeExists(const std::string& name) override {
    return std::filesystem::exists(GetThemePath(name));
  }

  bool SaveCrosshair(const std::string& name, const Crosshair& crosshair) override {
    bool saved = WriteJsonMessageToFile(GetCrosshairPath(name), crosshair);
    if (saved) {
      crosshair_cache_[name] = crosshair;
    }
    return saved;
  }

  bool DeleteCrosshair(const std::string& name) override {
    return std::filesystem::remove(GetCrosshairPath(name));
  }

  void RenameCrosshair(const std::string& old_name, const std::string& new_name) override {
    std::filesystem::rename(GetCrosshairPath(old_name), GetCrosshairPath(new_name));
    crosshair_cache_.erase(old_name);
    crosshair_cache_.erase(new_name);
  }

  Theme GetTheme(const std::string& theme_name) override {
    std::string current_theme_name = theme_name;
    for (int i = 0; i < 20; ++i) {
      Theme theme = GetThemeNoReferenceFollow(current_theme_name);
      if (!theme.has_reference()) {
        return theme;
      }
      current_theme_name = theme.reference();
    }

    return GetDefaultTheme();
  }

  Theme GetThemeNoReferenceFollow(const std::string& theme_name) override {
    if (theme_name.size() == 0) {
      return GetDefaultTheme();
    }
    auto it = theme_cache_.find(theme_name);
    if (it != theme_cache_.end()) {
      return it->second.theme;
    }

    auto path = theme_dir_ / std::format("{}.json", theme_name);
    if (std::filesystem::exists(path)) {
      Theme theme;
      if (ReadJsonMessageFromFile(path, &theme)) {
        if (!theme.has_health_bar()) {
          *theme.mutable_health_bar() = GetDefaultHealthBarAppearance();
        }
        ThemeCacheEntry entry;
        entry.theme = theme;
        entry.file_path = path;
        entry.last_modified_time = std::filesystem::last_write_time(path);
        theme_cache_[theme_name] = entry;
        return theme;
      }
    }

    return GetDefaultTheme();
  }

  Theme GetCurrentTheme() override {
    Settings* settings = GetMutableCurrentSettings();
    if (settings == nullptr) {
      return GetDefaultTheme();
    }
    return GetTheme(settings->theme_name());
  }

  bool SaveTheme(const std::string& theme_name, const Theme& theme) override {
    bool saved = WriteJsonMessageToFile(GetThemePath(theme_name), theme);
    if (saved) {
      theme_cache_.erase(theme_name);
    }
    return saved;
  }

  bool DeleteTheme(const std::string& name) override {
    return std::filesystem::remove(GetThemePath(name));
  }

  void RenameTheme(const std::string& old_name, const std::string& new_name) override {
    std::filesystem::rename(GetThemePath(old_name), GetThemePath(new_name));
    theme_cache_.erase(old_name);
    theme_cache_.erase(new_name);
  }

  void MaybeInvalidateThemeCache() override {
    bool something_changed = false;
    for (auto& map_entry : theme_cache_) {
      auto& cache_entry = map_entry.second;
      auto last_modified_time = std::filesystem::last_write_time(cache_entry.file_path);
      if (last_modified_time > cache_entry.last_modified_time) {
        Logger::get()->info("Invalidate theme cache entry for {}", map_entry.first);
        something_changed = true;
      }
    }
    if (something_changed) {
      // Invalidate everything since references could not have changed while the theme they point to
      // changed.
      theme_cache_.clear();
    }
  }

  float GetDpi() override {
    float dpi = settings_.dpi();
    return dpi > 0 ? dpi : kDefaultDpi;
  }

  Settings GetCurrentSettingsForScenario(const std::string& scenario_name_raw) override {
    if (settings_.disable_per_scenario_settings() || scenario_name_raw.size() == 0) {
      return settings_;
    }
    // Only store scenario settings for the base name and share for all levels / cm suffixes.
    // TODO: Add an option in settings to configure this behavior
    const std::string& scenario_name = GetBaseNameForScenarioSettings(scenario_name_raw);

    auto it = scenario_settings_cache_.find(scenario_name);
    ScenarioSettings scenario_settings;
    bool missing_scenario_settings = false;
    if (it != scenario_settings_cache_.end()) {
      scenario_settings = it->second;
    } else {
      i64 scenario_id = db_->GetScenarioId(scenario_name);
      scenario_settings = db_->GetScenarioSettings(scenario_id);
      if (!IsDefaultInstance(scenario_settings)) {
        scenario_settings_cache_[scenario_name] = scenario_settings;
      } else {
        missing_scenario_settings = true;
      }
    }

    auto config = settings_.scenario_settings_config();
    auto should_set = [](ScenarioSettingsStoreType type) {
      return type == ScenarioSettingsStoreType::SCENARIO_SETTINGS_STORE_TYPE_UNKNOWN ||
             type == ScenarioSettingsStoreType::STORE_PER_SCENARIO;
    };
    auto should_set_default_global = [](ScenarioSettingsStoreType type) {
      return type == ScenarioSettingsStoreType::STORE_PER_SCENARIO;
    };

    if (scenario_settings.has_cm_per_360() && should_set(config.cm_per_360())) {
      settings_.set_cm_per_360(scenario_settings.cm_per_360());
    }
    if (scenario_settings.has_theme_name() && should_set(config.theme_name())) {
      settings_.set_theme_name(scenario_settings.theme_name());
    }
    if (scenario_settings.has_metronome_bpm() && should_set(config.metronome_bpm())) {
      settings_.set_metronome_bpm(scenario_settings.metronome_bpm());
    }
    if (should_set(config.enable_metronome())) {
      settings_.set_enable_metronome(scenario_settings.enable_metronome());
    }
    if (scenario_settings.has_crosshair_size() && should_set(config.crosshair_size())) {
      settings_.set_crosshair_size(scenario_settings.crosshair_size());
    }
    if (scenario_settings.has_crosshair_name() && should_set(config.crosshair_name())) {
      settings_.set_current_crosshair_name(scenario_settings.crosshair_name());
    }
    if (scenario_settings.has_tracking_shots_per_second() &&
        should_set_default_global(config.tracking_shots_per_second())) {
      settings_.set_tracking_shots_per_second(scenario_settings.tracking_shots_per_second());
    }
    if (should_set_default_global(config.auto_hold_tracking())) {
      settings_.set_auto_hold_tracking(scenario_settings.auto_hold_tracking());
    }
    if (scenario_settings.has_health_bar() && should_set(config.health_bar())) {
      *settings_.mutable_health_bar() = scenario_settings.health_bar();
    }

    if (missing_scenario_settings) {
      WriteScenarioSettings(scenario_name);
    }

    return settings_;
  }

  Settings GetCurrentSettings() override {
    return settings_;
  }

  Settings* GetMutableCurrentSettings() override {
    return &settings_;
  }

  Crosshair GetCurrentCrosshair() override {
    Settings settings = GetCurrentSettings();
    return GetCrosshair(settings.current_crosshair_name());
  }

  void MarkDirty() override {
    needs_save_ = true;
  }

  bool MaybeFlushToDisk(const std::string& scenario_name) override {
    if (needs_save_) {
      FlushToDisk(scenario_name);
      return true;
    }
    return false;
  }

  void FlushToDisk(const std::string& scenario_name) override {
    WriteScenarioSettings(scenario_name);
    if (WriteJsonMessageToFile(settings_path_, settings_)) {
      needs_save_ = false;
    }
  }

 private:
  void WriteScenarioSettings(const std::string& scenario_name_raw) {
    if (scenario_name_raw.size() > 0) {
      const std::string& scenario_name = GetBaseNameForScenarioSettings(scenario_name_raw);
      ScenarioSettings scenario_settings;
      scenario_settings.set_crosshair_size(settings_.crosshair_size());
      scenario_settings.set_crosshair_name(settings_.current_crosshair_name());
      scenario_settings.set_tracking_shots_per_second(settings_.tracking_shots_per_second());
      scenario_settings.set_cm_per_360(settings_.cm_per_360());
      scenario_settings.set_metronome_bpm(settings_.metronome_bpm());
      scenario_settings.set_enable_metronome(settings_.enable_metronome());
      scenario_settings.set_theme_name(settings_.theme_name());
      scenario_settings.set_auto_hold_tracking(settings_.auto_hold_tracking());
      i64 scenario_id = db_->GetScenarioId(scenario_name);
      db_->UpdateScenarioSettings(scenario_id, scenario_settings);
      scenario_settings_cache_[scenario_name] = scenario_settings;
    }
  }

  std::filesystem::path GetCrosshairPath(const std::string& name) {
    return crosshair_dir_ / std::format("{}.json", name);
  }

  std::filesystem::path GetThemePath(const std::string& name) {
    return theme_dir_ / std::format("{}.json", name);
  }

  std::filesystem::path settings_path_;
  Settings settings_;
  bool needs_save_ = false;
  std::filesystem::path theme_dir_;
  std::filesystem::path texture_dir_;
  std::filesystem::path crosshair_dir_;
  std::unordered_map<std::string, ThemeCacheEntry> theme_cache_;
  std::unordered_map<std::string, ScenarioSettings> scenario_settings_cache_;
  std::unordered_map<std::string, Crosshair> crosshair_cache_;
  AimDb* db_;
  HistoryManager* history_manager_;
};

}  // namespace

std::string GetMouseButtonName(u8 button) {
  if (button == SDL_BUTTON_LEFT) {
    return "Left Click";
  }
  if (button == SDL_BUTTON_MIDDLE) {
    return "Middle Click";
  }
  if (button == SDL_BUTTON_RIGHT) {
    return "Right Click";
  }
  if (button == SDL_BUTTON_X1) {
    return "Mouse X1";
  }
  if (button == SDL_BUTTON_X2) {
    return "Mouse X2";
  }
  return "";
}

bool IsMappableKeyDownEvent(const SDL_Event& event) {
  return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_KEY_DOWN;
}

bool IsMappableKeyUpEvent(const SDL_Event& event) {
  return event.type == SDL_EVENT_KEY_UP || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
}

std::string GetKeyNameForEvent(const SDL_Event& event) {
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
    return GetMouseButtonName(event.button.button);
  }
  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
    return SDL_GetKeyName(event.key.key);
  }
  return "";
}

bool IsEscapeKeyDown(const SDL_Event& event) {
  return event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE;
}

bool KeyNameMatchesEvent(const SDL_Event& event, const std::string& name) {
  if (name.size() == 0) {
    return false;
  }
  return absl::AsciiStrToLower(name) == absl::AsciiStrToLower(GetKeyNameForEvent(event));
}

bool KeyMappingMatchesEvent(const SDL_Event& event, const KeyMapping& mapping) {
  std::string event_name = absl::AsciiStrToLower(GetKeyNameForEvent(event));
  return KeyMappingMatchesEvent(event_name, mapping);
}

bool KeyMappingMatchesEvent(const std::string& event_name, const KeyMapping& mapping) {
  if (event_name.size() == 0) {
    return false;
  }
  if (absl::AsciiStrToLower(mapping.mapping1()) == event_name) {
    return true;
  }
  if (absl::AsciiStrToLower(mapping.mapping2()) == event_name) {
    return true;
  }
  if (absl::AsciiStrToLower(mapping.mapping3()) == event_name) {
    return true;
  }
  if (absl::AsciiStrToLower(mapping.mapping4()) == event_name) {
    return true;
  }
  return false;
}

SettingsUpdater::SettingsUpdater(SettingsManager* settings_manager, HistoryManager* history_manager)
    : settings_manager_(settings_manager), history_manager_(history_manager) {
  auto current_settings = settings_manager_->GetMutableCurrentSettings();
  if (current_settings != nullptr) {
    settings = *current_settings;
  }
}

void SettingsUpdater::SaveIfChangesMade(const std::string& scenario_name) {
  auto current_settings = settings_manager_->GetMutableCurrentSettings();
  if (current_settings == nullptr) {
    Logger::get()->warn("No settings available at save time");
    return;
  }
  if (IsEquivalentProto(*current_settings, settings)) {
    return;
  }
  std::string theme_name = settings.theme_name();
  if (current_settings->theme_name() != theme_name) {
    if (theme_name.size() > 0) {
      history_manager_->UpdateRecentView(ObjectType::THEME, theme_name);
    }
  }
  std::string crosshair_name = settings.current_crosshair_name();
  if (current_settings->current_crosshair_name() != crosshair_name) {
    if (crosshair_name.size() > 0) {
      history_manager_->UpdateRecentView(ObjectType::CROSSHAIR, crosshair_name);
    }
  }

  *current_settings = settings;
  settings_manager_->MarkDirty();
  settings_manager_->MaybeFlushToDisk(scenario_name);
  settings_manager_->MaybeInvalidateThemeCache();
}

Theme GetDefaultTheme() {
  Theme t;
  *(t.mutable_crosshair()->mutable_color()) = ToStoredColor("#FFAC1C");
  *(t.mutable_crosshair()->mutable_outline_color()) = ToStoredColor("#000000");

  *t.mutable_front_appearance()->mutable_color() = ToStoredColor(0.7);
  *t.mutable_floor_appearance()->mutable_color() = ToStoredColor(0.6);
  *t.mutable_roof_appearance()->mutable_color() = ToStoredColor(0.6);
  *t.mutable_side_appearance()->mutable_color() = ToStoredColor(0.65);

  *t.mutable_target_color() = ToStoredColor(0);
  *t.mutable_ghost_target_color() = ToStoredColor(0.65);

  *t.mutable_health_bar() = GetDefaultHealthBarAppearance();
  return t;
}

HealthBarAppearance GetDefaultHealthBarAppearance() {
  HealthBarAppearance h;
  *h.mutable_background_color() = ToStoredColor(0.75);
  h.set_background_alpha(0.8);

  *h.mutable_health_color() = ToStoredColor(0.3);
  return h;
}

Crosshair GetDefaultCrosshair() {
  Crosshair crosshair;
  crosshair.add_layers()->mutable_dot()->set_outline_thickness(1);
  return crosshair;
}

std::unique_ptr<SettingsManager> CreateSettingsManager(const std::filesystem::path& settings_path,
                                                       const std::filesystem::path& theme_dir,
                                                       const std::filesystem::path& texture_dir,
                                                       const std::filesystem::path& crosshair_dir,
                                                       AimDb* db,
                                                       HistoryManager* history_manager) {
  return std::make_unique<SettingsManagerImpl>(
      settings_path, theme_dir, texture_dir, crosshair_dir, db, history_manager);
}

bool ReadSettingsFile(const std::filesystem::path& path, std::optional<Settings>* maybe_settings) {
  auto maybe_content = ReadFileContentAsString(path);
  if (!maybe_content.has_value()) {
    return true;
  }
  google::protobuf::json::ParseOptions opts;
  opts.ignore_unknown_fields = true;
  opts.case_insensitive_enum_parsing = true;
  std::string json = *maybe_content;

  Settings settings;
  auto status = google::protobuf::util::JsonStringToMessage(json, &settings, opts);
  if (!status.ok()) {
    return false;
  }

  *maybe_settings = settings;
  return true;
}

}  // namespace aim
