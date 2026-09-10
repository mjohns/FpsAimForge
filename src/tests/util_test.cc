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
}
