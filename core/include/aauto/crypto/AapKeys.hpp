#pragma once

#include <string>

namespace aauto::crypto {

// Reference HU certificate and private key for AAP mutual authentication.
// These are the well-known keys from the android-auto-headunit reference.
extern const std::string kAapCertificate;
extern const std::string kAapPrivateKey;

} // namespace aauto::crypto
