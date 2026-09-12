#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "absl/time/time.h"
#include "aim/common/simple_types.h"

namespace aim {

std::string EpochSecondsToString(i64 epoch_seconds);
std::string EpochSecondsToIsoDateString(i64 micros, absl::TimeZone);
std::string EpochMicrosToIsoDateString(i64 micros, absl::TimeZone);

int YyyymmddToEpochDays(std::string_view date_str);

i64 GetNowEpochMicros();
i64 GetNowEpochMillis();
i64 GetNowEpochSeconds();
i32 GetNowEpochMinutes();

// Gets the interval between events in microseconds for a rate of n times per second.
static i64 TimesPerSecondToIntervalMicros(float times_per_second) {
  i64 micros_per_second = 1000000;
  return micros_per_second / times_per_second;
}

inline float MicrosToSeconds(i64 micros) {
  return micros / 1000000.0f;
}

inline i64 SecondsToMicros(float seconds) {
  return seconds * 1000000;
}

std::string GetHowLongAgoStringFromEpochMicros(i64 start_epoch_micros, i64 end_epoch_micros);
std::string GetHowLongAgoStringFromEpochSeconds(i64 start_epoch_seconds, i64 end_epoch_seconds);
std::optional<i64> ParseTimestampStringAsMicros(const std::string& timestamp);

class Stopwatch {
 public:
  void Start();
  void Stop();

  bool IsRunning() {
    return running_;
  }

  std::chrono::steady_clock::duration GetElapsed() const;
  i64 GetElapsedMicros() const;
  float GetElapsedSeconds() const;

  void AddElapsedSeconds(float seconds);

 private:
  bool running_ = false;
  std::chrono::steady_clock::time_point start_time_;
  std::chrono::steady_clock::duration previously_elapsed_duration_{0};
};

// Class to invoke a function at a given rate.
struct TimedInvokerParams {
  static TimedInvokerParams TimesPerSecond(float times_per_second) {
    TimedInvokerParams params;
    params.interval_micros = TimesPerSecondToIntervalMicros(times_per_second);
    return params;
  }

  // Default to playing once every second.
  i64 interval_micros = 1000000;
  i64 initial_delay_micros = 0;
};

class TimedInvoker {
 public:
  TimedInvoker(TimedInvokerParams params, std::function<void()> fn);

  // Returns if it was invoked.
  bool MaybeInvoke(i64 now_micros);

 private:
  void Invoke(i64 now_micros);

  bool initialized_ = false;
  TimedInvokerParams params_;
  i64 last_invoke_time_micros_ = 0;
  std::function<void()> fn_;
};

}  // namespace aim
