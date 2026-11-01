// Fonts of the original's GDI text through SDL3_ttf (docs/frontend.md 7), shared by the menus and the in-game
// interface.
#pragma once

#include <SDL3_ttf/SDL_ttf.h>

#include <source_location>
#include <string>
#include <string_view>

#include "ui/lang.hpp"
#include "ui/painted.hpp"

namespace antanta::ui {

// Opens a font as CreateFontA with a positive height does: GDI takes the largest whole em size whose cell (ascent +
// descent, from the font's VDMX table) is not more than the height. Times New Roman Bold asked for 18 gets em 15 and
// cell 17, asked for 16 gets em 13 and cell 15 (the cells measured on the original, docs/frontend.md 7; the em sizes
// from timesbd.ttf's VDMX table, and the text widths in the screenshots agree). FreeType's rounded ascent and descent
// give the same choice for both when both are rounded up: the font's ascent and descent are read at em 1000.
TTF_Font* open_gdi_font(const std::string& path, int height);

// Kerning off, as GDI's TextOut draws (no kerning by default). SDL3_ttf 3.2.2 without HarfBuzz puts a pair's kerning
// into that one glyph's offset and not into the pen advance, so a strongly kerned pair moves only its second letter
// and leaves a gap after it (the Russian "Усеченная": the -221/2048 em of У+с pulls с left, е stays). Returns `font`.
inline TTF_Font* no_kerning(TTF_Font* font) {
    if (font) TTF_SetFontKerning(font, false);
    return font;
}

// The marker check of docs/languages.md 0 at a place that draws text: in the pseudo-language a drawn text that did
// not come through the lookup goes to the log, once per text and place (ui/lang.hpp lang_check). Nothing without a
// language or outside `xx`. The place is the caller's.
void check_drawn(Language* lang, std::string_view text,
                 const std::source_location& where = std::source_location::current());

// One line of `text` drawn white as the coverage mask of the painted words (ui/painted.hpp): its alpha, the baseline
// and the height of a capital H. Empty text: an empty mask.
Coverage text_coverage(TTF_Font* font, std::string_view text);

}  // namespace antanta::ui
