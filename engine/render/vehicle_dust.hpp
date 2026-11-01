

#pragma once

#include <array>
#include <optional>

namespace antanta {

struct DustPoint {
    float x = 0, y = 0;  // world pixels from the unit's point
};




std::optional<std::array<DustPoint, 6>> vehicle_dust_points(int nation, int type, int size, int orient);

}  // namespace antanta
