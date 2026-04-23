#pragma once

#include "aauto/session/SessionState.hpp"
#include "aauto/session/Framer.hpp"
#include "aauto/session/HandshakeCoordinator.hpp"
#include "aauto/session/InboundMessageAssembler.hpp"
#include "aauto/session/OutboundMessageEncoder.hpp"
#include "aauto/transport/ITransport.hpp"
#include "aauto/crypto/ICryptoStrategy.hpp"
#include "aauto/service/IService.hpp"

#include <asio.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <system_error>
#include <vector>

namespace aauto::session {

/// Session -> Engine notification interface.
class ISessionObserver {
public:
    virtual ~ISessionObserver() = default;
    virtual void on_session_state_changed(uint32_t session_id,
                                          SessionState state) = 0;
    virtual void on_session_error(uint32_t session_id,
                                  const std::error_code& ec) = 0;
};

struct SessionConfig {
    uint32_t session_id;
    uint32_t ssl_handshake_timeout_ms    = 10000;
    uint32_t version_exchange_timeout_ms = 5000;
    uint32_t byebye_timeout_ms           = 3000;
};

/// One AAP session with one phone.
///
/// Owns: Framer, strand, timers.
/// References: ITransport, ICryptoStrategy, services, observer.
/// All async ops serialized via strand.
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(asio::any_io_executor executor,
            SessionConfig config,
            std::shared_ptr<transport::ITransport> transport,
            std::shared_ptr<crypto::ICryptoStrategy> crypto,
            ISessionObserver* observer);
    ~Session();

    void start();
    void stop();

    void register_service(uint8_t channel_id,
                          std::shared_ptr<service::IService> svc);

    void send_message(uint8_t channel_id, uint16_t message_type,
                      const std::vector<uint8_t>& payload);
    void send_raw(uint8_t channel_id, uint16_t message_type,
                  const uint8_t* data, std::size_t size);

    /// Forward native window to video service(s).
    void set_video_surface(void* native_window);

    /// Forward touch event to input service(s).
    void send_touch_event(int32_t x, int32_t y, int32_t action);

    SessionState state() const;
    uint32_t session_id() const { return config_.session_id; }

private:
    bool is_handshake_state() const;
    bool handle_handshake_message(uint16_t msg_type,
                                  const std::vector<uint8_t>& payload);
    void begin_disconnect();
    void close_transport_and_services();
    void complete_disconnect();

    // Read loop
    void start_read();
    void on_read_complete(const std::error_code& ec, std::size_t bytes);
    void dispatch_decrypted(uint8_t channel_id, uint16_t msg_type,
                            std::vector<uint8_t> payload);

    // Write queue
    void enqueue_write(std::vector<uint8_t> wire_data);
    void do_write_next();
    void on_write_complete(const std::error_code& ec, std::size_t bytes);

    // State machine
    void transition_to(SessionState new_state);
    void handle_error(const std::error_code& ec);
    void start_state_timer(uint32_t timeout_ms);
    void on_state_timeout();

    // Handshake (order: VERSION → SSL → AUTH)
    void begin_version_exchange();
    void begin_ssl_handshake();
    void on_ssl_data_received(const uint8_t* data, std::size_t size);
    void on_ssl_complete();
    void send_version_request();
    void on_version_response(const std::vector<uint8_t>& payload);

    // Members
    asio::strand<asio::any_io_executor>      strand_;
    SessionConfig                             config_;
    std::shared_ptr<transport::ITransport>    transport_;
    std::shared_ptr<crypto::ICryptoStrategy>  crypto_;
    ISessionObserver*                         observer_;
    OutboundMessageEncoder                    outbound_encoder_;
    InboundMessageAssembler                   inbound_assembler_;
    HandshakeCoordinator                      handshake_coordinator_;
    SessionState                              state_ = SessionState::Idle;

    static constexpr std::size_t kMaxWriteQueueSize = 256;

    std::array<uint8_t, 16384>                read_buffer_;
    std::queue<std::vector<uint8_t>>          write_queue_;
    bool                                      write_in_progress_ = false;

    std::map<uint8_t, std::shared_ptr<service::IService>> services_;

    asio::steady_timer                        state_timer_;
};

} // namespace aauto::session
