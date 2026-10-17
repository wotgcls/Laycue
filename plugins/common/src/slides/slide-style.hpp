// How a Scripture or Lyrics slide looks: where it sits, what is behind the words, the colours and the size.
// Each plugin has its own templates (starting styles) and the operator can change any of them. Pure C++17.
#pragma once

#include <cstdint>
#include <string>

namespace laycue::slides {

enum class Layout { LowerThird = 0, Card = 1, FullScreen = 2 };
enum class Fill { None = 0, Solid = 1, SeeThrough = 2, Gradient = 3, Picture = 4 };
enum class TextCase { AsWritten = 0, Upper = 1, FirstLetter = 2 };

constexpr int kMinSize = 60;
constexpr int kMaxSize = 160;
constexpr int kSizeStep = 10;

struct SlideStyle {
	int layout = int(Layout::LowerThird);
	int fill = int(Fill::Gradient);
	uint32_t color1 = 0x0C101C; // 0xRRGGBB: the background colour, or where the gradient starts
	uint32_t color2 = 0x1B2A4A; // where the gradient ends
	std::string picture;        // for Fill::Picture
	uint32_t text = 0xFFFFFF;   // the words
	uint32_t accent = 0xE0A91E; // the reference or song name, and edges
	int textCase = int(TextCase::AsWritten);
	int sizePercent = 100;

	bool operator==(const SlideStyle &o) const
	{
		return layout == o.layout && fill == o.fill && color1 == o.color1 && color2 == o.color2 &&
		       picture == o.picture && text == o.text && accent == o.accent && textCase == o.textCase &&
		       sizePercent == o.sizePercent;
	}
	bool operator!=(const SlideStyle &o) const { return !(*this == o); }
};

// Keeps every value in range (an old or hand-edited file can't break the picture).
void sanitize(SlideStyle &style);

// The background part of a style (fill, colours, picture), for "use the Scripture background" in Lyrics.
void copyBackground(const SlideStyle &from, SlideStyle &to);

} // namespace laycue::slides
