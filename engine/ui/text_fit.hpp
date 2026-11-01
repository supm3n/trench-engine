// Text that fits its place (docs/languages.md 2.4 and 2.5, slice L2b): the place table of every one-line text place
// that must take a longer translation (its box, font, alignment and how many lines it may use), and the fitting order
// of rule 2: as it is; a smaller size of the same font down to the minimum; a second line where the place allows it;
// the short form (KEY.short, ui/lang.hpp lang_short); last an ellipsis, the full text then shows as a tooltip. Running
// text (messages, briefings, hints, F1) wraps instead and is not here. No SDL: the caller measures.
//
// English fits every place as it is (the overflow test checks it), so English is drawn exactly as before.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace antanta::ui {

enum class TextAlign : std::uint8_t { Left, Centre };






struct TextPlace {
    const char* name;
    int width;
    int em, min_em;
    int lines;
    TextAlign align;
    const char* keys;
};

// The place table (docs/languages.md 2.5 lists the same, with the places that need no fitting).
const std::vector<TextPlace>& text_places();
// The place called `name`; a place that is not in the table is a mistake in the code: it aborts.
const TextPlace& text_place(std::string_view name);

// How a text was fitted: the em it is drawn at and its lines (more than one only where the place allows), whether it
// is the short form and whether its last line ends in an ellipsis.
struct TextFit {
    int em = 0;
    std::vector<std::string> lines;
    bool shortened = false, ellipsis = false;
    // Drawn as it is: one line at the place's em (every English text).
    bool as_is(const TextPlace& p) const { return em == p.em && lines.size() == 1 && !shortened && !ellipsis; }
};

// The width in pixels of `text` at `em`.
using TextMeasure = std::function<int(int em, std::string_view text)>;

// The text in lines of at most `width` pixels, broken at spaces (a line break in the text always breaks); false when
// a single word is wider than the line.
bool text_wrap(std::string_view text, int width, const std::function<int(std::string_view)>& measure,
               std::vector<std::string>* lines);
// Fits `text` (and `short_form`, may be empty) into the place, in the order of rule 2. A text that needs no more than
// the place has comes back as it is (`as_is`).
TextFit text_fit(const TextPlace& p, std::string_view text, std::string_view short_form, const TextMeasure& measure);

}  // namespace antanta::ui
