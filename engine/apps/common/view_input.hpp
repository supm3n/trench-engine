


#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "formats/bytes.hpp"
#include "formats/params.hpp"
#include "sim/state.hpp"

namespace antanta {

// Reads a game file by its path relative to the game folder, '/'-separated.
using FileLoader = std::function<std::optional<Bytes>(const std::string& path, std::string* error)>;


struct ViewBox {
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};


bool view_box_for(int w, int h, int col, int row, ViewBox* box);
// A new game without a recorded view (the owner's answer 7, docs/view-dependence.md): every house counts as drawn
// from the moment it exists, so the box holds every cell of the map.
ViewBox view_box_whole_map(const sim::GameState& s);




std::vector<sim::Command> view_inputs(const ViewBox& box, const sim::GameState& s);

// Loads the depth map of every house graphic the HouseDepth commands name into GameState::house_depth_maps (once
// per graphic). A map that cannot be loaded is left out (the sim then counts the input as unported) and reported
// in `warnings`.
void load_house_depth(const FileLoader& load, const std::vector<BasicParams>& params,
                      std::span<const sim::Command> commands, sim::GameState& s, std::vector<std::string>* warnings);




void load_saved_depth(const FileLoader& load, const std::vector<BasicParams>& params, sim::GameState& s,
                      std::vector<std::string>* warnings);

}  // namespace antanta
