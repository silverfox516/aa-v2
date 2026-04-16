#include <gtest/gtest.h>

#include "aauto/utils/ProtocolConstants.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/session/SessionState.hpp"

// Verify that core headers compile and basic types work.

TEST(ProtocolConstants, AapErrcMakesValidErrorCode) {
    auto ec = aauto::make_error_code(aauto::AapErrc::PingTimeout);
    EXPECT_NE(ec.value(), 0);
    EXPECT_EQ(ec.category().name(), std::string("aap"));
    EXPECT_FALSE(ec.message().empty());
}

TEST(ProtocolConstants, SuccessIsZero) {
    auto ec = aauto::make_error_code(aauto::AapErrc::Success);
    EXPECT_EQ(ec.value(), 0);
}

TEST(SessionState, IsTerminal) {
    using aauto::session::SessionState;
    using aauto::session::is_terminal;

    EXPECT_FALSE(is_terminal(SessionState::Idle));
    EXPECT_FALSE(is_terminal(SessionState::Running));
    EXPECT_TRUE(is_terminal(SessionState::Disconnected));
    EXPECT_TRUE(is_terminal(SessionState::Error));
}

TEST(SessionState, ToString) {
    using aauto::session::SessionState;
    using aauto::session::to_string;

    EXPECT_STREQ(to_string(SessionState::Idle), "Idle");
    EXPECT_STREQ(to_string(SessionState::Running), "Running");
    EXPECT_STREQ(to_string(SessionState::Error), "Error");
}

#define LOG_TAG "Test"
TEST(Logger, DoesNotCrash) {
    AA_LOG_I("test message %d", 42);
    AA_LOG_W("warning %s", "test");
}
