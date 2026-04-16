#define LOG_TAG "UsbTransport"

#include "AndroidUsbTransport.hpp"
#include "aauto/utils/Logger.hpp"

#include <unistd.h>

namespace aauto::impl {

AndroidUsbTransport::AndroidUsbTransport(asio::any_io_executor executor, int fd)
    : descriptor_(executor, fd)
    , open_(true) {
    AA_LOG_I("USB transport created, fd=%d", fd);
}

AndroidUsbTransport::~AndroidUsbTransport() {
    close();
}

void AndroidUsbTransport::async_read(asio::mutable_buffer buffer,
                                     transport::ReadHandler handler) {
    if (!open_) {
        asio::post(descriptor_.get_executor(), [handler] {
            handler(asio::error::operation_aborted, 0);
        });
        return;
    }

    descriptor_.async_read_some(buffer, std::move(handler));
}

void AndroidUsbTransport::async_write(asio::const_buffer buffer,
                                      transport::WriteHandler handler) {
    if (!open_) {
        asio::post(descriptor_.get_executor(), [handler] {
            handler(asio::error::operation_aborted, 0);
        });
        return;
    }

    asio::async_write(descriptor_, buffer, std::move(handler));
}

void AndroidUsbTransport::close() {
    if (open_) {
        open_ = false;
        std::error_code ec;
        descriptor_.close(ec);
        if (ec) {
            AA_LOG_W("close error: %s", ec.message().c_str());
        }
        AA_LOG_I("USB transport closed");
    }
}

bool AndroidUsbTransport::is_open() const {
    return open_;
}

asio::any_io_executor AndroidUsbTransport::get_executor() {
    return descriptor_.get_executor();
}

} // namespace aauto::impl
