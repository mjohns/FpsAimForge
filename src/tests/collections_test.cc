#include "aim/common/collections.h"

#include "aim/proto/guide.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

using namespace aim;

using ::protobuf_matchers::EqualsProto;

namespace {

GuideSection CreateGuideSection(std::vector<std::string> items) {
  GuideSection s;
  for (auto& item : items) {
    s.add_playlists(item);
  }
  return s;
}

}  // namespace

TEST(CollectionsTest, MoveRepeatedItem) {
  GuideSection section = CreateGuideSection({"1", "2", "3", "4"});

  MoveRepeatedItem(section.mutable_playlists(), 0, 0);
  EXPECT_THAT(section, EqualsProto(CreateGuideSection({"1", "2", "3", "4"})));

  MoveRepeatedItem(section.mutable_playlists(), 0, 4);
  EXPECT_THAT(section, EqualsProto(CreateGuideSection({"2", "3", "4", "1"})));

  MoveRepeatedItem(section.mutable_playlists(), 1, 3);
  EXPECT_THAT(section, EqualsProto(CreateGuideSection({"2", "4", "3", "1"})));

  MoveRepeatedItem(section.mutable_playlists(), -1, 3);
  EXPECT_THAT(section, EqualsProto(CreateGuideSection({"2", "4", "3", "1"})));
}
