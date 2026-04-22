#define LOG_TAG "AA.TcpTransport"

#include "AndroidTcpTransport.hpp"
#include "aauto/utils/Logger.hpp"

namespace aauto::impl {

AndroidTcpTransport::AndroidTcpTransport(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)) {
    auto ep = socket_.remote_endpoint();
    AA_LOG_I("connected: %s:%d", ep.address().to_string().c_str(), ep.port());
}

AndroidTcpTransport::~AndroidTcpTransport() {
    close();
}

void AndroidTcpTransport::async_read(asio::mutable_buffer buffer,
                                     transport::ReadHandler handler) {
    if (!open_) {
        asio::post(socket_.get_executor(),
            [h = std::move(handler)] {
                h(asio::error::operation_aborted, 0);
            });
        return;
    }

    AA_LOG_D("async_read_some: up to %zu bytes", asio::buffer_size(buffer));
    socket_.async_read_some(buffer,
        [this, h = std::move(handler)](const std::error_code& ec,
                                       std::size_t bytes) {
            if (ec) {
                AA_LOG_W("read error: %s", ec.message().c_str());
                open_ = false;
            } else {
                AA_LOG_D("read: %zu bytes", bytes);
            }
            h(ec, bytes);
        });
}

void AndroidTcpTransport::async_write(asio::const_buffer buffer,
                                      transport::WriteHandler handler) {
    if (!open_) {
        asio::post(socket_.get_executor(),
            [h = std::move(handler)] {
                h(asio::error::operation_aborted, 0);
            });
        return;
    }

    auto size = asio::buffer_size(buffer);
    AA_LOG_D("async_write: %zu bytes", size);
    asio::async_write(socket_, buffer,
        [this, h = std::move(handler)](const std::error_code& ec,
                                       std::size_t bytes) {
            if (ec) {
                AA_LOG_W("write error: %s", ec.message().c_str());
                open_ = false;
            } else {
                AA_LOG_D("write complete: %zu bytes", bytes);
            }
            h(ec, bytes);
        });
}

void AndroidTcpTransport::close() {
    bool expected = true;
    if (!open_.compare_exchange_strong(expected, false)) return;

    std::error_code ec;
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    AA_LOG_I("closed");
}

bool AndroidTcpTransport::is_open() const {
    return open_;
}

asio::any_io_executor AndroidTcpTransport::get_executor() {
    return socket_.get_executor();
}

} // namespace aauto::impl
