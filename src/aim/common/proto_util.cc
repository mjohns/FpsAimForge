#include "proto_util.h"

#include "google/protobuf/message.h"
#include "google/protobuf/util/message_differencer.h"

namespace aim {

bool IsEquivalentProto(const google::protobuf::Message& lhs, const google::protobuf::Message& rhs) {
  return google::protobuf::util::MessageDifferencer::Equivalent(lhs, rhs);
}

}  // namespace aim
