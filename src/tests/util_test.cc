#include "aim/common/util.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace aim;

using ::testing::StrEq;

TEST(UtilTest, MaybeIntToString) {
  EXPECT_THAT(MaybeIntToString(1), StrEq("1"));
  EXPECT_THAT(MaybeIntToString(10), StrEq("10"));
  EXPECT_THAT(MaybeIntToString(10.2), StrEq("10.2"));
  EXPECT_THAT(MaybeIntToString(1.12), StrEq("1.1"));
  EXPECT_THAT(MaybeIntToString(1.0003), StrEq("1"));
  EXPECT_THAT(MaybeIntToString(10.1, 3), StrEq("10.1"));
  EXPECT_THAT(MaybeIntToString(0, 1), StrEq("0"));
  EXPECT_THAT(MaybeIntToString(10.2, 0), StrEq("10"));
  EXPECT_THAT(MaybeIntToString(0.03, 1), StrEq("0"));
  EXPECT_THAT(MaybeIntToString(4.999, 1), StrEq("4.9"));
}

TEST(UtilTest, MaybeIntToString_NoDecimals) {
  EXPECT_THAT(MaybeIntToString(1), StrEq("1"));
  EXPECT_THAT(MaybeIntToString(10), StrEq("10"));
  EXPECT_THAT(MaybeIntToString(0.1, 0), StrEq("0"));
  EXPECT_THAT(MaybeIntToString(10.001, 0), StrEq("10"));
  EXPECT_THAT(MaybeIntToString(10.001, 2), StrEq("10"));
  EXPECT_THAT(MaybeIntToString(1.0001, 3), StrEq("1"));
  EXPECT_THAT(MaybeIntToString(1.0000001, 3), StrEq("1"));
}

TEST(UtilTest, MaybeIntToString_ExactNumber) {
  EXPECT_THAT(MaybeIntToString(1.123, 3), StrEq("1.123"));
  EXPECT_THAT(MaybeIntToString(1.023, 3), StrEq("1.023"));
  EXPECT_THAT(MaybeIntToString(1.003, 3), StrEq("1.003"));
  EXPECT_THAT(MaybeIntToString(1.000, 3), StrEq("1"));
}
