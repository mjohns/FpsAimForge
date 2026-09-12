#include "aim/common/times.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace aim;

using ::testing::Eq;
using ::testing::StrEq;

TEST(TimesTest, GetHowLongAgoString) {
  // Times in seconds.
  i64 minute = 60;
  i64 hour = minute * 60;
  i64 day = hour * 24;
  i64 week = day * 7;

  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, minute), StrEq("1 minute ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 59), StrEq("Just now"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 2 * minute), StrEq("2 minutes ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, hour), StrEq("1 hour ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 1.5 * hour), StrEq("1 hour ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 2 * hour), StrEq("2 hours ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, day), StrEq("1 day ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 2 * day), StrEq("2 days ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 6 * day + hour * 14), StrEq("6 days ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 7 * day), StrEq("1 week ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 13 * day), StrEq("1 week ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 14 * day), StrEq("2 weeks ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 30 * day), StrEq("4 weeks ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 50 * day), StrEq("7 weeks ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 50 * day), StrEq("7 weeks ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 56 * day), StrEq("8 weeks ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 10 * week), StrEq("2 months ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 45 * week), StrEq("10 months ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 40 * week), StrEq("9 months ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 30 * week), StrEq("6 months ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 52 * week), StrEq("11 months ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 366 * day), StrEq("1 year ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 368 * day), StrEq("1 year ago"));
  EXPECT_THAT(GetHowLongAgoStringFromEpochSeconds(0, 72 * week), StrEq("1.3 years ago"));
}

TEST(TimesTest, EpochSecondsToIsoDateString) {
  auto tz = absl::UTCTimeZone();

  i64 sept12 = 1789171200;

  EXPECT_THAT(EpochSecondsToIsoDateString(sept12, tz), StrEq("20260912"));
  EXPECT_THAT(EpochSecondsToIsoDateString(sept12 + 60, tz), StrEq("20260912"));
  EXPECT_THAT(EpochSecondsToIsoDateString(sept12 - 1, tz), StrEq("20260911"));
}

TEST(TimesTest, YyyymmddToEpochDays) {
  EXPECT_THAT(YyyymmddToEpochDays("20260912"), Eq(20708));
  EXPECT_THAT(YyyymmddToEpochDays("20260913"), Eq(20709));
  EXPECT_THAT(YyyymmddToEpochDays("20260910"), Eq(20706));
}
