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
    write_thread_ = std::thread([this] { write_loop(); });
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

    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        auto* p = static_cast<const uint8_t*>(buffer.data());
        pending_write_data_.assign(p, p + buffer.size());
        pending_write_handler_ = std::move(handler);
        write_requested_ = true;
    }
    write_cv_.notify_one();
}

void AndroidUsbTransport::close() {
    bool expected = true;
    if (!open_.compare_exchange_strong(expected, false)) {
        return;
    }

    AA_LOG_I("USB transport closing");

    read_cv_.notify_one();
    write_cv_.notify_one();

    if (read_thread_.joinable()) read_thread_.join();
    if (write_thread_.joinable()) write_thread_.join();

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }

    // Cancel pending handlers
    {
        std::lock_guard<std::mutex> lock(read_mutex_);
        if (pending_read_handler_) {
            auto h = std::move(pending_read_handler_);
            asio::post(executor_, [h] { h(asio::error::operation_aborted, 0); });
        }
    }
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        if (pending_write_handler_) {
            auto h = std::move(pending_write_handler_);
            asio::post(executor_, [h] { h(asio::error::operation_aborted, 0); });
        }
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
        {
            std::unique_lock<std::mutex> lock(read_mutex_);
            read_cv_.wait(lock, [this] {
                return read_requested_ || !open_;
            });
            if (!open_) break;
        }

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
            if (err == ETIMEDOUT) continue;
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
                auto exec = executor_;
                asio::post(exec, [handler, err] {
                    handler(std::error_code(err, std::system_category()), 0);
                });
            }
            continue;
        }

        std::size_t bytes_read = static_cast<std::size_t>(n);
        transport::ReadHandler handler;
        {
            std::lock_guard<std::mutex> lock(read_mutex_);
            handler = std::move(pending_read_handler_);
            pending_read_handler_ = nullptr;
            read_requested_ = false;
        }
        if (handler) {
            auto exec = executor_;
            asio::post(exec, [handler, bytes_read] {
                handler({}, bytes_read);
            });
        }
    }

    AA_LOG_D("read thread exited");
}

void AndroidUsbTransport::write_loop() {
    AA_LOG_D("write thread started");

    while (open_) {
        std::vector<uint8_t> data;
        {
            std::unique_lock<std::mutex> lock(write_mutex_);
            write_cv_.wait(lock, [this] {
                return write_requested_ || !open_;
            });
            if (!open_ && !write_requested_) break;
            data.swap(pending_write_data_);
            write_requested_ = false;
        }

        struct usbdevfs_bulktransfer bulk{};
        bulk.ep = ep_out_;
        bulk.len = static_cast<unsigned int>(data.size());
        bulk.timeout = kBulkTimeoutMs;
        bulk.data = data.data();

        int n = ::ioctl(fd_, USBDEVFS_BULK, &bulk);

        transport::WriteHandler handler;
        {
            std::lock_guard<std::mutex> lock(write_mutex_);
            handler = std::move(pending_write_handler_);
            pending_write_handler_ = nullptr;
        }

        auto exec = executor_;
        if (n < 0) {
            int err = errno;
            if (!open_) break;
            AA_LOG_E("write ioctl failed: %s (errno=%d)", strerror(err), err);
            if (handler) {
                asio::post(exec, [handler, err] {
                    handler(std::error_code(err, std::system_category()), 0);
                });
            }
        } else {
            std::size_t written = static_cast<std::size_t>(n);
            if (handler) {
                asio::post(exec, [handler, written] {
                    handler({}, written);
                });
            }
        }
    }

    AA_LOG_D("write thread exited");
}

} // namespace aauto::impl
