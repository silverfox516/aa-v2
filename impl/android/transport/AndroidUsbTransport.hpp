#pragma once

#include "aauto/transport/ITransport.hpp"

#include <asio.hpp>

namespace aauto::impl {

/// ITransport implementation for Android USB accessory mode.
/// Wraps a file descriptor obtained from UsbManager (passed via IPC).
///
/// USB accessory protocol:
///   1. App layer opens USB device via UsbManager, gets fd
///   2. App sends accessory identification strings
///   3. Device switches to accessory mode, re-enumerates
///   4. App opens the new accessory fd and passes it to engine
///   5. This class wraps that fd for async read/write via asio
class AndroidUsbTransport : public transport::ITransport {
public:
    /// Construct from an already-opened USB accessory file descriptor.
    /// Takes ownership of the fd (will close on destruction).
    AndroidUsbTransport(asio::any_io_executor executor, int fd);
    ~AndroidUsbTransport();

    void async_read(asio::mutable_buffer buffer,
                    transport::ReadHandler handler) override;
    void async_write(asio::const_buffer buffer,
                     transport::WriteHandler handler) override;
    void close() override;
    bool is_open() const override;
    asio::any_io_executor get_executor() override;

private:
    asio::posix::stream_descriptor descriptor_;
    bool open_;
};

} // namespace aauto::impl
