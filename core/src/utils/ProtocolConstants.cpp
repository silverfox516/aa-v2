#include "aauto/utils/ProtocolConstants.hpp"

namespace {

class AapErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override { return "aap"; }

    std::string message(int ev) const override {
        switch (static_cast<aauto::AapErrc>(ev)) {
            case aauto::AapErrc::Success:                return "success";
            case aauto::AapErrc::TransportClosed:        return "transport closed";
            case aauto::AapErrc::TransportReadError:     return "transport read error";
            case aauto::AapErrc::TransportWriteError:    return "transport write error";
            case aauto::AapErrc::SslHandshakeFailed:     return "SSL handshake failed";
            case aauto::AapErrc::VersionMismatch:        return "protocol version mismatch";
            case aauto::AapErrc::AuthFailed:             return "authentication failed";
            case aauto::AapErrc::ServiceDiscoveryFailed: return "service discovery failed";
            case aauto::AapErrc::ChannelOpenFailed:      return "channel open failed";
            case aauto::AapErrc::PingTimeout:            return "ping timeout";
            case aauto::AapErrc::FramingError:           return "framing error";
            case aauto::AapErrc::DecryptionFailed:       return "decryption failed";
            case aauto::AapErrc::ProtobufParseError:     return "protobuf parse error";
            case aauto::AapErrc::SessionTerminated:      return "session terminated";
            case aauto::AapErrc::ByeByeReceived:         return "bye-bye received";
            case aauto::AapErrc::InternalError:          return "internal error";
        }
        return "unknown AAP error";
    }
};

const AapErrorCategory kAapCategory{};

} // anonymous namespace

namespace aauto {

const std::error_category& aap_category() noexcept {
    return kAapCategory;
}

} // namespace aauto
