#pragma once

#include <google/protobuf/message_lite.h>
#include <google/protobuf/stubs/common.h>

#include <cstdint>
#include <vector>

namespace aauto::utils {

template <typename T>
std::size_t protobuf_byte_size(const T& msg) {
#if defined(GOOGLE_PROTOBUF_VERSION) && GOOGLE_PROTOBUF_VERSION >= 3004000
    return static_cast<std::size_t>(msg.ByteSizeLong());
#else
    return static_cast<std::size_t>(msg.ByteSize());
#endif
}

template <typename T>
std::vector<uint8_t> serialize_to_vector(const T& msg) {
    std::vector<uint8_t> buf(protobuf_byte_size(msg));
    msg.SerializeToArray(buf.data(), static_cast<int>(buf.size()));
    return buf;
}

} // namespace aauto::utils
