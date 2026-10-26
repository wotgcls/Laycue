#include "slides/slide-style.hpp"

#include <algorithm>

namespace laycue::slides {

void sanitize(SlideStyle &s)
{
	s.layout = std::clamp(s.layout, 0, kLayoutCount - 1);
	s.fill = std::clamp(s.fill, 0, kFillCount - 1);
	s.textCase = std::clamp(s.textCase, 0, int(TextCase::FirstLetter));
	s.sizePercent = std::clamp(s.sizePercent, kMinSize, kMaxSize);
	s.align = std::clamp(s.align, 0, int(Align::Right));
	s.font = std::clamp(s.font, 0, kFontCount - 1);
	s.decor = std::clamp(s.decor, 0, kDecorCount - 1);
	s.transition = std::clamp(s.transition, 0, int(Transition::Cut));
	s.line = std::clamp(s.line, 0, kAccentLineCount - 1);
	s.color1 &= 0xFFFFFF;
	s.color2 &= 0xFFFFFF;
	s.color3 &= 0xFFFFFF;
	s.text &= 0xFFFFFF;
	s.accent &= 0xFFFFFF;
	if (s.fill == int(Fill::Picture) && s.picture.empty())
		s.fill = int(Fill::Gradient); // no picture chosen yet: the gradient stands in
}

void copyBackground(const SlideStyle &from, SlideStyle &to)
{
	to.fill = from.fill;
	to.color1 = from.color1;
	to.color2 = from.color2;
	to.color3 = from.color3;
	to.picture = from.picture;
}

bool isAnimated(const SlideStyle &style)
{
	return style.decor != int(Decor::None) || style.fill == int(Fill::Moving) ||
	       style.line == int(AccentLine::Sweep);
}

} // namespace laycue::slides
