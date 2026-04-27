#include <gtest/gtest.h>

#include "aauto/transport/BufferedTransport.hpp"

#include "mock/MockTransport.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace aauto;
using namespace aauto::transport;
using namespace aauto::test;

namespace {

// Run the given io_context on a dedicated thread and tear down at end.
class IoContextThread {
public:
    IoContextThread() : work_(asio::make_work_guard(io_)) {
        thread_ = std::thread([this] { io_.run(); });
    }
    ~IoContextThread() {
        work_.reset();
        io_.stop();
        if (thread_.joinable()) thread_.join();
    }
    asio::io_context& io() { return io_; }
    asio::any_io_executor executor() { return io_.get_executor(); }

private:
    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    std::thread thread_;
};

// Pump io_context briefly so posted handlers complete.
void pump(asio::io_context& /*io*/) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

std::vector<uint8_t> make_payload(std::size_t size, uint8_t seed) {
    std::vector<uint8_t> v(size);
    for (std::size_t i = 0; i < size; ++i) {
        v[i] = static_cast<uint8_t>(seed + i);
    }
    return v;
}

} // namespace

TEST(BufferedTransportTest, DeliversChunkToWaitingReader) {
    IoContextThread thr;
    auto mock = std::make_shared<MockTransport>(thr.executor());
    auto buffered = std::make_shared<BufferedTransport>(mock);
    buffered->start();

    // Reader parks first; data arrives later.
    std::array<uint8_t, 16384> dest{};
    std::vector<uint8_t> received;
    bool fired = false;
    buffered->async_read(asio::buffer(dest),
        [&](const std::error_code& ec, std::size_t bytes) {
            ASSERT_FALSE(ec);
            received.assign(dest.begin(),
                            dest.begin() + static_cast<std::ptrdiff_t>(bytes));
            fired = true;
        });

    auto payload = make_payload(128, 0x10);
    mock->feed_read(payload);
    pump(thr.io());

    EXPECT_TRUE(fired);
    EXPECT_EQ(received, payload);

    buffered->close();
}

TEST(BufferedTransportTest, BuffersChunksWhileNoReader) {
    IoContextThread thr;
    auto mock = std::make_shared<MockTransport>(thr.executor());
    auto buffered = std::make_shared<BufferedTransport>(mock);
    buffered->start();

    // Feed three chunks before any reader arrives. BufferedTransport
    // should auto-issue underlying reads and queue all three.
    auto p1 = make_payload(32, 0x01);
    auto p2 = make_payload(48, 0x40);
    auto p3 = make_payload(16, 0x80);
    mock->feed_read(p1);
    pump(thr.io());
    mock->feed_read(p2);
    pump(thr.io());
    mock->feed_read(p3);
    pump(thr.io());

    // Now drain via three sequential async_read calls.
    auto drain_one = [&](const std::vector<uint8_t>& expected) {
        std::array<uint8_t, 16384> dest{};
        bool fired = false;
        std::vector<uint8_t> got;
        buffered->async_read(asio::buffer(dest),
            [&](const std::error_code& ec, std::size_t bytes) {
                ASSERT_FALSE(ec);
                got.assign(dest.begin(),
                           dest.begin() + static_cast<std::ptrdiff_t>(bytes));
                fired = true;
            });
        pump(thr.io());
        EXPECT_TRUE(fired);
        EXPECT_EQ(got, expected);
    };

    drain_one(p1);
    drain_one(p2);
    drain_one(p3);

    buffered->close();
}

TEST(BufferedTransportTest, ForwardsWriteToUnderlying) {
    IoContextThread thr;
    auto mock = std::make_shared<MockTransport>(thr.executor());
    auto buffered = std::make_shared<BufferedTransport>(mock);
    buffered->start();

    auto payload = make_payload(64, 0xA0);
    bool fired = false;
    buffered->async_write(asio::buffer(payload),
        [&](const std::error_code& ec, std::size_t bytes) {
            ASSERT_FALSE(ec);
            EXPECT_EQ(bytes, payload.size());
            fired = true;
        });
    pump(thr.io());

    EXPECT_TRUE(fired);
    EXPECT_EQ(mock->get_written_data(), payload);

    buffered->close();
}

TEST(BufferedTransportTest, CloseAbortsPendingReader) {
    IoContextThread thr;
    auto mock = std::make_shared<MockTransport>(thr.executor());
    auto buffered = std::make_shared<BufferedTransport>(mock);
    buffered->start();

    std::array<uint8_t, 16384> dest{};
    std::error_code received_ec;
    bool fired = false;
    buffered->async_read(asio::buffer(dest),
        [&](const std::error_code& ec, std::size_t /*bytes*/) {
            received_ec = ec;
            fired = true;
        });
    pump(thr.io());

    buffered->close();
    pump(thr.io());

    EXPECT_TRUE(fired);
    EXPECT_EQ(received_ec, asio::error::operation_aborted);
}
