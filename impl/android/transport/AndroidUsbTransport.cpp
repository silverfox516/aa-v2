#define LOG_TAG "AA.UsbTransport"

#include "AndroidUsbTransport.hpp"
#include "aauto/utils/Logger.hpp"

#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace aauto::impl {

AndroidUsbTransport::AndroidUsbTransport(asio::any_io_executor executor,
                                         int fd, int ep_in, int ep_out)
    : executor_(executor)
    , fd_(fd)
    , ep_in_(ep_in)
    , ep_out_(ep_out) {
    AA_LOG_I("USB transport created: fd=%d ep_in=0x%02x ep_out=0x%02x",
             fd_, ep_in_, ep_out_);
    read_thread_ = std::thread([this] { read_loop(); });
}

AndroidUsbTransport::~AndroidUsbTransport() {
    close();
}

void AndroidUsbTransport::async_read(asio::mutable_buffer buffer,
                                     transport::ReadHandler handler) {
    if (!open_) {
        asio::post(executor_, [handler] {
            handler(asio::error::operation_aborted, 0);
        });
        return;
    }

    {
        std::lock_guard<std::mutex> lock(read_mutex_);
        pending_read_buf_ = buffer;
        pending_read_handler_ = std::move(handler);
        read_requested_ = true;
    }
    read_cv_.notify_one();
}

void AndroidUsbTransport::async_write(asio::const_buffer buffer,
                                      transport::WriteHandler handler) {
    if (!open_) {
        asio::post(executor_, [handler] {
            handler(asio::error::operation_aborted, 0);
        });
        return;
    }

    struct usbdevfs_bulktransfer bulk{};
    bulk.ep = ep_out_;
    bulk.len = static_cast<unsigned int>(buffer.size());
    bulk.timeout = 1000;
    bulk.data = const_cast<void*>(buffer.data());

    int n = ::ioctl(fd_, USBDEVFS_BULK, &bulk);

    auto exec = executor_;
    if (n < 0) {
        int err = errno;
        AA_LOG_E("write failed: %s (errno=%d, ep=0x%02x, len=%u)",
                 strerror(err), err, ep_out_, bulk.len);
        asio::post(exec, [handler, err] {
            handler(std::error_code(err, std::system_category()), 0);
        });
    } else {
        // write complete
        std::size_t written = static_cast<std::size_t>(n);
        asio::post(exec, [handler, written] {
            handler({}, written);
        });
    }
}

void AndroidUsbTransport::close() {
    bool expected = true;
    if (!open_.compare_exchange_strong(expected, false)) {
        return;
    }

    AA_LOG_I("USB transport closing");

    // Wake read thread
    read_cv_.notify_one();

    if (read_thread_.joinable()) {
        read_thread_.join();
    }

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }

    // Cancel any pending read
    transport::ReadHandler handler;
    {
        std::lock_guard<std::mutex> lock(read_mutex_);
        handler = std::move(pending_read_handler_);
        pending_read_handler_ = nullptr;
        read_requested_ = false;
    }
    if (handler) {
        asio::post(executor_, [handler] {
            handler(asio::error::operation_aborted, 0);
        });
    }

    AA_LOG_I("USB transport closed");
}

bool AndroidUsbTransport::is_open() const {
    return open_;
}

asio::any_io_executor AndroidUsbTransport::get_executor() {
    return executor_;
}

void AndroidUsbTransport::read_loop() {
    AA_LOG_D("read thread started");

    while (open_) {
        // Wait for a read request
        {
            std::unique_lock<std::mutex> lock(read_mutex_);
            read_cv_.wait(lock, [this] {
                return read_requested_ || !open_;
            });
            if (!open_) break;
        }

        // Perform blocking bulk read
        std::size_t buf_size;
        void* buf_data;
        {
            std::lock_guard<std::mutex> lock(read_mutex_);
            buf_size = pending_read_buf_.size();
            buf_data = pending_read_buf_.data();
        }

        struct usbdevfs_bulktransfer bulk{};
        bulk.ep = ep_in_;
        bulk.len = static_cast<unsigned int>(buf_size);
        bulk.timeout = kBulkTimeoutMs;
        bulk.data = buf_data;

        int n = ::ioctl(fd_, USBDEVFS_BULK, &bulk);

        if (n < 0) {
            int err = errno;
            if (err == ETIMEDOUT) {
                // Timeout is normal — retry
                continue;
            }
            if (!open_) break;

            AA_LOG_E("read ioctl failed: %s (errno=%d)", strerror(err), err);

            transport::ReadHandler handler;
            {
                std::lock_guard<std::mutex> lock(read_mutex_);
                handler = std::move(pending_read_handler_);
                pending_read_handler_ = nullptr;
                read_requested_ = false;
            }
            if (handler) {
                asio::post(executor_, [handler, err] {
                    handler(std::error_code(err, std::system_category()), 0);
                });
            }
            continue;
        }

        // Success — deliver data
        std::size_t bytes_read = static_cast<std::size_t>(n);
        transport::ReadHandler handler;
        {
            std::lock_guard<std::mutex> lock(read_mutex_);
            handler = std::move(pending_read_handler_);
            pending_read_handler_ = nullptr;
            read_requested_ = false;
        }
        if (handler) {
            asio::post(executor_, [handler, bytes_read] {
                handler({}, bytes_read);
            });
        }
    }

    AA_LOG_D("read thread exited");
}

} // namespace aauto::impl
