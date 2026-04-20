#pragma once

#include "aauto/crypto/ICryptoStrategy.hpp"

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>

#include <memory>
#include <string>
#include <vector>

namespace aauto::impl {

/// ICryptoStrategy implementation using OpenSSL.
/// Performs SSL/TLS handshake as client (HU acts as TLS client).
/// After handshake, encrypts/decrypts payload with the negotiated session keys.
class OpenSslCryptoStrategy : public crypto::ICryptoStrategy {
public:
    explicit OpenSslCryptoStrategy(const crypto::CryptoConfig& config);
    ~OpenSslCryptoStrategy();

    void handshake_step(const uint8_t* input_data, std::size_t input_size,
                        crypto::HandshakeStepHandler handler) override;

    void encrypt(const uint8_t* plaintext, std::size_t size,
                 crypto::CryptoHandler handler) override;

    void decrypt(const uint8_t* ciphertext, std::size_t size,
                 crypto::CryptoHandler handler) override;

    bool is_established() const override;
    void reset() override;

private:
    bool init_ssl_context(const crypto::CryptoConfig& config);
    void init_ssl();
    std::vector<uint8_t> read_bio_output();

    struct SslDeleter {
        void operator()(SSL* s) const { if (s) SSL_free(s); }
    };
    struct SslCtxDeleter {
        void operator()(SSL_CTX* c) const { if (c) SSL_CTX_free(c); }
    };

    std::unique_ptr<SSL_CTX, SslCtxDeleter> ctx_;
    std::unique_ptr<SSL, SslDeleter> ssl_;
    BIO* read_bio_  = nullptr;  // owned by SSL
    BIO* write_bio_ = nullptr;  // owned by SSL
    bool established_ = false;
    crypto::CryptoConfig config_;
};

} // namespace aauto::impl
