#define LOG_TAG "OpenSslCrypto"

#include "OpenSslCryptoStrategy.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtocolConstants.hpp"

#include <openssl/err.h>

namespace aauto::impl {

OpenSslCryptoStrategy::OpenSslCryptoStrategy(const crypto::CryptoConfig& config)
    : config_(config) {
    if (!init_ssl_context(config)) {
        AA_LOG_E("failed to initialize SSL context");
    }
    init_ssl();
}

OpenSslCryptoStrategy::~OpenSslCryptoStrategy() = default;

bool OpenSslCryptoStrategy::init_ssl_context(const crypto::CryptoConfig& config) {
    const SSL_METHOD* method = SSLv23_server_method();
    ctx_.reset(SSL_CTX_new(method));
    if (!ctx_) {
        AA_LOG_E("SSL_CTX_new failed");
        return false;
    }

    // Disable certificate verification (AAP uses custom cert exchange)
    SSL_CTX_set_verify(ctx_.get(), SSL_VERIFY_NONE, nullptr);

    // Load HU certificate
    if (!config.cert_pem_path.empty()) {
        if (SSL_CTX_use_certificate_file(ctx_.get(),
                config.cert_pem_path.c_str(), SSL_FILETYPE_PEM) != 1) {
            AA_LOG_E("failed to load certificate: %s",
                     config.cert_pem_path.c_str());
            return false;
        }
    }

    // Load HU private key
    if (!config.key_pem_path.empty()) {
        if (SSL_CTX_use_PrivateKey_file(ctx_.get(),
                config.key_pem_path.c_str(), SSL_FILETYPE_PEM) != 1) {
            AA_LOG_E("failed to load private key: %s",
                     config.key_pem_path.c_str());
            return false;
        }
    }

    return true;
}

void OpenSslCryptoStrategy::init_ssl() {
    ssl_.reset(SSL_new(ctx_.get()));
    if (!ssl_) {
        AA_LOG_E("SSL_new failed");
        return;
    }

    // Memory BIOs for non-blocking I/O
    read_bio_ = BIO_new(BIO_s_mem());
    write_bio_ = BIO_new(BIO_s_mem());
    BIO_set_nbio(read_bio_, 1);
    BIO_set_nbio(write_bio_, 1);

    // SSL takes ownership of the BIOs
    SSL_set_bio(ssl_.get(), read_bio_, write_bio_);

    // HU acts as TLS server
    SSL_set_accept_state(ssl_.get());

    established_ = false;
}

void OpenSslCryptoStrategy::handshake_step(const uint8_t* input_data,
                                           std::size_t input_size,
                                           crypto::HandshakeStepHandler handler) {
    if (!ssl_) {
        handler(make_error_code(AapErrc::SslHandshakeFailed), {{}, false});
        return;
    }

    // Feed incoming handshake data to SSL via read_bio
    if (input_data && input_size > 0) {
        BIO_write(read_bio_, input_data, static_cast<int>(input_size));
    }

    // Attempt handshake
    int ret = SSL_do_handshake(ssl_.get());
    int err = SSL_get_error(ssl_.get(), ret);

    // Read any output data SSL wants to send
    auto output = read_bio_output();

    if (ret == 1) {
        // Handshake complete
        established_ = true;
        AA_LOG_I("SSL handshake complete");
        handler({}, crypto::HandshakeResult{std::move(output), true});
    } else if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        // Handshake in progress, need more data
        handler({}, crypto::HandshakeResult{std::move(output), false});
    } else {
        // Error
        unsigned long ossl_err = ERR_get_error();
        char buf[256];
        ERR_error_string_n(ossl_err, buf, sizeof(buf));
        AA_LOG_E("SSL handshake error: %s", buf);
        handler(make_error_code(AapErrc::SslHandshakeFailed), {{}, false});
    }
}

void OpenSslCryptoStrategy::encrypt(const uint8_t* plaintext, std::size_t size,
                                    crypto::CryptoHandler handler) {
    if (!established_ || !ssl_) {
        handler(make_error_code(AapErrc::InternalError), {});
        return;
    }

    int written = SSL_write(ssl_.get(), plaintext, static_cast<int>(size));
    if (written <= 0) {
        AA_LOG_E("SSL_write failed");
        handler(make_error_code(AapErrc::InternalError), {});
        return;
    }

    auto ciphertext = read_bio_output();
    handler({}, std::move(ciphertext));
}

void OpenSslCryptoStrategy::decrypt(const uint8_t* ciphertext, std::size_t size,
                                    crypto::CryptoHandler handler) {
    if (!established_ || !ssl_) {
        handler(make_error_code(AapErrc::DecryptionFailed), {});
        return;
    }

    // Feed ciphertext into read_bio
    BIO_write(read_bio_, ciphertext, static_cast<int>(size));

    // Read decrypted plaintext
    std::vector<uint8_t> plaintext(size + 256);
    int read_bytes = SSL_read(ssl_.get(), plaintext.data(),
                              static_cast<int>(plaintext.size()));
    if (read_bytes <= 0) {
        int err = SSL_get_error(ssl_.get(), read_bytes);
        if (err == SSL_ERROR_WANT_READ) {
            // Need more data, return empty
            handler({}, {});
            return;
        }
        AA_LOG_E("SSL_read failed, error=%d", err);
        handler(make_error_code(AapErrc::DecryptionFailed), {});
        return;
    }

    plaintext.resize(static_cast<std::size_t>(read_bytes));
    handler({}, std::move(plaintext));
}

bool OpenSslCryptoStrategy::is_established() const {
    return established_;
}

void OpenSslCryptoStrategy::reset() {
    ssl_.reset();
    read_bio_ = nullptr;
    write_bio_ = nullptr;
    established_ = false;
    init_ssl();
}

std::vector<uint8_t> OpenSslCryptoStrategy::read_bio_output() {
    std::vector<uint8_t> output;
    char buf[4096];
    int pending = BIO_ctrl_pending(write_bio_);
    while (pending > 0) {
        int n = BIO_read(write_bio_, buf,
                         std::min(pending, static_cast<int>(sizeof(buf))));
        if (n > 0) {
            output.insert(output.end(), buf, buf + n);
        }
        pending = BIO_ctrl_pending(write_bio_);
    }
    return output;
}

} // namespace aauto::impl
