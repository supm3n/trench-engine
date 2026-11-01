// The live game's side of the effect list (render/effects, docs/render.md 14): the records the sim's step raised
// (SimEvent::Effect) and the ones the original's drawing code makes itself from the state, with CRT rand() (ours
// draws from the view's own generator, so the sim's numbers never change).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "render/effects.hpp"
#include "render/live.hpp"
#include "sim/events.hpp"

namespace antanta {

// The record of one SimEvent::Effect (render/effects EffectSpawn).
EffectSpawn effect_spawn(const sim::SimEvent::EffectArgs& a);
// The view's stand-in for the CRT rand() the original's drawing code calls (the MSVC formula on Effects::rand_state).
int effect_rand(Effects& fx);


bool effect_point_in_view(float x, float y, ViewPos pos, int w, int h);



bool unit_in_view(const WorldGraphics& g, int nation, int type, float x, float y, float lift, ViewPos pos, int w,
                  int h);




void effects_dust(Effects& fx, EffectGraphics& eg, const GameFileLoader& load, const WorldGraphics& g,
                  const sim::GameState& s, const sim::SimEvent& ev, ViewPos pos, int w, int h);




void effects_house_fires(Effects& fx, EffectGraphics& eg, const GameFileLoader& load, const WorldGraphics& g,
                         const LiveView& lv, const sim::GameState& s);






void effects_tick(LiveView& lv, const WorldGraphics& g, const GameFileLoader& load, const sim::GameState& s,
                  const std::vector<sim::SimEvent>& events, ViewPos pos, int w, int h,
                  std::optional<sim::HouseRef> selected = std::nullopt, UnitSprites* sprites = nullptr);

}  // namespace antanta
