#pragma once

#include "aauto/transport/ITransport.hpp"

#include <asio.hpp>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace aauto::transport {

/// Decorator that ensures the underlying transport keeps reading
/// independently of how fast the upper layer consumes data.
///
/// Why it exists: the raw ITransport contract serializes reads — the
/// next async_read can't start until the previous handler returns.
/// When the upper layer's per-message processing (decrypt, framer
/// reassembly, dispatch) is slow, the underlying transport sits idle
/// even though USB/TCP has bytes ready, which is a missed opportunity
/// for throughput and adds latency.
///
/// What it does:
///   - Owns a fixed-size pool of read buffers.
///   - Keeps one async_read pending on the underlying transport at all
///     times. As soon as a read completes, the buffer is pushed onto
///     an internal queue and a new underlying read is issued.
///   - When the upper layer calls async_read, BufferedTransport pops
///     from the queue (if non-empty) or stores the handler as pending
///     until the next chunk arrives.
///
/// As a result: "transport에서 읽을게 있으면 항상 바로 read" — the
/// underlying transport is never blocked on the upper layer.
///
/// TX path (async_write) is forwarded straight to the underlying
/// transport, which already has a dedicated write thread + queue
/// pattern in the existing implementations (USB, TCP). Buffering is
/// not needed there.
///
/// Threading: this class is invoked from the asio strand (same as the
/// underlying transport). Internal state is protected by a mutex
/// because the underlying transport's read completion can fire on its
/// own thread before being posted to the strand.
class BufferedTransport
    : public ITransport,
      public std::enable_shared_from_this<BufferedTransport> {
public:
    /// kReadBufferSize matches Session::read_buffer_ size so buffers
    /// pop directly into the upper layer without further chunking.
    static constexpr std::size_t kReadBufferSize = 16384;

    /// kMaxQueuedReads caps memory growth when the upper layer falls
    /// behind. When full, the oldest buffered read is dropped (with a
    /// warning log) — the assumption is that AAP recovery handles
    /// occasional gaps better than unbounded memory.
    static constexpr std::size_t kMaxQueuedReads = 32;

    explicit BufferedTransport(std::shared_ptr<ITransport> underlying);
    ~BufferedTransport() override;

    /// Start the underlying-read auto-loop. Must be called once before
    /// async_read. Idempotent.
    void start();

    // ===== ITransport =====

    void async_read(asio::mutable_buffer buffer,
                    ReadHandler handler) override;
    void async_write(asio::const_buffer buffer,
                     WriteHandler handler) override;
    void close() override;
    bool is_open() const override;
    asio::any_io_executor get_executor() override;

private:
    void issue_underlying_read();
    void on_underlying_read(const std::error_code& ec,
                            std::vector<uint8_t> buf,
                            std::size_t bytes);
    void invoke_handler(ReadHandler handler,
                        std::error_code ec,
                        std::vector<uint8_t> data,
                        asio::mutable_buffer dest);

    std::shared_ptr<ITransport> underlying_;
    asio::any_io_executor       executor_;

    std::mutex                            mu_;
    std::deque<std::vector<uint8_t>>      rx_queue_;     // ready chunks
    std::vector<uint8_t>                  pending_read_; // buffer in flight
    bool                                  loop_started_ = false;
    bool                                  closed_       = false;

    // One outstanding upper-layer async_read at a time (per ITransport
    // contract). Stored when the queue is empty and resolved on the
    // next underlying read completion.
    ReadHandler                           pending_handler_;
    asio::mutable_buffer                  pending_dest_{};
    std::error_code                       deferred_error_;
};

} // namespace aauto::transport
