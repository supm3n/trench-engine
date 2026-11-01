

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bytes.hpp"

namespace antanta {

// One 6-byte entry: u8 kind (1 unit, 2 house, 3 research), a byte the reader ignores, u16 type, u16 count.
struct AiTemplateEntry {
    std::uint8_t kind = 0;
    std::uint16_t type = 0, count = 0;
};



struct AiTemplateStep {
    std::vector<AiTemplateEntry> main, list2;
};

struct AiTemplate {
    std::vector<AiTemplateStep> steps;
};



std::optional<AiTemplate> parse_ai_template(ByteSpan file, std::string* error);

}  // namespace antanta
