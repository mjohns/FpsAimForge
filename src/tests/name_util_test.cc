#include "aim/common/name_util.h"

#include <algorithm>
#include <optional>
#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace aim;

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::StrEq;

void ExpectParsedFloatValueSuffix(std::string str,
                                  std::string expected_suffix,
                                  float expected_value) {
  std::string_view suffix;
  float value;
  ASSERT_TRUE(ParseFloatValueSuffix(str, &suffix, &value)) << str;
  EXPECT_THAT(suffix, StrEq(expected_suffix));
  EXPECT_THAT(value, Eq(expected_value));
}

TEST(NameUtilTest, ParseFloatValueSuffix) {
  ExpectParsedFloatValueSuffix("25cm", "cm", 25);
  ExpectParsedFloatValueSuffix("-25s", "s", -25);
  ExpectParsedFloatValueSuffix("25s", "s", 25);
  ExpectParsedFloatValueSuffix("1s", "s", 1);

  std::string_view suffix;
  float value;
  EXPECT_FALSE(ParseFloatValueSuffix("1", &suffix, &value));
  EXPECT_FALSE(ParseFloatValueSuffix("cm12", &suffix, &value));
  EXPECT_FALSE(ParseFloatValueSuffix("cm", &suffix, &value));
  EXPECT_FALSE(ParseFloatValueSuffix("", &suffix, &value));
  EXPECT_FALSE(ParseFloatValueSuffix("a", &suffix, &value));
}

TEST(NameUtilTest, GetBundleName) {
  EXPECT_THAT(GetBundleName("Bundle"), StrEq("Bundle"));
  EXPECT_THAT(GetBundleName("Bundle Two"), StrEq("Bundle"));
  EXPECT_THAT(GetBundleName("Bundle   Two"), StrEq("Bundle"));
  EXPECT_THAT(GetBundleName("Bundle "), StrEq("Bundle"));
  EXPECT_THAT(GetBundleName(""), StrEq(""));
}

TEST(NameUtilTest, GetLevelFromWord) {
  EXPECT_FALSE(GetLevelFromWord("Scenario").has_value());
  EXPECT_FALSE(GetLevelFromWord("L").has_value());
  EXPECT_FALSE(GetLevelFromWord("s1").has_value());
  EXPECT_FALSE(GetLevelFromWord("LL").has_value());

  EXPECT_THAT(GetLevelFromWord("L1"), Optional(1));
  EXPECT_THAT(GetLevelFromWord("L2"), Optional(2));
  EXPECT_THAT(GetLevelFromWord("L-1"), Optional(-1));
  EXPECT_THAT(GetLevelFromWord("L-1.5"), Optional(-1.5));
  EXPECT_THAT(GetLevelFromWord("L0"), Optional(0));
}

TEST(NameUtilTest, GetNameInfo_NoSuffix) {
  NameInfo info = GetNameInfo("Scenario One");
  EXPECT_THAT(info.base_name, StrEq("Scenario One"));
  EXPECT_FALSE(info.level.has_value());
  EXPECT_FALSE(info.cm_per_360.has_value());

  EXPECT_THAT(info.GetFullName(), StrEq("Scenario One"));
}

TEST(NameUtilTest, GetNameInfo_Empty) {
  NameInfo info = GetNameInfo("");
  EXPECT_THAT(info.base_name, IsEmpty());
}

TEST(NameUtilTest, GetNameInfo_SingleChar) {
  NameInfo info = GetNameInfo("a");
  EXPECT_THAT(info.base_name, StrEq("a"));

  info = GetNameInfo("1");
  EXPECT_THAT(info.base_name, StrEq("1"));

  info = GetNameInfo("-");
  EXPECT_THAT(info.base_name, StrEq("-"));
}

TEST(NameUtilTest, GetNameInfo_TrailingWhitespace) {
  // TODO: Should this strip? We really don't want trailing whitespace in the system.
  NameInfo info = GetNameInfo("ab ");
  EXPECT_THAT(info.base_name, StrEq("ab "));
}

TEST(NameUtilTest, GetNameInfo_StripCm360) {
  NameInfo info = GetNameInfo("Scenario 25cm");
  EXPECT_THAT(info.base_name, StrEq("Scenario"));
  EXPECT_THAT(info.cm_per_360, Optional(25));
  EXPECT_FALSE(info.level.has_value());
  EXPECT_THAT(info.GetFullName(), StrEq("Scenario 25cm"));
}

TEST(NameUtilTest, GetNameInfo_StripLevel) {
  NameInfo info = GetNameInfo("Scenario L1.5");
  EXPECT_THAT(info.base_name, StrEq("Scenario"));
  EXPECT_THAT(info.level, Optional(1.5));
  EXPECT_FALSE(info.cm_per_360.has_value());

  EXPECT_THAT(info.GetFullName(), StrEq("Scenario L1.5"));
}

TEST(NameUtilTest, GetNameInfo_StripAll) {
  NameInfo info = GetNameInfo("Scenario L1.5 35cm 40s");
  EXPECT_THAT(info.base_name, StrEq("Scenario"));
  EXPECT_THAT(info.level, Optional(1.5));
  EXPECT_THAT(info.cm_per_360, Optional(35));
  EXPECT_THAT(info.duration, Optional(40));

  EXPECT_THAT(info.GetFullName(), StrEq("Scenario L1.5 40s 35cm"));
}

TEST(NameUtilTest, GetNameInfo_NoSuffixMatch) {
  NameInfo info = GetNameInfo("SERF ww5t Int -1Bot");

  EXPECT_THAT(info.base_name, StrEq("SERF ww5t Int -1Bot"));

  EXPECT_THAT(info.GetFullName(), StrEq("SERF ww5t Int -1Bot"));
}

TEST(NameUtilTest, GetNameInfo_NoSuffixMatch_HasDynamicSuffixToo) {
  NameInfo info = GetNameInfo("SERF ww5t Int -1Bot 120fov");

  EXPECT_THAT(info.base_name, StrEq("SERF ww5t Int -1Bot"));

  EXPECT_THAT(info.GetFullName(), StrEq("SERF ww5t Int -1Bot 120fov"));
}

TEST(NameUtilTest, GetSortedLevelNames) {
  NameInfo cm35 = GetNameInfo("S 35cm");
  NameInfo cm35_ln1 = GetNameInfo("S L-1 35cm");
  NameInfo cm35_l0 = GetNameInfo("S L0 35cm");
  NameInfo cm35_l1 = GetNameInfo("S L1 35cm");
  NameInfo cm35_l2 = GetNameInfo("S L2 35cm");
  NameInfo cm35_l3 = GetNameInfo("S L3 35cm");
  NameInfo cm35_l4 = GetNameInfo("S L4 35cm");
  NameInfo cm35_l5_5 = GetNameInfo("S L5.5 35cm");
  NameInfo cm35_l10 = GetNameInfo("S L10 35cm");
  NameInfo cm35_l11 = GetNameInfo("S L11 35cm");

  NameInfo cm45 = GetNameInfo("S 45cm");
  NameInfo cm45_l1 = GetNameInfo("S L1 45cm");
  NameInfo cm45_l2 = GetNameInfo("S L2 45cm");
  NameInfo cm45_l3 = GetNameInfo("S L3 45cm");
  NameInfo cm45_l4 = GetNameInfo("S L4 45cm");
  NameInfo cm45_l5_5 = GetNameInfo("S L5.5 45cm");
  NameInfo cm45_l10 = GetNameInfo("S L10 45cm");
  NameInfo cm45_l11 = GetNameInfo("S L11 45cm");

  NameInfo l1 = GetNameInfo("S L1");
  NameInfo l2 = GetNameInfo("S L2");
  NameInfo l3 = GetNameInfo("S L3");
  NameInfo l4 = GetNameInfo("S L4");
  NameInfo l5_5 = GetNameInfo("S L5.5");
  NameInfo l10 = GetNameInfo("S L10");
  NameInfo l11 = GetNameInfo("S L11");

  NameInfo base_name = GetNameInfo("S");

  std::vector<NameInfo> names{
      cm35,      cm35_ln1, cm35_l0, cm35_l1, cm35_l2, cm35_l3, cm35_l4,   cm35_l5_5, cm35_l10,
      cm35_l11,  cm45,     cm45_l1, cm45_l2, cm45_l3, cm45_l4, cm45_l5_5, cm45_l10,  cm45_l11,
      base_name, l1,       l2,      l3,      l4,      l5_5,    l10,       l11,
  };
  std::random_device rd;
  std::mt19937 g(rd());

  std::shuffle(names.begin(), names.end(), g);

  EXPECT_THAT(GetSortedLevelNames(cm35_l1, names),
              ElementsAre(StrEq(cm35_ln1.GetFullName()),
                          StrEq(cm35_l0.GetFullName()),
                          StrEq(cm35_l1.GetFullName()),
                          StrEq(cm35_l2.GetFullName()),
                          StrEq(cm35_l3.GetFullName()),
                          StrEq(cm35_l4.GetFullName()),
                          StrEq(cm35_l5_5.GetFullName()),
                          StrEq(cm35_l10.GetFullName()),
                          StrEq(cm35_l11.GetFullName())));

  EXPECT_THAT(GetSortedLevelNames(base_name, names),
              ElementsAre(StrEq(l1.GetFullName()),
                          StrEq(l2.GetFullName()),
                          StrEq(l3.GetFullName()),
                          StrEq(l4.GetFullName()),
                          StrEq(l5_5.GetFullName()),
                          StrEq(l10.GetFullName()),
                          StrEq(l11.GetFullName())));
}
