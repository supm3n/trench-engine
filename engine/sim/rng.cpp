// Random numbers: the MSVC CRT generator and the shared u16 table (docs/determinism.md, docs/movement.md 8).
#include "state.hpp"

namespace antanta::sim {


void Crt_srand(GameState& s, std::uint32_t seed) { s.crt_holdrand = seed; }


int Crt_rand(GameState& s) {
    s.crt_holdrand = s.crt_holdrand * 214013u + 2531011u;
    return static_cast<int>(s.crt_holdrand >> 16 & 0x7fff);
}


void RNG_FillTable(GameState& s) {
    Crt_srand(s, s.rng_seed);
    s.rng_table.resize(0x10000);
    for (std::uint16_t& v : s.rng_table) v = static_cast<std::uint16_t>(Crt_rand(s));
    s.rng_index = 0;
}

}  // namespace antanta::sim
