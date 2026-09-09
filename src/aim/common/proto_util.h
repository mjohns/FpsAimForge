#pragma once

#include "google/protobuf/message.h"

namespace aim {

bool IsEquivalentProto(const google::protobuf::Message& lhs, const google::protobuf::Message& rhs);

template <typename T>
bool IsDefaultInstance(const T& message) {
  return IsEquivalentProto(message, message.default_instance());
}

}  // namespace aim
