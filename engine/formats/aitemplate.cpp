#include "aitemplate.hpp"

#include <cstring>

namespace antanta {

std::optional<AiTemplate> parse_ai_template(ByteSpan file, std::string* error) {
    Cursor c{file};
    const ByteSpan magic = c.take(4);
    if (!c.ok || std::memcmp(magic.data(), "tmpl", 4) != 0) {
        *error = "AI template magic tmpl missing";
        return std::nullopt;
    }
    const std::uint8_t n = c.u8();
    if (n > 100) {
        *error = "AI template has more than 100 steps";
        return std::nullopt;
    }
    AiTemplate t;
    auto list = [&c](std::vector<AiTemplateEntry>& out) {
        const std::uint16_t count = c.u16();
        for (std::uint16_t i = 0; i < count && c.ok; ++i) {
            AiTemplateEntry e;
            e.kind = c.u8();
            c.skip(1);
            e.type = c.u16(), e.count = c.u16();
            out.push_back(e);
        }
    };
    for (std::uint8_t i = 0; i < n && c.ok; ++i) {
        AiTemplateStep s;
        list(s.main);
        list(s.list2);
        t.steps.push_back(std::move(s));
    }
    if (!c.ok) {
        *error = "AI template is truncated";
        return std::nullopt;
    }
    return t;
}

}  // namespace antanta
