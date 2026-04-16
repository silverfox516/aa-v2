#pragma once

#include <asio.hpp>
#include <cstdint>
#include <functional>
#include <system_error>

namespace aauto::transport {

using ReadHandler  = std::function<void(const std::error_code& ec,
                                        std::size_t bytes_read)>;
using WriteHandler = std::function<void(const std::error_code& ec,
                                        std::size_t bytes_written)>;

/// Inbound port: async byte stream transport (USB, TCP, etc.)
///
/// Lifecycle contract:
///   1. Constructed in closed state
///   2. Caller opens transport (platform-specific, outside this interface)
///   3. Once open, async_read/async_write are valid
///   4. close() may be called from any state; cancels pending operations
///   5. After close(), no further async_read/async_write calls
///
/// Threading contract:
///   All callbacks dispatched on the associated executor (io_context thread).
///   Implementations must NOT call handlers inline during async_read/async_write.
///   One outstanding async_read and one async_write at a time.
class ITransport {
public:
    virtual ~ITransport() = default;

    /// Async read into buffer. Exactly one handler invocation per call.
    /// Buffer must remain valid until handler fires.
    /// ec == asio::error::operation_aborted on close().
    virtual void async_read(asio::mutable_buffer buffer,
                            ReadHandler handler) = 0;

    /// Async write from buffer. Exactly one handler invocation per call.
    /// Buffer must remain valid until handler fires.
    virtual void async_write(asio::const_buffer buffer,
                             WriteHandler handler) = 0;

    /// Close transport. Cancels pending ops. Idempotent.
    virtual void close() = 0;

    /// Returns true if transport is open and usable.
    virtual bool is_open() const = 0;

    /// Executor for dispatching callbacks.
    virtual asio::any_io_executor get_executor() = 0;
};

} // namespace aauto::transport
