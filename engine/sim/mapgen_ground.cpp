// The random map generator's ground painting (docs/map-generator-plan.md 5.8, slice G3). Generator code, no original
// function: the shipped maps were painted by hand in the editor; this paints ground with their numbers.
//
// The visible ground of the shipped maps is the sub-tile layer: every cell holds a 3 x 3 grid of soft round blobs
// (docs/render.md "Transitions"), each blob any texture, and the whole map is a dithered mosaic of them. Windows of
// that mosaic fall into a few "mixes" of ids per climate (the tables below). So:
// - The sub-tile slots of the whole map form one lattice (3 points per cell edge along the cell's u and v edges). On it
//   lie "clump" seeds, one in every 2 x 2 block of slots at a random spot; every slot takes the id of its nearest seed
//   (a few take a draw of their own), so ids come in small round clumps as on the shipped maps.
// - Every seed belongs to a region: the nearest of a jittered grid of region points (10 cells apart) after a slow warp,
//   and within a band of 2 cells of a border the nearer or the farther one by a draw, so two regions' clumps
//   interleave. Each region takes a mix by the rules of plan 5.8 (weights, the forest floor, only mix pairs that touch
//   on the shipped maps), and has a high mix for hill tops and a shore mix by water.
// - The base tile of a cell is its region's (it shows only between the blobs), the second base tile in small patches.
#include "sim/mapgen_ground.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <vector>

namespace antanta::sim {

namespace {

// ---- The mixes (tools/mapgen_mixes.py over the shipped skirmish maps, 2026-11-04; ids and weights only). ----
//
// The shipped windows of 6 cells x 12 rows grouped twice: into a few mixes (whose neighbours on the maps are counted),
// and each mix into palettes, ids that occur together in real windows (a mix's average alone would put ids side by
// side that never meet). The last palette of each climate is the shore: the ids on land within 2 cells of water. The
// steep ids are much more common on steep land than on all land (ledges, rock faces): they stay off flat ground. Made
// textures (fields, paving, ruts, water) are left out of every table.

struct MixId {
    std::uint16_t id;
    std::uint16_t w;  // per mille of the palette's slots
};
struct Palette {
    std::uint16_t mix;           // its mix (the shore: the number of mixes)
    std::uint16_t share;         // per 10,000 of the shipped windows
    std::uint16_t high, trees;   // how often on high ground (above 90) and under trees, x 100 against all windows
    std::uint16_t base, base2;   // the most common base tiles under it
    std::uint16_t base2_share;   // per mille of its cells with the second
    std::uint16_t first, count;  // its ids in the climate's id list
};

// summer: tools/mapgen_mixes.py, 9705 windows of 6 x 12 cells, 10 mixes, 40 palettes and the shore (426151 slots)
constexpr MixId kSummerIds[] = {
    {74, 171},  {51, 112},  {71, 97},   {61, 70},   {89, 55},   {69, 54},   {52, 47},   {87, 37},   {82, 35},
    {48, 31},   {73, 26},   {86, 25},   {125, 23},  {84, 22},   {126, 21},  {88, 19},   {49, 19},   {33, 17},
    {68, 17},   {97, 16},   {130, 16},  {91, 16},   {134, 15},  {37, 14},   {159, 13},  {83, 13},   {129, 59},
    {97, 53},   {131, 48},  {42, 42},   {84, 39},   {126, 37},  {158, 35},  {69, 33},   {134, 31},  {130, 31},
    {51, 31},   {99, 31},   {52, 30},   {61, 30},   {48, 28},   {82, 28},   {25, 27},   {31, 26},   {43, 24},
    {24, 22},   {2, 21},    {86, 21},   {74, 20},   {71, 20},   {89, 20},   {127, 19},  {100, 18},  {151, 18},
    {155, 17},  {85, 17},   {87, 16},   {64, 16},   {26, 15},   {91, 15},   {77, 15},   {28, 14},   {157, 14},
    {163, 14},  {97, 107},  {74, 86},   {68, 62},   {148, 48},  {100, 47},  {48, 46},   {52, 45},   {69, 44},
    {71, 43},   {82, 41},   {61, 36},   {51, 32},   {49, 30},   {37, 28},   {89, 28},   {87, 25},   {43, 22},
    {83, 20},   {86, 19},   {158, 18},  {73, 18},   {157, 18},  {81, 16},   {47, 15},   {33, 15},   {131, 14},
    {84, 14},   {66, 14},   {126, 13},  {125, 13},  {88, 12},   {130, 11},  {52, 306},  {97, 112},  {37, 74},
    {48, 56},   {74, 54},   {126, 38},  {82, 37},   {49, 34},   {61, 33},   {69, 27},   {33, 26},   {51, 25},
    {68, 20},   {131, 19},  {130, 18},  {158, 17},  {47, 17},   {71, 16},   {2, 15},    {87, 15},   {134, 14},
    {89, 14},   {43, 14},   {37, 329},  {52, 101},  {48, 89},   {97, 80},   {51, 66},   {126, 63},  {74, 46},
    {49, 31},   {47, 26},   {68, 26},   {82, 23},   {61, 19},   {69, 17},   {14, 16},   {89, 15},   {73, 15},
    {33, 14},   {71, 12},   {157, 12},  {126, 189}, {97, 129},  {14, 60},   {51, 58},   {100, 55},  {52, 53},
    {37, 53},   {48, 49},   {74, 48},   {61, 35},   {131, 29},  {71, 29},   {129, 28},  {69, 28},   {82, 22},
    {49, 22},   {134, 20},  {89, 20},   {130, 19},  {43, 15},   {125, 14},  {84, 13},   {152, 13},  {48, 231},
    {158, 60},  {131, 54},  {43, 45},   {69, 42},   {126, 37},  {52, 37},   {163, 37},  {148, 34},  {97, 32},
    {82, 32},   {51, 31},   {74, 30},   {61, 30},   {37, 28},   {71, 24},   {100, 22},  {44, 22},   {84, 20},
    {155, 19},  {89, 18},   {130, 17},  {87, 16},   {151, 15},  {159, 15},  {68, 14},   {83, 14},   {42, 14},
    {134, 13},  {142, 326}, {64, 199},  {60, 96},   {97, 94},   {37, 92},   {36, 48},   {34, 45},   {87, 44},
    {52, 40},   {82, 17},   {34, 126},  {36, 120},  {97, 114},  {37, 84},   {87, 77},   {52, 65},   {130, 62},
    {82, 41},   {142, 40},  {74, 28},   {64, 27},   {51, 26},   {131, 22},  {33, 22},   {84, 18},   {71, 17},
    {61, 16},   {83, 16},   {89, 15},   {48, 14},   {69, 14},   {2, 12},    {86, 12},   {60, 12},   {97, 388},
    {52, 125},  {37, 62},   {158, 52},  {87, 43},   {99, 37},   {48, 30},   {82, 29},   {126, 26},  {74, 21},
    {131, 20},  {33, 20},   {36, 19},   {100, 18},  {2, 18},    {130, 17},  {69, 17},   {47, 15},   {64, 15},
    {84, 13},   {157, 13},  {97, 587},  {25, 83},   {5, 83},    {2, 50},    {7, 31},    {3, 29},    {28, 27},
    {37, 25},   {126, 23},  {129, 21},  {6, 15},    {158, 13},  {52, 12},   {36, 270},  {97, 262},  {37, 171},
    {52, 103},  {87, 84},   {34, 50},   {82, 20},   {33, 14},   {100, 13},  {48, 11},   {97, 342},  {126, 219},
    {37, 196},  {52, 61},   {48, 41},   {14, 38},   {33, 22},   {82, 15},   {47, 15},   {43, 14},   {100, 13},
    {74, 13},   {69, 12},   {97, 316},  {34, 204},  {36, 90},   {87, 84},   {37, 67},   {64, 48},   {52, 45},
    {142, 45},  {130, 26},  {82, 25},   {33, 21},   {60, 17},   {100, 13},  {97, 448},  {34, 350},  {87, 113},
    {52, 42},   {33, 22},   {130, 13},  {82, 12},   {158, 225}, {131, 182}, {148, 127}, {48, 75},   {155, 62},
    {126, 59},  {43, 35},   {151, 35},  {69, 22},   {61, 21},   {84, 21},   {97, 19},   {117, 18},  {52, 16},
    {139, 15},  {163, 15},  {37, 14},   {134, 13},  {130, 13},  {44, 13},   {131, 309}, {158, 110}, {129, 100},
    {126, 83},  {134, 66},  {97, 49},   {130, 49},  {100, 39},  {48, 39},   {148, 30},  {127, 21},  {51, 20},
    {155, 20},  {52, 18},   {61, 17},   {43, 15},   {69, 14},   {131, 429}, {158, 276}, {148, 75},  {126, 56},
    {155, 32},  {139, 21},  {151, 20},  {43, 18},   {127, 17},  {48, 17},   {33, 14},   {134, 12},  {61, 12},
    {158, 395}, {131, 243}, {148, 104}, {126, 50},  {155, 50},  {151, 37},  {48, 20},   {117, 20},  {43, 20},
    {61, 17},   {139, 17},  {130, 13},  {127, 13},  {131, 497}, {130, 82},  {97, 70},   {28, 62},   {25, 59},
    {2, 54},    {134, 39},  {5, 39},    {129, 30},  {127, 21},  {126, 17},  {158, 15},  {148, 14},  {97, 270},
    {100, 266}, {131, 96},  {134, 68},  {126, 63},  {129, 51},  {158, 37},  {14, 26},   {148, 23},  {130, 21},
    {152, 21},  {48, 20},   {61, 13},   {43, 13},   {127, 12},  {97, 475},  {100, 311}, {14, 45},   {130, 40},
    {134, 33},  {126, 31},  {129, 26},  {131, 25},  {152, 15},  {97, 334},  {100, 247}, {130, 132}, {14, 87},
    {134, 63},  {129, 45},  {126, 37},  {152, 21},  {117, 20},  {61, 13},   {100, 463}, {97, 247},  {130, 90},
    {14, 61},   {134, 53},  {126, 30},  {129, 30},  {152, 14},  {131, 12},  {97, 223},  {5, 167},   {25, 135},
    {2, 133},   {28, 51},   {6, 51},    {131, 46},  {7, 36},    {130, 29},  {129, 28},  {3, 26},    {134, 17},
    {52, 17},   {42, 15},   {127, 13},  {126, 12},  {2, 449},   {97, 134},  {25, 88},   {5, 54},    {28, 46},
    {6, 35},    {129, 25},  {131, 21},  {3, 21},    {7, 19},    {37, 18},   {74, 16},   {42, 16},   {61, 15},
    {130, 14},  {126, 14},  {52, 14},   {25, 451},  {97, 246},  {5, 108},   {2, 86},    {7, 50},    {28, 36},
    {6, 23},    {28, 348},  {25, 145},  {97, 144},  {2, 85},    {5, 82},    {126, 43},  {127, 33},  {6, 32},
    {131, 30},  {129, 25},  {7, 21},    {134, 12},  {25, 171},  {28, 134},  {148, 104}, {131, 73},  {156, 60},
    {129, 59},  {4, 43},    {128, 43},  {127, 37},  {43, 25},   {61, 23},   {88, 23},   {3, 23},    {27, 23},
    {30, 21},   {26, 19},   {7, 18},    {139, 17},  {17, 16},   {2, 15},    {89, 15},   {87, 13},   {159, 13},
    {24, 12},   {148, 197}, {156, 142}, {127, 139}, {25, 133},  {28, 80},   {139, 70},  {88, 65},   {131, 62},
    {4, 21},    {128, 19},  {87, 18},   {61, 15},   {129, 14},  {89, 12},   {82, 12},   {148, 406}, {97, 106},
    {68, 73},   {100, 61},  {48, 39},   {74, 31},   {25, 30},   {61, 29},   {49, 27},   {131, 26},  {82, 22},
    {43, 22},   {28, 20},   {33, 17},   {127, 16},  {69, 16},   {134, 15},  {71, 15},   {5, 15},    {129, 14},
    {156, 182}, {127, 137}, {131, 92},  {89, 74},   {71, 65},   {73, 64},   {134, 63},  {87, 57},   {47, 47},
    {117, 45},  {129, 31},  {157, 31},  {84, 24},   {159, 23},  {125, 22},  {86, 16},   {151, 15},  {88, 13},
    {131, 235}, {127, 183}, {156, 111}, {129, 110}, {134, 63},  {71, 40},   {89, 37},   {87, 36},   {47, 35},
    {117, 32},  {157, 26},  {73, 24},   {84, 15},   {125, 15},  {159, 14},  {82, 12},   {86, 12},   {127, 403},
    {131, 96},  {156, 72},  {88, 45},   {139, 43},  {148, 41},  {129, 34},  {134, 33},  {89, 33},   {25, 30},
    {87, 24},   {73, 23},   {117, 22},  {47, 19},   {71, 18},   {157, 18},  {28, 17},   {128, 17},  {151, 12},
    {130, 210}, {100, 187}, {14, 135},  {97, 131},  {126, 84},  {134, 75},  {129, 58},  {152, 31},  {48, 22},
    {61, 14},   {155, 14},  {52, 13},   {51, 12},   {43, 12},   {130, 347}, {129, 103}, {14, 86},   {97, 78},
    {134, 67},  {126, 64},  {100, 51},  {131, 35},  {52, 22},   {48, 21},   {25, 20},   {51, 17},   {61, 16},
    {152, 16},  {69, 15},   {4, 15},    {2, 14},    {5, 13},    {117, 288}, {61, 122},  {59, 91},   {131, 64},
    {97, 61},   {100, 59},  {134, 40},  {129, 34},  {130, 32},  {158, 32},  {126, 30},  {127, 24},  {48, 23},
    {87, 22},   {148, 16},  {14, 16},   {51, 16},   {52, 14},   {156, 14},  {129, 367}, {131, 115}, {97, 86},
    {134, 55},  {127, 46},  {100, 43},  {126, 40},  {130, 34},  {25, 31},   {117, 29},  {61, 27},   {2, 20},
    {128, 19},  {52, 17},   {26, 17},   {158, 15},  {3, 13},    {4, 13},    {28, 12},   {134, 344}, {131, 251},
    {129, 103}, {152, 69},  {97, 67},   {100, 50},  {127, 33},  {130, 29},  {126, 20},  {2, 18},    {158, 16},
    {134, 481}, {74, 49},   {130, 46},  {68, 36},   {61, 35},   {52, 34},   {97, 27},   {100, 27},  {2, 25},
    {71, 24},   {33, 24},   {82, 22},   {152, 22},  {69, 21},   {43, 20},   {51, 19},   {131, 18},  {47, 17},
    {37, 15},   {25, 13},   {117, 13},  {14, 13},   {97, 89},   {48, 75},   {82, 69},   {87, 65},   {131, 57},
    {52, 57},   {61, 41},   {43, 32},   {36, 27},   {37, 27},   {34, 27},   {134, 25},  {51, 25},   {148, 24},
    {130, 23},  {126, 22},  {69, 22},   {158, 21},  {100, 20},  {129, 19},  {74, 19},   {64, 19},   {44, 18},
    {88, 17},   {89, 16},   {33, 16},   {127, 15},  {71, 15},   {86, 15},   {83, 15},   {84, 14},   {25, 14},
    {68, 14},   {2, 14},    {81, 13}};
constexpr Palette kSummerPalettes[] = {
    {0, 508, 26, 122, 1, 157, 239, 0, 26},    {0, 317, 413, 161, 127, 1, 312, 26, 38},
    {0, 283, 48, 99, 157, 1, 209, 64, 32},    {0, 231, 198, 82, 157, 97, 201, 96, 23},
    {0, 210, 11, 60, 97, 157, 423, 119, 19},  {0, 209, 49, 146, 1, 157, 287, 138, 23},
    {0, 167, 120, 88, 1, 127, 234, 161, 29},  {0, 118, 13, 51, 1, 6, 7, 190, 10},
    {0, 109, 58, 90, 1, 1, 0, 200, 24},       {1, 337, 136, 99, 1, 157, 343, 224, 21},
    {1, 276, 115, 71, 157, 6, 383, 245, 13},  {1, 269, 1, 64, 1, 1, 0, 258, 10},
    {1, 265, 25, 107, 157, 1, 159, 268, 13},  {1, 209, 21, 79, 1, 6, 8, 281, 13},
    {1, 138, 10, 52, 1, 1, 0, 294, 7},        {2, 368, 177, 163, 1, 127, 298, 301, 20},
    {2, 268, 82, 109, 1, 127, 243, 321, 17},  {2, 258, 41, 156, 1, 127, 178, 338, 13},
    {2, 172, 132, 155, 1, 127, 218, 351, 13}, {2, 125, 471, 79, 157, 6, 243, 364, 13},
    {3, 268, 15, 81, 1, 127, 147, 377, 15},   {3, 258, 1, 41, 1, 127, 79, 392, 9},
    {3, 256, 15, 72, 1, 130, 88, 401, 10},    {3, 219, 0, 37, 1, 14, 124, 411, 9},
    {4, 352, 281, 67, 157, 6, 396, 420, 16},  {4, 248, 202, 96, 157, 6, 229, 436, 17},
    {4, 179, 154, 46, 6, 157, 367, 453, 7},   {4, 170, 353, 55, 157, 6, 259, 460, 12},
    {5, 418, 108, 147, 2, 148, 177, 472, 24}, {5, 297, 48, 84, 2, 99, 274, 496, 15},
    {5, 125, 121, 87, 157, 127, 99, 511, 20}, {6, 439, 33, 131, 74, 156, 302, 531, 18},
    {6, 236, 24, 70, 74, 1, 257, 549, 17},    {6, 87, 40, 83, 1, 2, 175, 566, 19},
    {7, 350, 43, 109, 1, 127, 94, 585, 14},   {7, 268, 194, 149, 1, 127, 143, 599, 18},
    {8, 465, 23, 87, 1, 127, 140, 617, 19},   {8, 92, 176, 158, 1, 127, 211, 636, 19},
    {9, 219, 146, 115, 1, 6, 88, 655, 11},    {9, 216, 80, 82, 157, 134, 245, 666, 22},
    {10, 0, 0, 0, 1, 157, 0, 688, 35},
};
// How often two mixes' windows touch on the shipped maps (neighbouring windows; the diagonal: the same mix).
constexpr std::uint16_t kSummerSteep[] = {24, 26, 35, 42, 46, 77, 83, 84, 85, 86, 91, 153};
// How often two mixes' windows touch on the shipped maps (neighbouring windows; the diagonal: the same mix).
constexpr std::uint16_t kSummerTouch[10][10] = {
    {4388, 1194, 413, 329, 231, 165, 20, 273, 270, 230}, {1194, 3204, 44, 79, 534, 41, 4, 77, 23, 25},
    {413, 44, 2884, 124, 125, 65, 31, 64, 259, 124},     {329, 79, 124, 2104, 8, 34, 3, 568, 275, 137},
    {231, 534, 125, 8, 2172, 115, 11, 64, 25, 66},       {165, 41, 65, 34, 115, 2328, 123, 7, 47, 43},
    {20, 4, 31, 3, 11, 123, 2322, 4, 209, 48},           {273, 77, 64, 568, 64, 7, 4, 916, 150, 50},
    {270, 23, 259, 275, 25, 47, 209, 150, 608, 122},     {230, 25, 124, 137, 66, 43, 48, 50, 122, 684},
};
// winter: tools/mapgen_mixes.py, 3208 windows of 6 x 12 cells, 6 mixes, 21 palettes and the shore (22324 slots)
constexpr MixId kWinterIds[] = {
    {176, 107}, {74, 88},   {185, 86},  {180, 76},  {184, 60}, {181, 59},  {71, 57},   {89, 53},   {186, 45},
    {168, 40},  {69, 31},   {178, 28},  {82, 28},   {52, 28},  {48, 25},   {81, 23},   {87, 20},   {182, 18},
    {102, 18},  {162, 17},  {49, 17},   {73, 16},   {163, 16}, {68, 15},   {125, 14},  {66, 13},   {176, 272},
    {180, 92},  {74, 91},   {185, 73},  {71, 61},   {186, 60}, {181, 51},  {184, 39},  {178, 37},  {89, 36},
    {69, 31},   {51, 18},   {102, 18},  {48, 17},   {162, 17}, {168, 17},  {73, 17},   {87, 15},   {82, 14},
    {190, 13},  {68, 12},   {74, 143},  {71, 111},  {69, 68},  {51, 66},   {176, 60},  {162, 60},  {186, 34},
    {89, 31},   {102, 30},  {87, 29},   {180, 26},  {91, 25},  {48, 24},   {165, 23},  {181, 22},  {52, 22},
    {73, 21},   {166, 21},  {84, 19},   {185, 18},  {178, 17}, {184, 17},  {168, 16},  {125, 15},  {49, 15},
    {86, 15},   {68, 13},   {82, 13},   {88, 13},   {191, 12}, {178, 202}, {176, 123}, {180, 113}, {74, 79},
    {186, 54},  {185, 53},  {71, 53},   {181, 45},  {89, 38},  {69, 36},   {184, 30},  {102, 19},  {48, 18},
    {162, 15},  {188, 15},  {190, 14},  {87, 14},   {73, 14},  {91, 14},   {168, 13},  {82, 13},   {51, 12},
    {77, 12},   {192, 284}, {176, 173}, {191, 94},  {168, 74}, {180, 69},  {162, 62},  {186, 57},  {184, 42},
    {178, 35},  {181, 24},  {185, 23},  {74, 20},   {71, 16},  {48, 15},   {51, 13},   {176, 458}, {180, 110},
    {184, 76},  {185, 56},  {186, 43},  {74, 40},   {181, 36}, {178, 33},  {71, 26},   {173, 23},  {182, 20},
    {48, 19},   {69, 16},   {192, 16},  {89, 15},   {168, 13}, {176, 520}, {180, 270}, {184, 86},  {178, 43},
    {186, 22},  {173, 18},  {185, 16},  {182, 13},  {181, 12}, {176, 673}, {180, 100}, {184, 90},  {178, 28},
    {185, 25},  {186, 22},  {173, 19},  {182, 18},  {74, 12},  {181, 12},  {176, 486}, {178, 232}, {180, 125},
    {184, 55},  {173, 23},  {185, 21},  {186, 20},  {182, 14}, {181, 13},  {74, 12},   {180, 378}, {176, 344},
    {184, 74},  {178, 53},  {185, 28},  {186, 27},  {182, 24}, {173, 24},  {181, 19},  {74, 16},   {71, 12},
    {180, 347}, {176, 175}, {178, 89},  {184, 66},  {185, 59}, {74, 50},   {181, 45},  {186, 40},  {71, 34},
    {173, 26},  {182, 19},  {190, 18},  {48, 16},   {89, 15},  {180, 296}, {176, 291}, {178, 236}, {184, 60},
    {186, 28},  {173, 26},  {185, 21},  {181, 16},  {74, 13},  {182, 12},  {180, 499}, {178, 178}, {176, 139},
    {184, 73},  {186, 25},  {185, 20},  {182, 15},  {173, 15}, {181, 13},  {74, 12},   {190, 12},  {178, 444},
    {180, 190}, {184, 133}, {176, 54},  {186, 45},  {181, 40}, {185, 26},  {190, 24},  {74, 17},   {188, 14},
    {71, 13},   {178, 686}, {180, 152}, {184, 55},  {176, 46}, {186, 23},  {181, 14},  {188, 13},  {185, 12},
    {178, 466}, {176, 260}, {180, 153}, {184, 48},  {186, 23}, {185, 21},  {181, 16},  {173, 13},  {176, 302},
    {184, 171}, {180, 120}, {178, 78},  {185, 52},  {182, 45}, {186, 41},  {181, 41},  {173, 31},  {179, 20},
    {74, 19},   {48, 19},   {188, 19},  {71, 14},   {69, 14},  {89, 12},   {184, 194}, {180, 186}, {176, 154},
    {182, 97},  {185, 38},  {178, 32},  {186, 32},  {173, 29}, {181, 27},  {68, 23},   {190, 20},  {74, 19},
    {188, 19},  {171, 17},  {48, 16},   {81, 16},   {63, 15},  {177, 15},  {71, 14},   {69, 14},   {89, 12},
    {52, 12},   {184, 378}, {178, 144}, {180, 119}, {176, 87}, {181, 61},  {186, 42},  {185, 34},  {173, 33},
    {190, 31},  {182, 29},  {168, 15},  {188, 15},  {187, 12}, {68, 270},  {184, 164}, {176, 110}, {63, 103},
    {180, 94},  {191, 89},  {182, 78},  {52, 20},   {185, 16}, {81, 15},   {178, 14},  {186, 13},  {48, 13},
    {68, 446},  {191, 152}, {63, 144},  {184, 121}, {180, 75}, {182, 34},  {176, 29},  {176, 146}, {69, 83},
    {184, 75},  {81, 65},   {48, 63},   {180, 62},  {82, 51},  {66, 47},   {83, 42},   {163, 40},  {186, 37},
    {76, 36},   {78, 35},   {52, 29},   {37, 25},   {68, 21},  {87, 18},   {89, 17},   {181, 16},  {182, 14},
    {36, 14},   {84, 13},   {162, 13},  {88, 13},   {71, 12},  {61, 12}};
constexpr Palette kWinterPalettes[] = {
    {0, 807, 76, 115, 180, 176, 185, 0, 26},    {0, 779, 52, 94, 176, 180, 351, 26, 21},
    {0, 620, 60, 83, 176, 180, 64, 47, 30},     {0, 327, 159, 88, 176, 180, 286, 77, 23},
    {0, 175, 6, 64, 176, 180, 215, 100, 15},    {1, 829, 47, 106, 180, 176, 291, 115, 16},
    {1, 546, 20, 64, 176, 180, 222, 131, 9},    {1, 521, 36, 83, 176, 1, 234, 140, 10},
    {1, 287, 52, 86, 180, 176, 225, 150, 10},   {2, 561, 17, 85, 180, 176, 244, 160, 11},
    {2, 496, 45, 99, 180, 176, 219, 171, 14},   {2, 427, 33, 82, 180, 176, 253, 185, 10},
    {2, 299, 93, 69, 180, 192, 207, 195, 11},   {3, 642, 375, 121, 192, 176, 162, 206, 11},
    {3, 455, 380, 99, 192, 176, 130, 217, 8},   {3, 427, 136, 75, 176, 180, 234, 225, 8},
    {4, 661, 87, 152, 1, 180, 220, 233, 16},    {4, 567, 74, 152, 178, 180, 187, 249, 22},
    {4, 212, 368, 160, 192, 180, 185, 271, 13}, {5, 215, 6, 83, 178, 180, 124, 284, 13},
    {5, 147, 3, 45, 178, 180, 45, 297, 7},      {6, 0, 0, 0, 180, 176, 0, 304, 26},
};
// How often two mixes' windows touch on the shipped maps (neighbouring windows; the diagonal: the same mix).
constexpr std::uint16_t kWinterSteep[] = {83, 84, 172, 174, 175, 187};
// How often two mixes' windows touch on the shipped maps (neighbouring windows; the diagonal: the same mix).
constexpr std::uint16_t kWinterTouch[6][6] = {
    {1620, 641, 449, 212, 325, 59}, {641, 1160, 444, 176, 238, 7},  {449, 444, 742, 282, 238, 32},
    {212, 176, 282, 1050, 196, 1},  {325, 238, 238, 196, 584, 170}, {59, 7, 32, 1, 170, 176},
};

struct Climate {
    std::span<const MixId> ids;
    std::span<const Palette> palettes;     // the shore last
    std::span<const std::uint16_t> steep;  // ids kept off flat ground
    const std::uint16_t* touch;            // mixes x mixes
    std::size_t mixes;
};
Climate ClimateOf(bool winter) {
    if (winter) return {kWinterIds, kWinterPalettes, kWinterSteep, &kWinterTouch[0][0], std::size(kWinterTouch)};
    return {kSummerIds, kSummerPalettes, kSummerSteep, &kSummerTouch[0][0], std::size(kSummerTouch)};
}

// ---- The painter's numbers. Lattice units: one sub-tile slot (3 per cell edge); positions in eighths of a slot. ----

constexpr std::int32_t kSub = 8;          // position units per slot
constexpr std::int32_t kSeedStep = 3;     // clump seeds: one per 3 x 3 slots
constexpr std::int32_t kRegionStep = 45;  // region points: 15 cells apart (regions of about 225 cells)
constexpr std::int32_t kBand = 6;         // slots: the band on each side of a region border where clumps interleave
constexpr std::int32_t kWarp[2][2] = {{60, 24}, {18, 8}};  // slots: the warp of the region borders in two octaves,
                                                           // the lattice of each and its reach
constexpr std::int32_t kSpeckle = 12;                      // per cent of slots that take a draw of their own
constexpr std::int32_t kHigh = 88;  // height from which a seed takes its region's high mix (plus or minus 12)
constexpr std::int32_t kHighJitter = 12;
constexpr std::int32_t kSteep = 20;     // a cell whose corners differ by more takes its region's high palette
constexpr std::int32_t kFlat = 12;      // on a cell whose corners differ by at most this, a steep id is drawn again
constexpr std::int32_t kShore = 2;      // cells from water that take the shore palette
constexpr std::int32_t kTouchMin = 20;  // per mille of a mix's borders: the least for two mixes to be neighbours
constexpr std::int32_t kDominant[2] = {35, 0};  // per cent of seeds that take their region's leading id (summer,
                                                // winter: the winter mixes are already peaked)
constexpr std::int32_t kFactor[2][2] = {{40, 60}, {70, 30}};  // per map, every mix's weight: from, plus up to twice
constexpr std::int32_t kSameBonus = 4;     // a region takes a neighbour's mix this many times more readily
constexpr std::int32_t kForestTrees = 15;  // per cent of a region's seeds under trees that make it forest floor

// ---- Hashes and noise, own (plan 6: integer only). ----

std::uint32_t Mix32(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
std::uint32_t Hash(std::uint32_t key, std::int32_t x, std::int32_t y) {
    return Mix32(Mix32(key ^ static_cast<std::uint32_t>(x) * 0x9e3779b1u) ^
                 static_cast<std::uint32_t>(y) * 0x85ebca77u);
}
std::uint32_t SubKey(std::uint64_t key, std::uint32_t n) {
    std::uint64_t z = key + 0x9e3779b97f4a7c15ull * (n + 1);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
}
std::int32_t FloorDiv(std::int32_t a, std::int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// The smooth step 3t^2 - 2t^3 on 0..1024.
std::int64_t Smooth(std::int64_t t) { return t * t * (3072 - 2 * t) >> 20; }
// Value noise 0..1023 at (x, y) on a lattice of `cell` units.
std::int32_t Noise(std::uint32_t key, std::int32_t x, std::int32_t y, std::int32_t cell) {
    const std::int32_t ix = FloorDiv(x, cell), iy = FloorDiv(y, cell);
    const std::int64_t fx = Smooth(static_cast<std::int64_t>(x - ix * cell) * 1024 / cell);
    const std::int64_t fy = Smooth(static_cast<std::int64_t>(y - iy * cell) * 1024 / cell);
    const std::int64_t v00 = Hash(key, ix, iy) & 1023, v10 = Hash(key, ix + 1, iy) & 1023;
    const std::int64_t v01 = Hash(key, ix, iy + 1) & 1023, v11 = Hash(key, ix + 1, iy + 1) & 1023;
    const std::int64_t a = v00 + ((v10 - v00) * fx >> 10), b = v01 + ((v11 - v01) * fx >> 10);
    return static_cast<std::int32_t>(a + ((b - a) * fy >> 10));
}
std::int32_t ISqrt(std::int64_t v) {
    if (v <= 0) return 0;
    std::int64_t r = 0, bit = std::int64_t{1} << ((std::bit_width(static_cast<std::uint64_t>(v)) - 1) & ~1);
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<std::int32_t>(r);
}

// ---- The lattice: cell (x, y) is the square [u, u + 1) x [v, v + 1) in cell units, its slot s at (3u + s % 3,
// 3v + s / 3) (measure3.py; render.md: slot a = s % 3 along the up-right edge, b = s / 3 along the down-right one).
// ----

std::int32_t CellU(std::int32_t x, std::int32_t y) { return (2 * x + (y & 1) - y) >> 1; }
std::int32_t CellV(std::int32_t x, std::int32_t y) { return (2 * x + (y & 1) + y) >> 1; }
// The cell at lattice cell (u, v); false off the map.
bool CellOf(std::int32_t u, std::int32_t v, std::int32_t W, std::int32_t H, std::int32_t* x, std::int32_t* y) {
    const std::int32_t yy = v - u;
    if (yy < 0 || yy >= H) return false;
    const std::int32_t xx = (u + v - (yy & 1)) >> 1;
    if (xx < 0 || xx >= W) return false;
    *x = xx;
    *y = yy;
    return true;
}

// A palette's ids as a table of 1,024 draws.
using DrawTable = std::array<std::uint16_t, 1024>;
DrawTable MakeDraw(const Climate& c, std::size_t palette) {
    const Palette& x = c.palettes[palette];
    std::int32_t total = 0;
    for (std::uint16_t i = 0; i < x.count; ++i) total += c.ids[x.first + i].w;
    DrawTable t{};
    std::int32_t acc = 0;
    std::size_t k = 0;
    for (std::uint16_t i = 0; i < x.count; ++i) {
        acc += c.ids[x.first + i].w;
        const std::size_t end = static_cast<std::size_t>(acc * 1024 / total);
        for (; k < end; ++k) t[k] = c.ids[x.first + i].id;
    }
    for (; k < t.size(); ++k) t[k] = c.ids[x.first + x.count - 1].id;
    return t;
}

struct Region {
    std::int32_t px = 0, py = 0;  // the region point (position units)
    std::int32_t seeds = 0, trees = 0;
    std::uint8_t plain = 0, high = 0;  // palettes
};

}  // namespace

void MapGen_PaintGround(MapData& m, std::span<const std::uint8_t> water, bool winter, std::uint64_t key) {
    const std::int32_t W = m.width, H = m.height;
    if (W <= 0 || H <= 0) return;
    const Climate c = ClimateOf(winter);
    const std::size_t nmix = c.mixes, npal = c.palettes.size(), shore_palette = npal - 1;
    const bool has_water = !water.empty() && std::any_of(water.begin(), water.end(), [](std::uint8_t w) { return w; });

    // The lattice's extent: u from -(H - 1) / 2 to W, v from 0 to W + H / 2 (cell units).
    const std::int32_t u0 = CellU(0, H - 1) - 1, v0 = 0;
    const std::int32_t su = 3 * (CellU(W - 1, 0) - u0 + 2), sv = 3 * (CellV(W - 1, H - 1) - v0 + 2);  // slots

    // Cells near water (the shore mix): a breadth-first count of steps from water, up to kShore.
    std::vector<std::uint8_t> shore(static_cast<std::size_t>(W) * H, 0);
    if (has_water) {
        std::vector<std::int32_t> dist(shore.size(), -1), queue;
        for (std::size_t i = 0; i < shore.size(); ++i)
            if (water[i]) {
                dist[i] = 0;
                queue.push_back(static_cast<std::int32_t>(i));
            }
        for (std::size_t q = 0; q < queue.size(); ++q) {
            const std::int32_t i = queue[q], x = i % W, y = i / W, p = y & 1;
            if (dist[i] >= kShore) continue;
            const std::int32_t nb[8][2] = {{x + p, y - 1}, {x - 1 + p, y - 1}, {x + p, y + 1}, {x - 1 + p, y + 1},
                                           {x + 1, y},     {x - 1, y},         {x, y - 2},     {x, y + 2}};
            for (const auto& n : nb) {
                if (n[0] < 0 || n[0] >= W || n[1] < 0 || n[1] >= H) continue;
                const std::int32_t j = n[1] * W + n[0];
                if (dist[j] >= 0) continue;
                dist[j] = dist[i] + 1;
                queue.push_back(j);
            }
        }
        for (std::size_t i = 0; i < shore.size(); ++i) shore[i] = dist[i] >= 0;
    }

    const std::uint32_t k_region = SubKey(key, 1), k_warp_x = SubKey(key, 2), k_warp_y = SubKey(key, 3);
    const std::uint32_t k_seed = SubKey(key, 4), k_border = SubKey(key, 5), k_mix = SubKey(key, 6);
    const std::uint32_t k_id = SubKey(key, 7), k_speckle = SubKey(key, 8), k_base = SubKey(key, 9);
    const std::uint32_t k_factor = SubKey(key, 10), k_high = SubKey(key, 11), k_dominant = SubKey(key, 12);
    const std::uint32_t k_flat = SubKey(key, 13);

    // Region points on a jittered grid (position units, lattice origin at u0, v0).
    const std::int32_t rgu = su / kRegionStep + 2, rgv = sv / kRegionStep + 2;
    std::vector<Region> regions(static_cast<std::size_t>(rgu) * rgv);
    for (std::int32_t j = 0; j < rgv; ++j)
        for (std::int32_t i = 0; i < rgu; ++i) {
            const std::uint32_t h = Hash(k_region, i, j);
            Region& r = regions[static_cast<std::size_t>(j) * rgu + i];
            r.px = (i * kRegionStep) * kSub + static_cast<std::int32_t>(h % (kRegionStep * kSub));
            r.py = (j * kRegionStep) * kSub + static_cast<std::int32_t>((h >> 16) % (kRegionStep * kSub));
        }

    // Clump seeds: one per 3 x 3 slots, at a random spot in its block; each seed's region (-1: off the map, no slot
    // comes near it).
    const std::int32_t gu = su / kSeedStep + 1, gv = sv / kSeedStep + 1;
    const std::size_t nseeds = static_cast<std::size_t>(gu) * gv;
    std::vector<std::int32_t> seed_x(nseeds), seed_y(nseeds), seed_region(nseeds);
    for (std::int32_t j = 0; j < gv; ++j)
        for (std::int32_t i = 0; i < gu; ++i) {
            const std::size_t s = static_cast<std::size_t>(j) * gu + i;
            const std::uint32_t h = Hash(k_seed, i, j);
            const std::int32_t x = i * kSeedStep * kSub + static_cast<std::int32_t>(h % (kSeedStep * kSub));
            const std::int32_t y = j * kSeedStep * kSub + static_cast<std::int32_t>((h >> 8) % (kSeedStep * kSub));
            seed_x[s] = x;
            seed_y[s] = y;
            const std::int32_t cu = x / kSub / 3 + u0, cv = y / kSub / 3 + v0, cy = cv - cu,
                               cx = (cu + cv - (cy & 1)) >> 1;
            if (cy < -3 || cy > H + 2 || cx < -2 || cx > W + 1) {
                seed_region[s] = -1;
                continue;
            }
            // The warp moves the borders into curves; then the two nearest region points.
            const std::int32_t sx = x / kSub, sy = y / kSub;
            std::int32_t wx = x, wy = y;
            for (std::uint32_t o = 0; o < 2; ++o) {
                wx += (Noise(k_warp_x + o, sx, sy, kWarp[o][0]) - 512) * kWarp[o][1] * kSub / 512;
                wy += (Noise(k_warp_y + o, sx, sy, kWarp[o][0]) - 512) * kWarp[o][1] * kSub / 512;
            }
            const std::int32_t ci = FloorDiv(wx, kRegionStep * kSub), cj = FloorDiv(wy, kRegionStep * kSub);
            std::int64_t d1 = INT64_MAX, d2 = INT64_MAX;
            std::int32_t r1 = 0, r2 = 0;
            for (std::int32_t dj = -1; dj <= 1; ++dj)
                for (std::int32_t di = -1; di <= 1; ++di) {
                    const std::int32_t ri = std::clamp(ci + di, 0, rgu - 1), rj = std::clamp(cj + dj, 0, rgv - 1);
                    const std::int32_t r = rj * rgu + ri;
                    if (r == r1 || r == r2) continue;
                    const Region& g = regions[static_cast<std::size_t>(r)];
                    const std::int64_t d = static_cast<std::int64_t>(g.px - wx) * (g.px - wx) +
                                           static_cast<std::int64_t>(g.py - wy) * (g.py - wy);
                    if (d < d1) {
                        d2 = d1;
                        r2 = r1;
                        d1 = d;
                        r1 = r;
                    } else if (d < d2) {
                        d2 = d;
                        r2 = r;
                    }
                }
            // Within kBand slots of the border the farther region by a draw, the more likely the nearer the border.
            const std::int32_t gap = (ISqrt(d2) - ISqrt(d1)) / 2;  // distance to the border line, position units
            const std::int32_t band = kBand * kSub;
            seed_region[s] = r1;
            if (gap < band && static_cast<std::int32_t>(Hash(k_border, i, j) % (2 * band)) + gap < band)
                seed_region[s] = r2;
        }

    // The seed's cell (clamped to the map for the seeds outside it).
    auto seed_cell = [&](std::size_t s) {
        const std::int32_t u = seed_x[s] / kSub / 3 + u0, v = seed_y[s] / kSub / 3 + v0;
        std::int32_t x = 0, y = 0;
        if (!CellOf(u, v, W, H, &x, &y)) {
            y = std::clamp(v - u, 0, H - 1);
            x = std::clamp((u + v - (y & 1)) >> 1, 0, W - 1);
        }
        return y * W + x;
    };

    // The regions' trees and their neighbours (seeds next to each other in different regions).
    std::vector<std::uint64_t> pairs;
    for (std::int32_t j = 0; j < gv; ++j)
        for (std::int32_t i = 0; i < gu; ++i) {
            const std::size_t s = static_cast<std::size_t>(j) * gu + i;
            if (seed_region[s] < 0) continue;
            const std::int32_t cell = seed_cell(s);
            Region& r = regions[static_cast<std::size_t>(seed_region[s])];
            ++r.seeds;
            const std::uint16_t o = m.cells[static_cast<std::size_t>(cell)].object & 0x7fff;
            if (o >= 9000 && o < 9500) ++r.trees;
            const std::int32_t a = seed_region[s];
            if (i + 1 < gu && seed_region[s + 1] >= 0 && seed_region[s + 1] != a) {
                const std::int32_t b = seed_region[s + 1];
                pairs.push_back(static_cast<std::uint64_t>(std::min(a, b)) << 32 |
                                static_cast<std::uint32_t>(std::max(a, b)));
            }
            if (j + 1 < gv && seed_region[s + gu] >= 0 && seed_region[s + gu] != a) {
                const std::int32_t b = seed_region[s + gu];
                pairs.push_back(static_cast<std::uint64_t>(std::min(a, b)) << 32 |
                                static_cast<std::uint32_t>(std::max(a, b)));
            }
        }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    std::vector<std::vector<std::int32_t>> nbrs(regions.size());
    for (const std::uint64_t p : pairs) {
        const auto a = static_cast<std::int32_t>(p >> 32), b = static_cast<std::int32_t>(p & 0xffffffffu);
        nbrs[static_cast<std::size_t>(a)].push_back(b);
        nbrs[static_cast<std::size_t>(b)].push_back(a);
    }

    // Which mixes may be neighbours (they touch on the shipped maps), and this map's own leaning: every mix's weight
    // times a factor, so maps differ as the shipped ones do (summer 40 to 160 %: some green, some brown; winter 70 to
    // 130 %: the shipped winter maps are more alike).
    auto touches = [&](std::size_t a, std::size_t b) {
        if (a == b) return true;
        std::int32_t row = 0;
        for (std::size_t k = 0; k < nmix; ++k) row += c.touch[a * nmix + k];
        return row > 0 && c.touch[a * nmix + b] * 1000 >= kTouchMin * row;
    };
    std::vector<std::int64_t> factor(nmix);
    for (std::size_t k = 0; k < nmix; ++k)
        factor[k] = kFactor[winter][0] + Hash(k_factor, static_cast<std::int32_t>(k), 0) % (2 * kFactor[winter][1] + 1);
    auto pick = [](const std::vector<std::int64_t>& w, std::uint32_t h) {
        std::int64_t total = 0;
        for (const std::int64_t v : w) total += v;
        if (total == 0) return std::size_t{0};
        std::int64_t t = static_cast<std::int64_t>(h % 1000000) * total / 1000000;
        std::size_t k = 0;
        while (k + 1 < w.size() && (w[k] == 0 || t >= w[k])) t -= w[k++];
        return k;
    };

    // Each region's palette, in grid order: a draw by weight among the palettes whose mix touches the mix of every
    // neighbour drawn before; under trees the forest floor's palettes count more, a neighbour's mix more again. Its
    // high palette: one that lies on high ground at least 1.5 times as often as the average, of a mix that touches.
    std::vector<std::uint8_t> chosen(regions.size(), 0);
    std::vector<std::int64_t> w(npal), hw(npal);
    for (std::size_t r = 0; r < regions.size(); ++r) {
        Region& g = regions[r];
        if (g.seeds == 0) continue;
        const bool forest = g.trees * 100 >= kForestTrees * g.seeds;
        for (std::size_t k = 0; k < npal; ++k) {
            const Palette& pal = c.palettes[k];
            w[k] = 0;
            if (k == shore_palette) continue;
            bool ok = true, same = false;
            for (const std::int32_t n : nbrs[r]) {
                if (!chosen[static_cast<std::size_t>(n)]) continue;
                const std::uint16_t nm = c.palettes[regions[static_cast<std::size_t>(n)].plain].mix;
                ok = ok && touches(pal.mix, nm);
                same = same || nm == pal.mix;
            }
            if (!ok) continue;
            std::int64_t v = pal.share * factor[pal.mix];
            if (forest) v = v * pal.trees * pal.trees / 10000;
            if (same) v *= 1 + kSameBonus;
            w[k] = v;
        }
        g.plain = static_cast<std::uint8_t>(pick(w, Hash(k_mix, static_cast<std::int32_t>(r), 0)));
        chosen[r] = 1;
        const std::uint16_t mix = c.palettes[g.plain].mix;
        for (std::size_t k = 0; k < npal; ++k) {
            const Palette& pal = c.palettes[k];
            hw[k] =
                k != shore_palette && pal.high >= 150 && touches(mix, pal.mix) ? std::int64_t{pal.share} * pal.high : 0;
        }
        g.high = g.plain;
        if (std::any_of(hw.begin(), hw.end(), [](std::int64_t v) { return v > 0; }))
            g.high = static_cast<std::uint8_t>(pick(hw, Hash(k_high, static_cast<std::int32_t>(r), 0)));
    }

    // Every cell's slope: the largest difference of its four corners (the cell, the cells above and below it on the
    // right, the cell to the right; measure2.py).
    std::vector<std::int32_t> slope(static_cast<std::size_t>(W) * H);
    for (std::int32_t y = 0; y < H; ++y)
        for (std::int32_t x = 0; x < W; ++x) {
            auto h = [&](std::int32_t xx, std::int32_t yy) {
                return std::int32_t{
                    m.cells[static_cast<std::size_t>(std::clamp(yy, 0, H - 1)) * W + std::clamp(xx, 0, W - 1)].height};
            };
            const std::int32_t xt = x + (y & 1);
            const std::int32_t c0 = h(x, y), c1 = h(xt, y - 1), c2 = h(xt, y + 1), c3 = h(x + 1, y);
            slope[static_cast<std::size_t>(y) * W + x] = std::max({c0, c1, c2, c3}) - std::min({c0, c1, c2, c3});
        }
    std::vector<std::uint8_t> steep_id(65536, 0);
    for (const std::uint16_t id : c.steep) steep_id[id] = 1;

    // Every seed's palette (its region's plain palette, the high one on high or steep ground, or the shore, by its
    // cell) and id: a draw from the palette, or (kDominant %) the region's own leading id of it, so a region is less
    // even than the average.
    std::vector<DrawTable> draw(npal);
    for (std::size_t k = 0; k < npal; ++k) draw[k] = MakeDraw(c, k);
    std::vector<std::uint8_t> seed_pal(nseeds);
    std::vector<std::uint16_t> seed_id(nseeds);
    for (std::int32_t j = 0; j < gv; ++j)
        for (std::int32_t i = 0; i < gu; ++i) {
            const std::size_t s = static_cast<std::size_t>(j) * gu + i;
            if (seed_region[s] < 0) continue;
            const std::int32_t cell = seed_cell(s);
            const Region& g = regions[static_cast<std::size_t>(seed_region[s])];
            std::uint8_t mix = g.plain;
            const std::uint32_t h = Hash(k_id, i, j);
            const std::int32_t jitter = static_cast<std::int32_t>(h >> 24) % (2 * kHighJitter + 1) - kHighJitter;
            if (m.cells[static_cast<std::size_t>(cell)].height + jitter > kHigh ||
                slope[static_cast<std::size_t>(cell)] > kSteep)
                mix = g.high;
            if (shore[static_cast<std::size_t>(cell)]) mix = static_cast<std::uint8_t>(shore_palette);
            seed_pal[s] = mix;
            seed_id[s] = draw[mix][h & 1023];
            if (static_cast<std::int32_t>((h >> 10) % 100) < kDominant[winter])
                seed_id[s] = draw[mix][Hash(k_dominant, seed_region[s], mix) & 1023];
        }

    // Every slot: the id of its nearest seed, or a draw of its own from that seed's palette; on flat ground a steep id
    // is drawn again (twice at most).
    for (std::int32_t y = 0; y < H; ++y)
        for (std::int32_t x = 0; x < W; ++x) {
            MapCell& cell = m.cells[static_cast<std::size_t>(y) * W + x];
            const std::int32_t cu = 3 * (CellU(x, y) - u0), cv = 3 * (CellV(x, y) - v0);
            // The cell's nine slots share one seed block (cu, cv are multiples of kSeedStep), so its candidate seeds
            // once per cell, in the search's order; the squares stay far below 2^31.
            static_assert(kSeedStep == 3);
            std::array<std::size_t, 9> cq{};
            std::array<std::int32_t, 9> cx{}, cy{};
            std::size_t nc = 0;
            for (std::int32_t dj = -1; dj <= 1; ++dj)
                for (std::int32_t di = -1; di <= 1; ++di) {
                    const std::int32_t ii = cu / kSeedStep + di, jj = cv / kSeedStep + dj;
                    if (ii < 0 || jj < 0 || ii >= gu || jj >= gv) continue;
                    const std::size_t q = static_cast<std::size_t>(jj) * gu + ii;
                    cq[nc] = q, cx[nc] = seed_x[q], cy[nc] = seed_y[q], ++nc;
                }
            for (std::int32_t s = 0; s < 9; ++s) {
                const std::int32_t a = cu + s % 3, b = cv + s / 3;
                const std::int32_t px = a * kSub + kSub / 2, py = b * kSub + kSub / 2;
                std::int32_t best = INT32_MAX;
                std::size_t bs = 0;
                for (std::size_t k = 0; k < nc; ++k) {
                    const std::int32_t d = (cx[k] - px) * (cx[k] - px) + (cy[k] - py) * (cy[k] - py);
                    if (d < best) {
                        best = d;
                        bs = cq[k];
                    }
                }
                const std::uint32_t h = Hash(k_speckle, a, b);
                std::uint16_t id = h % 100 < kSpeckle ? draw[seed_pal[bs]][(h >> 8) & 1023] : seed_id[bs];
                if (steep_id[id] && slope[static_cast<std::size_t>(y) * W + x] <= kFlat) {
                    const std::uint32_t f = Hash(k_flat, a, b);
                    id = draw[seed_pal[bs]][f & 1023];
                    if (steep_id[id]) id = draw[seed_pal[bs]][(f >> 10) & 1023];
                }
                cell.subtile[static_cast<std::size_t>(s)] = id;
            }
            // The base tile: the region's (at the cell's centre slot), its second tile in small patches.
            const std::int32_t ca = cu + 1, cb = cv + 1;
            std::int32_t gi = ca / kSeedStep, gj = cb / kSeedStep;
            gi = std::min(gi, gu - 1);
            gj = std::min(gj, gv - 1);
            const Palette& mx =
                c.palettes[regions[static_cast<std::size_t>(seed_region[static_cast<std::size_t>(gj) * gu + gi])]
                               .plain];
            const bool second = Noise(k_base, ca, cb, 6) * 1000 < mx.base2_share * 1024;
            cell.tile = static_cast<std::uint16_t>((second ? mx.base2 : mx.base) | 0x8000);
        }
}

MapGenGroundLook MapGen_GroundLook(const MapData& m, std::span<const std::uint8_t> water) {
    MapGenGroundLook look;
    const std::int32_t W = m.width, H = m.height;
    if (W <= 0 || H <= 0) return look;
    auto is_water = [&](std::int32_t i) { return !water.empty() && water[static_cast<std::size_t>(i)] != 0; };
    auto land = [&](std::int32_t x, std::int32_t y) {
        return y >= 20 && y < H - 20 && x >= 10 && x < W - 10 && !is_water(y * W + x);
    };
    auto median = [](std::vector<std::int32_t>& v) -> double {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        const std::size_t n = v.size();
        return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
    };

    // Sub-tile flag share, base tile differences (the right-down and left-down neighbours), land heights.
    std::int64_t flagged = 0, landn = 0, pairs = 0, differ = 0;
    std::vector<std::int32_t> heights;
    for (std::int32_t y = 0; y < H; ++y)
        for (std::int32_t x = 0; x < W; ++x) {
            if (!land(x, y)) continue;
            const MapCell& c = m.cells[static_cast<std::size_t>(y) * W + x];
            ++landn;
            flagged += (c.tile & 0x8000) != 0;
            heights.push_back(c.height);
            if (y + 1 >= H) continue;
            for (const std::int32_t dx : {0, -1}) {
                const std::int32_t xx = x + (y & 1) + dx;
                if (xx < 0 || xx >= W) continue;
                ++pairs;
                differ += (m.cells[static_cast<std::size_t>(y + 1) * W + xx].tile & 0x7fff) != (c.tile & 0x7fff);
            }
        }
    look.subtile_share = landn ? static_cast<std::int32_t>(flagged * 1000 / landn) : 0;
    look.base_differ = pairs ? static_cast<std::int32_t>(differ * 1000 / pairs) : 0;
    look.land_height = static_cast<std::int32_t>(median(heights));

    // The lattice of the whole map (-1: no slot or id 0).
    const std::int32_t u0 = CellU(0, H - 1), su = 3 * (CellU(W - 1, 0) - u0 + 1), sv = 3 * (CellV(W - 1, H - 1) + 1);
    std::vector<std::int32_t> lat(static_cast<std::size_t>(su) * sv, -1);
    std::vector<std::int64_t> count(65536, 0);
    for (std::int32_t y = 0; y < H; ++y)
        for (std::int32_t x = 0; x < W; ++x) {
            const MapCell& c = m.cells[static_cast<std::size_t>(y) * W + x];
            const std::int32_t cu = 3 * (CellU(x, y) - u0), cv = 3 * CellV(x, y);
            for (std::int32_t s = 0; s < 9; ++s) {
                const std::uint16_t id = c.subtile[static_cast<std::size_t>(s)];
                if (id == 0) continue;
                lat[static_cast<std::size_t>(cu + s % 3) * sv + cv + s / 3] = id;
                ++count[id];
            }
        }
    std::int64_t ok_a = 0, same_a = 0, ok_b = 0, same_b = 0;
    for (std::int32_t a = 0; a < su; ++a)
        for (std::int32_t b = 0; b < sv; ++b) {
            const std::int32_t v = lat[static_cast<std::size_t>(a) * sv + b];
            if (v < 0) continue;
            if (a + 1 < su) {
                const std::int32_t w = lat[static_cast<std::size_t>(a + 1) * sv + b];
                if (w >= 0) {
                    ++ok_a;
                    same_a += v == w;
                }
            }
            if (b + 1 < sv) {
                const std::int32_t w = lat[static_cast<std::size_t>(a) * sv + b + 1];
                if (w >= 0) {
                    ++ok_b;
                    same_b += v == w;
                }
            }
        }
    look.same_slots =
        static_cast<std::int32_t>(((ok_a ? same_a * 1000 / ok_a : 0) + (ok_b ? same_b * 1000 / ok_b : 0)) / 2);
    std::vector<std::int32_t> ids;
    for (std::int32_t id = 1; id < 65536; ++id)
        if (count[static_cast<std::size_t>(id)]) ids.push_back(id);
    look.distinct_ids = static_cast<std::int32_t>(ids.size());
    std::stable_sort(ids.begin(), ids.end(), [&](std::int32_t p, std::int32_t q) {
        return count[static_cast<std::size_t>(p)] > count[static_cast<std::size_t>(q)];
    });
    // Same-id blobs of the 12 main ids (4-connected on the lattice).
    std::vector<std::int32_t> sizes, stack;
    std::vector<std::uint8_t> seen(lat.size(), 0), main_id(65536, 0);
    for (std::size_t k = 0; k < ids.size() && k < 12; ++k) main_id[static_cast<std::size_t>(ids[k])] = 1;
    for (std::size_t p0 = 0; p0 < lat.size(); ++p0) {
        {
            const std::int32_t id = lat[p0];
            if (id < 0 || !main_id[static_cast<std::size_t>(id)] || seen[p0]) continue;
            std::int32_t n = 0;
            seen[p0] = 1;
            stack.assign(1, static_cast<std::int32_t>(p0));
            while (!stack.empty()) {
                const std::int32_t p = stack.back();
                stack.pop_back();
                ++n;
                const std::int32_t a = p / sv, b = p % sv;
                const std::int32_t nb[4][2] = {{a + 1, b}, {a - 1, b}, {a, b + 1}, {a, b - 1}};
                for (const auto& q : nb) {
                    if (q[0] < 0 || q[0] >= su || q[1] < 0 || q[1] >= sv) continue;
                    const std::size_t qi = static_cast<std::size_t>(q[0]) * sv + q[1];
                    if (lat[qi] != id || seen[qi]) continue;
                    seen[qi] = 1;
                    stack.push_back(static_cast<std::int32_t>(qi));
                }
            }
            sizes.push_back(n);
        }
    }
    if (!sizes.empty()) {
        std::sort(sizes.begin(), sizes.end());
        look.blob_median = static_cast<std::int32_t>(median(sizes));
        const double pos = 0.9 * static_cast<double>(sizes.size() - 1);  // numpy's linear percentile
        const std::size_t lo = static_cast<std::size_t>(pos);
        const std::size_t hi = std::min(lo + 1, sizes.size() - 1);
        look.blob_p90 =
            static_cast<std::int32_t>(sizes[lo] + (sizes[hi] - sizes[lo]) * (pos - static_cast<double>(lo)) + 0.5);
    }

    // Windows of 12 cells x 24 rows.
    std::vector<std::int32_t> nd, top;
    std::vector<std::int32_t> wc(65536, 0);
    for (std::int32_t y0 = 20; y0 < H - 44; y0 += 24)
        for (std::int32_t x0 = 10; x0 < W - 22; x0 += 12) {
            std::vector<std::uint16_t> used;
            std::int32_t n = 0;
            for (std::int32_t y = y0; y < y0 + 24; ++y)
                for (std::int32_t x = x0; x < x0 + 12; ++x)
                    for (const std::uint16_t id : m.cells[static_cast<std::size_t>(y) * W + x].subtile) {
                        if (id == 0) continue;
                        if (wc[id]++ == 0) used.push_back(id);
                        ++n;
                    }
            if (n == 0) continue;
            std::int32_t many = 0, best = 0;
            for (const std::uint16_t id : used) {
                if (wc[id] * 100 >= n * 3) ++many;
                best = std::max(best, wc[id]);
                wc[id] = 0;
            }
            nd.push_back(many);
            top.push_back(best * 1000 / n);
        }
    look.window_ids = static_cast<std::int32_t>(median(nd) + 0.5);
    look.window_top = static_cast<std::int32_t>(median(top));
    return look;
}

}  // namespace antanta::sim
