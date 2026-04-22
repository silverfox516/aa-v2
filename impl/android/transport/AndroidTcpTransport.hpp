#pragma once

#include "aauto/transport/ITransport.hpp"

#include <asio.hpp>
#include <atomic>
#include <memory>

namespace aauto::impl {

/// ITransport implementation for TCP sockets (wireless Android Auto).
///
/// Uses ASIO's native async TCP I/O — no dedicated threads needed
/// (unlike USB which requires blocking ioctl calls).
///
/// Constructed with an already-connected socket (from accept()).
class AndroidTcpTransport : public transport::ITransport {
public:
    AndroidTcpTransport(asio::ip::tcp::socket socket);
    ~AndroidTcpTransport();

    void async_read(asio::mutable_buffer buffer,
                    transport::ReadHandler handler) override;
    void async_write(asio::const_buffer buffer,
                     transport::WriteHandler handler) override;
    void close() override;
    bool is_open() const override;
    asio::any_io_executor get_executor() override;

private:
    asio::ip::tcp::socket socket_;
    std::atomic<bool> open_{true};
};

} // namespace aauto::impl
