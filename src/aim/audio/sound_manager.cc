#include "sound_manager.h"

#include "SDL3_mixer/SDL_mixer.h"
#include "absl/algorithm/container.h"
#include "glm/common.hpp"

namespace aim {
namespace {

constexpr const float kMaxGain = 2.0f;

constexpr const int kHitChannel = 1;
constexpr const int kShootChannel = 2;
constexpr const int kMetronomeChannel = 3;
constexpr const int kKillChannel = 4;

std::unique_ptr<Sound> LoadSound(MIX_Mixer* mixer,
                                 const std::vector<std::filesystem::path>& sound_dirs,
                                 const std::string& name) {
  for (const std::filesystem::path& dir : sound_dirs) {
    auto loaded_sound = Sound::Load(mixer, dir / name);
    if (loaded_sound) {
      return loaded_sound;
    }
  }

  return {};
}

}  // namespace

std::vector<std::string> SoundManager::ListSounds() {
  std::unordered_set<std::string> sound_names;
  for (const std::filesystem::path& dir : sound_dirs_) {
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      continue;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      std::string filename = entry.path().filename().string();
      if (filename.ends_with(".ogg")) {
        sound_names.insert(filename);
      }
    }
  }

  std::vector<std::string> sorted_sound_names(sound_names.begin(), sound_names.end());
  absl::c_sort(sorted_sound_names);
  return sorted_sound_names;
}

SoundManager::SoundManager(MIX_Mixer* mixer, const std::vector<std::filesystem::path>& sound_dirs)
    : sound_dirs_(sound_dirs), mixer_(mixer) {}

void SoundManager::LoadSounds(const Settings& settings) {
  if (mixer_ == nullptr) {
    return;
  }
  const SoundSettings& s = settings.sounds();
  if (s.has_master_volume_level()) {
    float level = glm::clamp<float>(s.master_volume_level(), 0, kMaxGain);
    MIX_SetMixerGain(mixer_, level);
  }
  std::vector<SoundItem> sounds{
      s.click_hit(),
      s.click_miss(),
      s.click_kill(),
      s.tracking_hit(),
      s.tracking_miss(),
      s.tracking_kill(),
      s.metronome(),
      s.reload(),
      s.target_expired(),
  };
  for (const SoundItem& item : sounds) {
    if (item.name().size() == 0) {
      continue;
    }
    MaybeLoadSound(item.name());
  }
}

bool SoundManager::LoadAndPlaySound(const SoundItem& item) {
  MaybeLoadSound(item.name());
  return PlayLoadedSound(item);
}

void SoundManager::MaybeLoadSound(const std::string& sound_name) {
  if (mixer_ == nullptr) {
    return;
  }
  if (sound_name.empty()) {
    return;
  }
  auto it = sound_cache_.find(sound_name);
  if (it == sound_cache_.end()) {
    std::unique_ptr<Sound> sound = LoadSound(mixer_, sound_dirs_, sound_name);
    sound_cache_[sound_name] = std::move(sound);
  }
}

bool SoundManager::PlayLoadedSound(const SoundItem& item, float gain) {
  if (item.name().empty()) {
    return false;
  }
  auto it = sound_cache_.find(item.name());
  if (it != sound_cache_.end()) {
    Sound* sound = it->second.get();
    if (sound != nullptr) {
      PlaySoundOptions opts;
      opts.gain = gain * (item.has_volume_level() ? item.volume_level() : 1.0f);
      if (item.has_pitch_modifier()) {
        opts.pitch_modifier = item.pitch_modifier();
      }
      sound->Play(opts);
      return true;
    }
  }

  return false;
}

bool SoundManager::PlayLoadedSound(const SoundSettings& settings, SoundType type, float gain) {
  switch (type) {
    case SoundType::CLICK_MISS:
      return PlayLoadedSound(settings.click_miss(), gain);
    case SoundType::CLICK_HIT:
      return PlayLoadedSound(settings.click_hit(), gain);
    case SoundType::CLICK_KILL:
      return PlayLoadedSound(settings.click_kill(), gain);
    case SoundType::TRACKING_MISS:
      return PlayLoadedSound(settings.tracking_miss(), gain);
    case SoundType::TRACKING_HIT:
      return PlayLoadedSound(settings.tracking_hit(), gain);
    case SoundType::TRACKING_KILL:
      return PlayLoadedSound(settings.tracking_kill(), gain);
    case SoundType::METRONOME:
      return PlayLoadedSound(settings.metronome(), gain);
    case SoundType::RELOAD:
      return PlayLoadedSound(settings.reload(), gain);
    case SoundType::TARGET_EXPIRED:
      return PlayLoadedSound(settings.target_expired(), gain);
  }
  return false;
}

}  // namespace aim
