// The painted words of the original's pictures (docs/languages.md 3, slices L4 and L4b): the buttons, plaques and
// headings whose English is part of the picture. For a language other than English the picture is changed once, when
// it is loaded: the old words are taken out (the blank) and the translated text is drawn in their place with the
// colours, shadow and glow of each state. Everything is made from the player's own game files at run time; nothing
// derived from them is stored. English keeps the original picture untouched, and so does any word whose translation
// is the English text.
//
// The table names each picture (archive and file), our text for it (a `pic.*` key of engine/lang/<code>/ui.txt), the
// box its words take in each state and how the blank is made. No SDL here: the caller draws the text into coverage
// masks (PaintRaster), so the pixel work is tested on made-up pictures.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/sprites.hpp"

namespace antanta::ui {

// How the old words are taken out of the box.
// - Under: the window's background picture has the same art without the words where the button lies on it (the main
//   menu's strips, the single player menu's cartridges and its compass); the words' pixels are taken from there, its
//   colour matched to the button's around them.
// - Rows: the words' pixels are taken from the same rows left or right of them (the grain of wood and metal strips and
//   of plaques runs along the rows), the colour matched at both ends of each run.
// - Cols: the words' pixels blended from the pixels just above and below them in the same column (the glass globes,
//   whose shading runs from the top to the bottom).
// - Tiles: a heading of letter tiles (L4b), the box being the tiles' band: the tiles are found in it (cut at the thin
//   dark separators, the background between the words where their lightest pixels turn dark), each tile's letter taken
//   off with the same place of the nearest other tile, and the heading laid out again with one tile per letter, the
//   letters in the look's font as tall as the original's.
enum class PaintFill : std::uint8_t { Under, Rows, Cols, Tiles };

// The font of the words: the menu font (Times New Roman Bold, else Liberation Serif Bold) or the display font of the
// tile headings (an OFL stencil font built into the engine, docs/languages.md 9 item 2).
enum class PaintFont : std::uint8_t { Menu, Display };

// The look of the words in one state: the text colour, and the halo under it (a shadow when moved, a glow when not):
// the text's coverage grown by `grow` pixels, blurred by a box of radius `blur`, at `strength` / 64 opacity (above 64
// the soft edge saturates), moved by (dx, dy). Colours 0xRRGGBB.
struct PaintInk {
    std::uint32_t text = 0;
    std::uint32_t halo = 0;
    std::int8_t dx = 0, dy = 0;
    std::uint8_t grow = 0, blur = 0, strength = 0;
};

// A look shared by a group of pictures: the font, its em in pixels and the smallest em before the text takes a second
// line (where `lines` is 2 and the box has the height) or is drawn narrower, and the ink of each state (normal,
// pressed, under the mouse: the button's three cells, frontend.cpp draw order; one-cell pictures use the first).
struct PaintLook {
    PaintFont font = PaintFont::Menu;
    int em = 15, min_em = 12;
    int lines = 1;
    int tracking = 0;  // tenths of a pixel added after each letter (the main menu's letters stand apart)
    std::array<PaintInk, 3> ink{};
};

// One painted picture. `archive` and `picture` are lower case file names (the archive's without its folder). The box
// (x, y, w, h) is in each cell: the old words lie inside it, the new text is centred on it. Under: the button's cell
// lies at (ux, uy) of the picture `under` of the same archive.
struct PaintedWord {
    const char* archive;
    const char* picture;
    const char* key;
    std::uint8_t cells;
    PaintFill fill;
    std::uint8_t look;
    int x, y, w, h;
    const char* under = nullptr;
    int ux = 0, uy = 0;
};

const std::vector<PaintedWord>& painted_words();
const PaintLook& painted_look(const PaintedWord& w);
// The table's entries for a picture of an archive (`archive` may be a path: its file name is compared; both without
// regard to case).
std::vector<const PaintedWord*> painted_find(std::string_view archive, std::string_view picture);
// Ours: the Options resolution picker's last item, Full screen (`key`, in every language, English too), made from the
// 1280 x 1024 picture with its words taken out. Not in the table: that picture itself keeps its words.
const PaintedWord& painted_full_screen();
// Its words taken out of the 1280 x 1024 picture (painted_blank, then smoothed along the rows); painted_draw puts the
// text on.
void painted_full_screen_blank(Image& img);

// One line of text drawn white: its coverage (0 to 255 per pixel, w x h), the baseline at row `ascent`, and the height
// of a capital letter above the baseline.
struct Coverage {
    int w = 0, h = 0, ascent = 0, cap = 0;
    std::vector<std::uint8_t> a;
};
using PaintRaster = std::function<Coverage(PaintFont font, int em, std::string_view text)>;

// Takes the old words out of the word's box in every cell of `img` (the picture, RGBA, as decoded: magenta keyed
// pixels are left alone and never used). `under` is the picture named by the entry for PaintFill::Under, else ignored.
// Tiles are left as they are: painted_draw lays them out again from the original.
void painted_blank(Image& img, const PaintedWord& w, const Image* under);
// Draws `text` into the word's box of every cell with that state's ink, centred; smaller down to the look's minimum,
// then on two lines where allowed, last narrower: never cut. PaintFill::Tiles: the heading laid out again in the box
// (squeezed to it when longer), centred where the original's tiles were.
void painted_draw(Image& img, const PaintedWord& w, const std::string& text, const PaintRaster& raster);
// Both: the picture as the language shows it.
void painted_apply(Image& img, const PaintedWord& w, const Image* under, const std::string& text,
                   const PaintRaster& raster);
// The mean difference of two pictures of the same size in 0..255 per channel, over the pixels that are not keyed in
// `a` (the English check of docs/languages.md 3.1: the English text drawn over the blank against the original); with
// `w`, only over its box and 4 pixels around it in each cell (the rest of a large picture is never touched).
double painted_difference(const Image& a, const Image& b, const PaintedWord* w = nullptr);

}  // namespace antanta::ui
