#include "sound.h"

#include <string>

#include "SDL3_mixer/SDL_mixer.h"
#include "aim/common/collections.h"
#include "glm/common.hpp"

namespace aim {
namespace {

constexpr const int kTrackQueueSize = 4;

}  // namespace

std::unique_ptr<Sound> Sound::Load(MIX_Mixer* mixer, const std::filesystem::path& sound_path) {
  if (!std::filesystem::exists(sound_path)) {
    return {};
  }
  MIX_Audio* audio = MIX_LoadAudio(mixer, sound_path.string().c_str(), true);
  if (audio == nullptr) {
    return {};
  }
  std::vector<MIX_Track*> track_queue;
  track_queue.reserve(kTrackQueueSize);
  for (int i = 0; i < kTrackQueueSize; ++i) {
    MIX_Track* track = MIX_CreateTrack(mixer);
    if (track == nullptr) {
      return {};
    }
    if (!MIX_SetTrackAudio(track, audio)) {
      return {};
    }
    track_queue.push_back(track);
  }
  return std::unique_ptr<Sound>(new Sound(mixer, audio, std::move(track_queue)));
}

Sound::Sound(MIX_Mixer* mixer, MIX_Audio* audio, std::vector<MIX_Track*> track_queue)
    : mixer_(mixer), audio_(audio), track_queue_(std::move(track_queue)) {}

Sound::~Sound() {}

void Sound::Play(PlaySoundOptions options) {
  int i = current_track_queue_index_;
  if (!IsValidIndex(track_queue_, i)) {
    return;
  }
  current_track_queue_index_ = (current_track_queue_index_ + 1) % track_queue_.size();

  // Use a queue of tracks so that overlapping sounds don't cut each other off. This happens for the
  // hit sound being played in fast succession.
  MIX_Track* track = track_queue_[i];

  if (options.gain) {
    MIX_SetTrackGain(track, *options.gain);
  }
  if (options.pitch_modifier) {
    MIX_SetTrackFrequencyRatio(track, glm::clamp<float>(*options.pitch_modifier, 0.01f, 100.0f));
  }
  MIX_PlayTrack(track, 0);
}

}  // namespace aim
