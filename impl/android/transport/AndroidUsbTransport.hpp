#pragma once

#include "aauto/transport/ITransport.hpp"

#include <asio.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace aauto::impl {

/// ITransport implementation for Android USB host mode (AOA).
///
/// Uses ioctl(USBDEVFS_BULK) for I/O on a usbdevfs file descriptor.
///
/// Threading model:
///   - Dedicated read thread: blocking ioctl reads, posts completions to executor
///   - Dedicated write thread: blocking ioctl writes, posts completions to executor
///   - Neither thread blocks the asio strand
class AndroidUsbTransport : public transport::ITransport {
public:
    AndroidUsbTransport(asio::any_io_executor executor,
                        int fd, int ep_in, int ep_out);
    ~AndroidUsbTransport();

    void async_read(asio::mutable_buffer buffer,
                    transport::ReadHandler handler) override;
    void async_write(asio::const_buffer buffer,
                     transport::WriteHandler handler) override;
    void close() override;
    bool is_open() const override;
    asio::any_io_executor get_executor() override;

private:
    void read_loop();
    void write_loop();

    asio::any_io_executor executor_;
    int fd_;
    int ep_in_;
    int ep_out_;
    std::atomic<bool> open_{true};

    // Read thread
    std::thread read_thread_;
    std::mutex read_mutex_;
    std::condition_variable read_cv_;
    asio::mutable_buffer pending_read_buf_;
    transport::ReadHandler pending_read_handler_;
    bool read_requested_ = false;

    // Write thread
    std::thread write_thread_;
    std::mutex write_mutex_;
    std::condition_variable write_cv_;
    std::vector<uint8_t> pending_write_data_;
    transport::WriteHandler pending_write_handler_;
    bool write_requested_ = false;

    static constexpr int kBulkTimeoutMs = 500;
};

} // namespace aauto::impl
