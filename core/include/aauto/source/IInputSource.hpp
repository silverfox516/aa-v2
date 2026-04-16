#pragma once

#include <cstdint>
#include <functional>

namespace aauto::source {

struct TouchEvent {
    uint32_t action;       // DOWN=0, UP=1, MOVE=2
    uint32_t x;
    uint32_t y;
    uint32_t pointer_id;
    int64_t  timestamp_ns;
};

struct KeyEvent {
    uint32_t keycode;
    uint32_t action;       // DOWN=0, UP=1
    int64_t  timestamp_ns;
};

using InputEventCallback = std::function<void(const TouchEvent&)>;
using KeyEventCallback   = std::function<void(const KeyEvent&)>;

/// Inbound port: platform input events -> AAP input channel.
///
/// Lifecycle:
///   1. Constructed (idle)
///   2. start(touch_cb, key_cb) -- begin forwarding events
///   3. stop() -- stop forwarding
///
/// Threading: callbacks may come from any thread.
/// Service posts them onto the session strand.
class IInputSource {
public:
    virtual ~IInputSource() = default;

    virtual void start(InputEventCallback touch_cb,
                       KeyEventCallback key_cb) = 0;
    virtual void stop() = 0;
};

} // namespace aauto::source
