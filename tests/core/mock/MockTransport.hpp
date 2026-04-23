#pragma once

#include "aauto/transport/ITransport.hpp"

#include <deque>
#include <optional>
#include <vector>

namespace aauto::test {

/// Mock transport for unit testing.
/// Feeds scripted data on async_read, records data from async_write.
class MockTransport : public transport::ITransport {
public:
    explicit MockTransport(asio::any_io_executor executor)
        : executor_(executor), open_(true) {}

    void async_read(asio::mutable_buffer buffer,
                    transport::ReadHandler handler) override {
        if (pending_read_error_) {
            auto ec = *pending_read_error_;
            pending_read_error_.reset();
            auto h = std::move(handler);
            asio::post(executor_, [h, ec] {
                h(ec, 0);
            });
            return;
        }

        if (!open_ || read_queue_.empty()) {
            // Store pending read for later fulfillment
            pending_read_buffer_ = buffer;
            pending_read_handler_ = std::move(handler);
            return;
        }

        auto& front = read_queue_.front();
        std::size_t to_copy = std::min(front.size(), buffer.size());
        std::memcpy(buffer.data(), front.data(), to_copy);
        front.erase(front.begin(), front.begin() + to_copy);
        if (front.empty()) {
            read_queue_.pop_front();
        }

        auto h = std::move(handler);
        asio::post(executor_, [h, to_copy] {
            h({}, to_copy);
        });
    }

    void async_write(asio::const_buffer buffer,
                     transport::WriteHandler handler) override {
        if (!open_) {
            asio::post(executor_, [handler] {
                handler(asio::error::operation_aborted, 0);
            });
            return;
        }

        auto* data = static_cast<const uint8_t*>(buffer.data());
        written_data_.insert(written_data_.end(), data, data + buffer.size());

        auto sz = buffer.size();
        asio::post(executor_, [handler, sz] {
            handler({}, sz);
        });
    }

    void close() override {
        open_ = false;
        if (pending_read_handler_) {
            auto h = std::move(pending_read_handler_);
            pending_read_handler_ = nullptr;
            asio::post(executor_, [h] {
                h(asio::error::operation_aborted, 0);
            });
        }
    }

    bool is_open() const override { return open_; }

    asio::any_io_executor get_executor() override { return executor_; }

    // === Test helpers ===

    /// Enqueue data to be returned by the next async_read.
    void feed_read(const std::vector<uint8_t>& data) {
        read_queue_.push_back(data);

        // If there's a pending read, fulfill it now
        if (pending_read_handler_ && !read_queue_.empty()) {
            auto& front = read_queue_.front();
            std::size_t to_copy = std::min(front.size(),
                                           pending_read_buffer_.size());
            std::memcpy(pending_read_buffer_.data(), front.data(), to_copy);
            front.erase(front.begin(), front.begin() + to_copy);
            if (front.empty()) {
                read_queue_.pop_front();
            }

            auto h = std::move(pending_read_handler_);
            pending_read_handler_ = nullptr;
            asio::post(executor_, [h, to_copy] {
                h({}, to_copy);
            });
        }
    }

    /// Get all data written by Session.
    const std::vector<uint8_t>& get_written_data() const {
        return written_data_;
    }

    void clear_written_data() { written_data_.clear(); }

    /// Inject a read error on the pending async_read (simulates USB disconnect).
    void inject_read_error(std::error_code ec) {
        if (pending_read_handler_) {
            auto h = std::move(pending_read_handler_);
            pending_read_handler_ = nullptr;
            asio::post(executor_, [h, ec] {
                h(ec, 0);
            });
            return;
        }

        pending_read_error_ = ec;
    }

private:
    asio::any_io_executor executor_;
    bool open_;

    std::deque<std::vector<uint8_t>> read_queue_;
    std::vector<uint8_t> written_data_;

    // Pending read state
    asio::mutable_buffer pending_read_buffer_;
    transport::ReadHandler pending_read_handler_;
    std::optional<std::error_code> pending_read_error_;
};

} // namespace aauto::test
