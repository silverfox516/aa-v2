#define LOG_TAG "AA.BufferedTransport"

#include "aauto/transport/BufferedTransport.hpp"
#include "aauto/utils/Logger.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace aauto::transport {

BufferedTransport::BufferedTransport(std::shared_ptr<ITransport> underlying)
    : underlying_(std::move(underlying))
    , executor_(underlying_->get_executor()) {}

BufferedTransport::~BufferedTransport() {
    close();
}

void BufferedTransport::start() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (loop_started_ || closed_) return;
        loop_started_ = true;
    }
    issue_underlying_read();
}

// ===== ITransport =====

void BufferedTransport::async_read(asio::mutable_buffer buffer,
                                   ReadHandler handler) {
    std::unique_lock<std::mutex> lock(mu_);
    if (closed_) {
        lock.unlock();
        invoke_handler(std::move(handler),
                       asio::error::operation_aborted, {}, buffer);
        return;
    }

    // If a chunk is already buffered, deliver it immediately.
    if (!rx_queue_.empty()) {
        auto chunk = std::move(rx_queue_.front());
        rx_queue_.pop_front();
        lock.unlock();
        invoke_handler(std::move(handler), {}, std::move(chunk), buffer);
        return;
    }

    // If the underlying transport reported an error and there are no
    // queued chunks left, surface it now.
    if (deferred_error_) {
        std::error_code ec = deferred_error_;
        deferred_error_ = std::error_code{};
        lock.unlock();
        invoke_handler(std::move(handler), ec, {}, buffer);
        return;
    }

    // Otherwise park the handler until the next underlying read fires.
    pending_handler_ = std::move(handler);
    pending_dest_    = buffer;
}

void BufferedTransport::async_write(asio::const_buffer buffer,
                                    WriteHandler handler) {
    // TX is forwarded directly. Underlying transport already owns
    // its own write thread + queue (USB / TCP impls).
    underlying_->async_write(buffer, std::move(handler));
}

void BufferedTransport::close() {
    std::unique_lock<std::mutex> lock(mu_);
    if (closed_) return;
    closed_ = true;
    ReadHandler pending = std::move(pending_handler_);
    asio::mutable_buffer pending_dest = pending_dest_;
    pending_dest_ = asio::mutable_buffer{};
    rx_queue_.clear();
    lock.unlock();

    if (underlying_) underlying_->close();

    if (pending) {
        invoke_handler(std::move(pending),
                       asio::error::operation_aborted, {}, pending_dest);
    }
}

bool BufferedTransport::is_open() const {
    return underlying_ && underlying_->is_open();
}

asio::any_io_executor BufferedTransport::get_executor() {
    return executor_;
}

// ===== Internal =====

void BufferedTransport::issue_underlying_read() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (closed_) return;
        pending_read_.assign(kReadBufferSize, 0);
    }
    auto self = shared_from_this();
    underlying_->async_read(
        asio::buffer(pending_read_.data(), pending_read_.size()),
        [self](const std::error_code& ec, std::size_t bytes) {
            // Move pending_read_ out under lock to avoid races with close().
            std::vector<uint8_t> buf;
            {
                std::lock_guard<std::mutex> lock(self->mu_);
                buf = std::move(self->pending_read_);
            }
            self->on_underlying_read(ec, std::move(buf), bytes);
        });
}

void BufferedTransport::on_underlying_read(const std::error_code& ec,
                                           std::vector<uint8_t> buf,
                                           std::size_t bytes) {
    if (ec) {
        std::unique_lock<std::mutex> lock(mu_);
        if (closed_) return;
        ReadHandler pending = std::move(pending_handler_);
        asio::mutable_buffer pending_dest = pending_dest_;
        pending_dest_ = asio::mutable_buffer{};
        if (pending) {
            // No queued data, no point storing the error — surface now.
            lock.unlock();
            invoke_handler(std::move(pending), ec, {}, pending_dest);
            return;
        }
        // No pending caller — remember the error for the next async_read.
        deferred_error_ = ec;
        return;
    }

    // Trim to actual bytes received and route either to a parked
    // handler or onto the queue.
    buf.resize(bytes);

    std::unique_lock<std::mutex> lock(mu_);
    if (closed_) return;

    if (pending_handler_) {
        ReadHandler h = std::move(pending_handler_);
        asio::mutable_buffer dest = pending_dest_;
        pending_handler_ = nullptr;
        pending_dest_ = asio::mutable_buffer{};
        lock.unlock();
        invoke_handler(std::move(h), {}, std::move(buf), dest);
    } else {
        // No waiting reader — buffer the chunk.
        if (rx_queue_.size() >= kMaxQueuedReads) {
            AA_LOG_W("rx queue full (%zu), dropping oldest chunk",
                     rx_queue_.size());
            rx_queue_.pop_front();
        }
        rx_queue_.push_back(std::move(buf));
        lock.unlock();
    }

    // Always issue the next underlying read — this is the whole point
    // of BufferedTransport: keep the transport reading regardless of
    // upper-layer processing speed.
    issue_underlying_read();
}

void BufferedTransport::invoke_handler(ReadHandler handler,
                                       std::error_code ec,
                                       std::vector<uint8_t> data,
                                       asio::mutable_buffer dest) {
    // Copy buffered chunk into the caller's destination buffer.
    std::size_t to_copy = 0;
    if (!ec && !data.empty()) {
        to_copy = std::min(asio::buffer_size(dest), data.size());
        if (to_copy > 0) {
            std::memcpy(dest.data(), data.data(), to_copy);
        }
        if (to_copy < data.size()) {
            // Caller's buffer was too small. Push the remainder back so
            // the next async_read picks it up. Must hold the lock to
            // touch rx_queue_.
            std::vector<uint8_t> leftover(
                data.begin() + static_cast<std::ptrdiff_t>(to_copy),
                data.end());
            std::lock_guard<std::mutex> lock(mu_);
            rx_queue_.push_front(std::move(leftover));
            AA_LOG_W("dest buffer smaller than chunk (%zu < %zu)",
                     to_copy, data.size());
        }
    }
    asio::post(executor_,
        [handler = std::move(handler), ec, to_copy]() {
            handler(ec, to_copy);
        });
}

} // namespace aauto::transport
