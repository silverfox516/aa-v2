#pragma once

#include "aauto/session/SessionState.hpp"
#include "aauto/session/Framer.hpp"
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

namespace aauto::engine { struct HeadunitConfig; }

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
    uint32_t service_discovery_timeout_ms = 5000;
    uint32_t channel_setup_timeout_ms    = 10000;
    uint32_t byebye_timeout_ms           = 3000;
    uint32_t ping_interval_ms            = 5000;
    uint32_t ping_timeout_ms             = 10000;
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
            const engine::HeadunitConfig& hu_config,
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

    SessionState state() const;
    uint32_t session_id() const { return config_.session_id; }

private:
    // Read loop
    void start_read();
    void on_read_complete(const std::error_code& ec, std::size_t bytes);
    void dispatch_frame(AapFrame frame);
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

    // Handshake
    void begin_ssl_handshake();
    void on_ssl_data_received(const uint8_t* data, std::size_t size);
    void on_ssl_complete();
    void send_version_request();
    void on_version_response(const std::vector<uint8_t>& payload);
    void on_auth_complete(const std::vector<uint8_t>& payload);
    void send_service_discovery_request();
    void on_service_discovery_response(const std::vector<uint8_t>& payload);
    void open_channels();
    void on_channel_open_response(const std::vector<uint8_t>& payload);

    // Ping
    void start_ping_timer();
    void send_ping();
    void on_ping_response(const std::vector<uint8_t>& payload);
    void on_ping_timeout();

    // Control message dispatch
    void handle_control_message(uint16_t msg_type,
                                const std::vector<uint8_t>& payload);

    // Discovered services from phone (populated during handshake)
    struct DiscoveredService {
        int32_t  service_id;
        uint8_t  assigned_channel;
    };
    std::vector<DiscoveredService> discovered_services_;

    // Members
    asio::strand<asio::any_io_executor>      strand_;
    SessionConfig                             config_;
    const engine::HeadunitConfig&             hu_config_;
    std::shared_ptr<transport::ITransport>    transport_;
    std::shared_ptr<crypto::ICryptoStrategy>  crypto_;
    ISessionObserver*                         observer_;
    Framer                                    framer_;
    SessionState                              state_ = SessionState::Idle;

    std::array<uint8_t, 16384>                read_buffer_;
    std::queue<std::vector<uint8_t>>          write_queue_;
    bool                                      write_in_progress_ = false;

    std::map<uint8_t, std::shared_ptr<service::IService>> services_;

    asio::steady_timer                        state_timer_;
    asio::steady_timer                        ping_timer_;
    asio::steady_timer                        ping_timeout_timer_;

    std::map<int32_t, uint8_t>                service_id_to_channel_;
    uint8_t                                   next_channel_id_ = 1;
    int                                       pending_channel_opens_ = 0;
};

} // namespace aauto::session
