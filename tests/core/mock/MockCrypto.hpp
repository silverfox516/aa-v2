#pragma once

#include "aauto/crypto/ICryptoStrategy.hpp"

namespace aauto::test {

/// Passthrough crypto for testing. No actual encryption.
/// Handshake completes immediately on first step.
class MockCrypto : public crypto::ICryptoStrategy {
public:
    void handshake_step(const uint8_t* /*input_data*/,
                        std::size_t /*input_size*/,
                        crypto::HandshakeStepHandler handler) override {
        established_ = true;
        handler({}, crypto::HandshakeResult{{}, true});
    }

    void encrypt(const uint8_t* plaintext, std::size_t size,
                 crypto::CryptoHandler handler) override {
        // Passthrough: ciphertext == plaintext
        std::vector<uint8_t> out(plaintext, plaintext + size);
        handler({}, std::move(out));
    }

    void decrypt(const uint8_t* ciphertext, std::size_t size,
                 crypto::CryptoHandler handler) override {
        // Passthrough: plaintext == ciphertext
        std::vector<uint8_t> out(ciphertext, ciphertext + size);
        handler({}, std::move(out));
    }

    bool is_established() const override { return established_; }

    void reset() override { established_ = false; }

private:
    bool established_ = false;
};

} // namespace aauto::test
