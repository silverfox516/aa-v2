#define LOG_TAG "AA.TcpTransport"

#include "AndroidTcpTransport.hpp"
#include "aauto/utils/Logger.hpp"

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>

namespace aauto::impl {

AndroidTcpTransport::AndroidTcpTransport(asio::any_io_executor executor, int fd)
    : executor_(executor), fd_(fd) {

    // Disable Nagle's algorithm for low-latency control messages
    int flag = 1;
    ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Log remote peer
    struct sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    if (::getpeername(fd_, reinterpret_cast<struct sockaddr*>(&peer), &peer_len) == 0) {
        char ip[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        AA_LOG_I("connected: %s:%d (TCP_NODELAY)", ip, ntohs(peer.sin_port));
    }

    read_thread_ = std::thread(&AndroidTcpTransport::read_loop, this);
    write_thread_ = std::thread(&AndroidTcpTransport::write_loop, this);
}

AndroidTcpTransport::~AndroidTcpTransport() {
    close();
}

void AndroidTcpTransport::async_read(asio::mutable_buffer buffer,
                                     transport::ReadHandler handler) {
    std::lock_guard<std::mutex> lock(read_mutex_);
    if (!open_) {
        asio::post(executor_,
            [h = std::move(handler)] { h(asio::error::operation_aborted, 0); });
        return;
    }
    pending_read_buf_ = buffer;
    pending_read_handler_ = std::move(handler);
    read_requested_ = true;
    read_cv_.notify_one();
}

void AndroidTcpTransport::async_write(asio::const_buffer buffer,
                                      transport::WriteHandler handler) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (!open_) {
        asio::post(executor_,
            [h = std::move(handler)] { h(asio::error::operation_aborted, 0); });
        return;
    }
    auto* src = static_cast<const uint8_t*>(buffer.data());
    pending_write_data_.assign(src, src + buffer.size());
    pending_write_handler_ = std::move(handler);
    write_requested_ = true;
    write_cv_.notify_one();
}

void AndroidTcpTransport::close() {
    bool expected = true;
    if (!open_.compare_exchange_strong(expected, false)) return;

    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;

    read_cv_.notify_all();
    write_cv_.notify_all();

    if (read_thread_.joinable()) read_thread_.join();
    if (write_thread_.joinable()) write_thread_.join();

    // Fire pending handlers with operation_aborted
    if (pending_read_handler_) {
        auto h = std::move(pending_read_handler_);
        asio::post(executor_, [h = std::move(h)] {
            h(asio::error::operation_aborted, 0);
        });
    }
    if (pending_write_handler_) {
        auto h = std::move(pending_write_handler_);
        asio::post(executor_, [h = std::move(h)] {
            h(asio::error::operation_aborted, 0);
        });
    }

    AA_LOG_I("closed");
}

bool AndroidTcpTransport::is_open() const {
    return open_;
}

asio::any_io_executor AndroidTcpTransport::get_executor() {
    return executor_;
}

void AndroidTcpTransport::read_loop() {
    while (open_) {
        std::unique_lock<std::mutex> lock(read_mutex_);
        read_cv_.wait(lock, [this] { return read_requested_ || !open_; });
        if (!open_) break;

        auto buf = pending_read_buf_;
        auto handler = std::move(pending_read_handler_);
        read_requested_ = false;
        lock.unlock();

        ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);

        if (n > 0) {
            auto bytes = static_cast<std::size_t>(n);
            asio::post(executor_,
                [h = std::move(handler), bytes] { h({}, bytes); });
        } else if (n == 0) {
            AA_LOG_I("peer closed connection");
            open_ = false;
            asio::post(executor_,
                [h = std::move(handler)] { h(asio::error::eof, 0); });
        } else {
            if (!open_) break;
            AA_LOG_W("recv error: %s", strerror(errno));
            open_ = false;
            asio::post(executor_,
                [h = std::move(handler)] {
                    h(std::make_error_code(std::errc::io_error), 0);
                });
        }
    }
}

void AndroidTcpTransport::write_loop() {
    while (open_) {
        std::unique_lock<std::mutex> lock(write_mutex_);
        write_cv_.wait(lock, [this] { return write_requested_ || !open_; });
        if (!open_) break;

        auto data = std::move(pending_write_data_);
        auto handler = std::move(pending_write_handler_);
        write_requested_ = false;
        lock.unlock();

        const uint8_t* ptr = data.data();
        size_t remaining = data.size();
        bool ok = true;

        while (remaining > 0 && open_) {
            ssize_t n = ::send(fd_, ptr, remaining, MSG_NOSIGNAL);
            if (n > 0) {
                ptr += n;
                remaining -= static_cast<size_t>(n);
            } else if (n == 0) {
                ok = false;
                break;
            } else {
                if (errno == EINTR) continue;
                AA_LOG_W("send error: %s", strerror(errno));
                ok = false;
                break;
            }
        }

        if (ok) {
            auto bytes = data.size();
            asio::post(executor_,
                [h = std::move(handler), bytes] { h({}, bytes); });
        } else {
            open_ = false;
            asio::post(executor_,
                [h = std::move(handler)] {
                    h(std::make_error_code(std::errc::io_error), 0);
                });
        }
    }
}

} // namespace aauto::impl
