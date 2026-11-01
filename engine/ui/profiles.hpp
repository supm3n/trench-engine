


#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/bytes.hpp"

namespace antanta::ui {


constexpr std::size_t kProfileNameMax = 14;

struct Profiles {
    std::vector<std::string> names{"Player"};  // UTF-8; never empty
    int selected = 0;                          // index into names; the selected name is the player's name
};





std::optional<Profiles> parse_splayers(ByteSpan file, std::string* error);

// Our file: "#" comment lines, "selected <n>", then one "name <text>" line per profile.
std::string profiles_print(const Profiles& p);
std::optional<Profiles> profiles_parse(std::string_view text, std::string* error);

// The text of the edit field made fit: at most kProfileNameMax characters (UTF-8 code points).
std::string profile_name_clip(std::string_view name);





bool profile_add(Profiles& p, std::string_view name);

bool profile_delete(Profiles& p);



void profile_select(Profiles& p, int index, std::string_view edit_text);

}  // namespace antanta::ui
