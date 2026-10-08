#include "aim/common/http.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace aim;

using ::testing::Eq;
using ::testing::Optional;
using ::testing::StrEq;

TEST(HttpTest, ParseEtagFromHeader) {
  EXPECT_THAT(ParseEtagFromHeader("etag: abcd"), Optional(StrEq("abcd")));
  EXPECT_THAT(ParseEtagFromHeader("etag:  abcd"), Optional(StrEq("abcd")));
  EXPECT_THAT(ParseEtagFromHeader("etag:abcd"), Optional(StrEq("abcd")));
  EXPECT_THAT(ParseEtagFromHeader("etag:abcd "), Optional(StrEq("abcd")));
  EXPECT_THAT(ParseEtagFromHeader("etag:abcd\n"), Optional(StrEq("abcd")));
  EXPECT_THAT(ParseEtagFromHeader("ETAG:abcd\n"), Optional(StrEq("abcd")));
  EXPECT_THAT(ParseEtagFromHeader("ETAG: \"abcd\""), Optional(StrEq("\"abcd\"")));
  EXPECT_THAT(ParseEtagFromHeader("ETAG: W/\"abcd\""), Optional(StrEq("W/\"abcd\"")));
  EXPECT_THAT(ParseEtagFromHeader("etag:  "), Eq(std::nullopt));
  EXPECT_THAT(ParseEtagFromHeader("etag:"), Eq(std::nullopt));
  EXPECT_THAT(ParseEtagFromHeader("bad"), Eq(std::nullopt));
  EXPECT_THAT(ParseEtagFromHeader(""), Eq(std::nullopt));
}

TEST(HttpTest, MakeEtagHeader) {
  EXPECT_THAT(MakeEtagHeader(" abcd"), StrEq("If-None-Match: \"abcd\""));
  EXPECT_THAT(MakeEtagHeader(" \"abcd\""), StrEq("If-None-Match: \"abcd\""));
  EXPECT_THAT(MakeEtagHeader("W/\"abcd\""), StrEq("If-None-Match: W/\"abcd\""));
}
