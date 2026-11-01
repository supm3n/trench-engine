





#pragma once

#include <cstdint>

namespace antanta::ui {

struct PlayClock {
    double ms = 0;  // milliseconds the game has run since this clock was started

    void advance(double frame_ms, bool running) {
        if (running && frame_ms > 0) ms += frame_ms;
    }
    // Whole seconds, so the difference of two readings is a whole number of running seconds.
    std::int64_t seconds() const { return static_cast<std::int64_t>(ms / 1000.0); }
};

}  // namespace antanta::ui
