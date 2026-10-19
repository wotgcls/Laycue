// How a Scripture or Lyrics slide looks: where it sits, what is behind the words, the colours and the size.
// Each plugin has its own templates (starting styles) and the operator can change any of them. Pure C++17.
#pragma once

#include <cstdint>
#include <string>

namespace laycue::slides {

// Where the slide sits and how it is built. Tab: a band with the reference in a tab above its corner. Rule: words
// beside a thick line. Quote: centred words with a large quotation mark. Split: a small square (the looping picture),
// a block with the reference and a block with the words, side by side. Torn: a band with a torn top edge, the
// reference in large letters on the left and the words in a dark box on the right.
enum class Layout { LowerThird = 0, Card = 1, FullScreen = 2, Tab = 3, Rule = 4, Quote = 5, Split = 6, Torn = 7 };
constexpr int kLayoutCount = 8;
// Moving: the two colours and the accent drift slowly past each other in a soft glow that rises from the bottom.
enum class Fill { None = 0, Solid = 1, SeeThrough = 2, Gradient = 3, Picture = 4, Moving = 5 };
constexpr int kFillCount = 6;
enum class TextCase { AsWritten = 0, Upper = 1, FirstLetter = 2 };
// How the words line up inside their box. It is a loose alignment: the box itself is placed by the layout.
enum class Align { Left = 0, Centre = 1, Right = 2 };
// The bundled typefaces. Bold broadcast capitals: Display (Bebas Neue), Anton, Compact (Barlow Condensed). Modern
// sans: Modern (Outfit), Clean (Barlow), Archivo. Premium serif: Classic (Libre Baskerville), Playfair Display,
// Cormorant Garamond. The numbers are saved, so new ones go at the end.
enum class Font {
	Modern = 0,
	Clean = 1,
	Compact = 2,
	Classic = 3,
	Display = 4,
	Anton = 5,
	Archivo = 6,
	Playfair = 7,
	Cormorant = 8
};
constexpr int kFontCount = 9;
// Fonts that only look right in capitals: the words are shown in capitals with them.
inline bool capitalsOnly(int font)
{
	return font == int(Font::Display) || font == int(Font::Anton);
}
// A small looping picture beside the words while the slide is on screen. The first ones suit verses, the last three
// suit songs.
enum class Decor {
	None = 0,
	OpenBible = 1,
	GlowBible = 2,
	Scroll = 3,
	Dove = 4,
	Cross = 5,
	Sunrise = 6,
	Notes = 7,
	Equalizer = 8,
	Vinyl = 9
};
constexpr int kDecorCount = 10;
// What happens when the words change: the old ones fade out and the new fade in, or they swap at once.
enum class Transition { Fade = 0, Cut = 1 };

constexpr int kMinSize = 60;
constexpr int kMaxSize = 160;
constexpr int kSizeStep = 10;

struct SlideStyle {
	int layout = int(Layout::LowerThird);
	int fill = int(Fill::Gradient);
	uint32_t color1 = 0x0C101C; // 0xRRGGBB: the background colour, or where the gradient starts
	uint32_t color2 = 0x1B2A4A; // where the gradient ends
	uint32_t color3 = 0x7B2FF7; // the third colour of the moving colours
	std::string picture;        // for Fill::Picture
	uint32_t text = 0xFFFFFF;   // the words
	uint32_t accent = 0xE0A91E; // the reference or song name, and edges
	int textCase = int(TextCase::AsWritten);
	int sizePercent = 100;
	int align = int(Align::Centre);
	int font = int(Font::Modern);
	int decor = int(Decor::None);
	int transition = int(Transition::Fade);

	bool operator==(const SlideStyle &o) const
	{
		return layout == o.layout && fill == o.fill && color1 == o.color1 && color2 == o.color2 &&
		       color3 == o.color3 && picture == o.picture && text == o.text && accent == o.accent && textCase == o.textCase &&
		       sizePercent == o.sizePercent && align == o.align && font == o.font && decor == o.decor &&
		       transition == o.transition;
	}
	bool operator!=(const SlideStyle &o) const { return !(*this == o); }
};

// Keeps every value in range (an old or hand-edited file can't break the picture).
void sanitize(SlideStyle &style);

// The background part of a style (fill, colours, picture), for "use the Scripture background" in Lyrics.
void copyBackground(const SlideStyle &from, SlideStyle &to);

// Whether the slide has anything that moves while it is up (a looping picture or the moving colours), so it has to be
// redrawn while on screen.
bool isAnimated(const SlideStyle &style);

} // namespace laycue::slides
