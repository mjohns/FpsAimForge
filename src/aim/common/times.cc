#include "times.h"

#include <chrono>
#include <format>

#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "aim/common/log.h"
#include "aim/common/util.h"

namespace aim {

std::string EpochSecondsToString(i64 epoch_seconds) {
#ifdef _WIN32
  std::chrono::sys_seconds tp{std::chrono::seconds(epoch_seconds)};
  std::chrono::zoned_time local_time{std::chrono::current_zone(), tp};
  return std::format("{:%Y-%m-%d %I:%M %p}", local_time);
#else
  absl::Time t = absl::FromUnixSeconds(epoch_seconds);
  absl::TimeZone local_tz = absl::LocalTimeZone();
  return absl::FormatTime("%Y-%m-%d %I:%M %p", t, local_tz);
#endif
}

void Stopwatch::Start() {
  if (running_) {
    return;
  }
  running_ = true;
  start_time_ = std::chrono::steady_clock::now();
}

void Stopwatch::Stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  auto now = std::chrono::steady_clock::now();
  previously_elapsed_duration_ += now - start_time_;
}

std::chrono::steady_clock::duration Stopwatch::GetElapsed() const {
  if (!running_) {
    return previously_elapsed_duration_;
  }
  auto now = std::chrono::steady_clock::now();
  auto elapsed = now - start_time_;
  return elapsed + previously_elapsed_duration_;
}

void Stopwatch::AddElapsedSeconds(float value) {
  std::chrono::duration<double> seconds(value);
  previously_elapsed_duration_ +=
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(seconds);
}

i64 Stopwatch::GetElapsedMicros() const {
  auto elapsed = GetElapsed();
  return std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
}

float Stopwatch::GetElapsedSeconds() const {
  auto elapsed = GetElapsed();
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  return millis / 1000.0f;
}

TimedInvoker::TimedInvoker(TimedInvokerParams params, std::function<void()> fn)
    : params_(params), fn_(std::move(fn)) {}

bool TimedInvoker::MaybeInvoke(i64 now_micros) {
  bool invoked = false;
  if (!initialized_) {
    if (params_.initial_delay_micros == 0) {
      this->Invoke(now_micros);
      invoked = true;
    } else {
      last_invoke_time_micros_ =
          now_micros + params_.initial_delay_micros - params_.interval_micros;
    }
    initialized_ = true;
    return invoked;
  }

  if (last_invoke_time_micros_ + params_.interval_micros <= now_micros) {
    this->Invoke(now_micros);
    invoked = true;
  }
  return invoked;
}

void TimedInvoker::Invoke(i64 now_micros) {
  last_invoke_time_micros_ = now_micros;
  if (fn_) {
    fn_();
  }
}

std::string GetHowLongAgoStringFromEpochSeconds(i64 start_epoch_seconds, i64 end_epoch_seconds) {
  return GetHowLongAgoStringFromEpochMicros(start_epoch_seconds * 1000000,
                                            end_epoch_seconds * 1000000);
}

std::string GetHowLongAgoStringFromEpochMicros(i64 start, i64 end) {
  i64 duration_micros = start > end ? start - end : end - start;
  auto duration = std::chrono::microseconds(duration_micros);

  int weeks = std::chrono::duration_cast<std::chrono::weeks>(duration).count();

  if (weeks >= 10) {
    int months = std::chrono::duration_cast<std::chrono::months>(duration).count();
    if (months >= 12) {
      std::string year_str = MaybeIntToString(months / 12.0f, 1);
      return year_str == "1" ? "1 year ago" : std::format("{} years ago", year_str);
    }
    return months == 1 ? std::format("{} month ago", months) : std::format("{} months ago", months);
  }

  if (weeks > 0) {
    return weeks == 1 ? std::format("{} week ago", weeks) : std::format("{} weeks ago", weeks);
  }

  int days = std::chrono::duration_cast<std::chrono::days>(duration).count();
  if (days > 0) {
    return days == 1 ? std::format("{} day ago", days) : std::format("{} days ago", days);
  }

  int hours = std::chrono::duration_cast<std::chrono::hours>(duration).count();
  if (hours > 0) {
    return hours == 1 ? std::format("{} hour ago", hours) : std::format("{} hours ago", hours);
  }

  int minutes = std::chrono::duration_cast<std::chrono::minutes>(duration).count();
  if (minutes > 0) {
    return minutes == 1 ? std::format("{} minute ago", minutes)
                        : std::format("{} minutes ago", minutes);
  }

  return "Just now";
}

std::optional<i64> ParseTimestampStringAsMicros(const std::string& timestamp) {
  if (timestamp.empty()) {
    return {};
  }
  absl::Time out;
  std::string err;
  if (!absl::ParseTime(absl::RFC3339_full, timestamp, &out, &err)) {
    Logger::get()->warn("Failed to parse time {}. err: {}", timestamp, err);
    return {};
  }
  return absl::ToUnixMicros(out);
}

i64 GetNowEpochMicros() {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

i64 GetNowEpochMillis() {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

i64 GetNowEpochSeconds() {
  return GetNowEpochMillis() / 1000;
}

i32 GetNowEpochMinutes() {
  return GetNowEpochSeconds() / 60;
}

std::string EpochMicrosToIsoDateString(i64 micros, absl::TimeZone time_zone) {
  return EpochSecondsToIsoDateString(micros / 1000000, time_zone);
}

std::string EpochSecondsToIsoDateString(i64 seconds, absl::TimeZone time_zone) {
  return absl::FormatTime("%Y%m%d", absl::FromTimeT(seconds), time_zone);
}

int YyyymmddToEpochDays(std::string_view date_str) {
  if (date_str.length() != 8) {
    return 0;
  }
  std::string formatted_str =
      std::format("{}-{}-{}", date_str.substr(0, 4), date_str.substr(4, 2), date_str.substr(6, 2));
  absl::CivilDay cd;
  if (!absl::ParseCivilTime(formatted_str, &cd)) {
    return 0;
  }
  return (cd - absl::CivilDay(1970, 1, 1));
}

}  // namespace aim
