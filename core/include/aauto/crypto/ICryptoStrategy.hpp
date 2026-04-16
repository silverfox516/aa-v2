#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace aauto::crypto {

struct HandshakeResult {
    std::vector<uint8_t> output_bytes;  // bytes to send to peer (may be empty)
    bool                 complete;       // true = handshake finished
};

using HandshakeStepHandler = std::function<void(const std::error_code& ec,
                                                HandshakeResult result)>;
using CryptoHandler = std::function<void(const std::error_code& ec,
                                         std::vector<uint8_t> output)>;

/// Internal port: SSL/TLS handshake + encrypt/decrypt.
///
/// Lifecycle:
///   1. Constructed with certificate/key material
///   2. handshake_step() called repeatedly until complete==true
///   3. After handshake: encrypt()/decrypt() for payload protection
///   4. reset() to prepare for new handshake (session restart)
///
/// Threading: all methods called from session strand.
class ICryptoStrategy {
public:
    virtual ~ICryptoStrategy() = default;

    /// Feed handshake bytes from peer (empty for initial step).
    /// Handler called with bytes to send back and completion status.
    virtual void handshake_step(const uint8_t* input_data,
                                std::size_t input_size,
                                HandshakeStepHandler handler) = 0;

    /// Encrypt plaintext payload.
    virtual void encrypt(const uint8_t* plaintext, std::size_t size,
                         CryptoHandler handler) = 0;

    /// Decrypt ciphertext payload.
    virtual void decrypt(const uint8_t* ciphertext, std::size_t size,
                         CryptoHandler handler) = 0;

    /// True after handshake completes.
    virtual bool is_established() const = 0;

    /// Reset for reuse with a new connection.
    virtual void reset() = 0;
};

struct CryptoConfig {
    std::string cert_pem_path;  // HU certificate (PEM)
    std::string key_pem_path;   // HU private key (PEM)
    // Empty = use build-time injected defaults
};

} // namespace aauto::crypto
