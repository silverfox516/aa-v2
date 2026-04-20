#pragma once

#include "aauto/transport/ITransport.hpp"

#include <asio.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace aauto::impl {

/// ITransport implementation for Android USB host mode (AOA).
///
/// Uses ioctl(USBDEVFS_BULK) for I/O on a usbdevfs file descriptor.
/// The fd is obtained from UsbDeviceConnection.getFileDescriptor() in Java.
///
/// Threading model:
///   - A dedicated read thread performs blocking ioctl reads
///   - Read completions are posted to the asio executor
///   - Writes are performed inline (ioctl write is fast, < 1ms)
///   - Write completions are posted to the asio executor
class AndroidUsbTransport : public transport::ITransport {
public:
    /// Construct from USB device fd and bulk endpoint addresses.
    /// fd: from UsbDeviceConnection.getFileDescriptor()
    /// ep_in: bulk IN endpoint address (e.g., 0x81)
    /// ep_out: bulk OUT endpoint address (e.g., 0x01)
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

    asio::any_io_executor executor_;
    int fd_;
    int ep_in_;
    int ep_out_;
    std::atomic<bool> open_{true};

    // Read thread state
    std::thread read_thread_;
    std::mutex read_mutex_;
    std::condition_variable read_cv_;
    asio::mutable_buffer pending_read_buf_;
    transport::ReadHandler pending_read_handler_;
    bool read_requested_ = false;

    static constexpr int kBulkTimeoutMs = 500;
};

} // namespace aauto::impl
