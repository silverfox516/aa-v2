#pragma once

#include "aauto/session/Session.hpp"

#include <vector>
#include <utility>

namespace aauto::test {

class MockSessionObserver : public session::ISessionObserver {
public:
    void on_session_state_changed(uint32_t session_id,
                                  session::SessionState state) override {
        state_changes_.emplace_back(session_id, state);
    }

    void on_session_error(uint32_t session_id,
                          const std::error_code& ec) override {
        errors_.emplace_back(session_id, ec);
    }

    const auto& state_changes() const { return state_changes_; }
    const auto& errors() const { return errors_; }

    session::SessionState last_state() const {
        return state_changes_.empty() ? session::SessionState::Idle
                                      : state_changes_.back().second;
    }

private:
    std::vector<std::pair<uint32_t, session::SessionState>> state_changes_;
    std::vector<std::pair<uint32_t, std::error_code>> errors_;
};

} // namespace aauto::test
