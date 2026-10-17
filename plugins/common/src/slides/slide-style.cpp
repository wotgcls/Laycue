#include "slides/slide-style.hpp"

#include <algorithm>

namespace laycue::slides {

void sanitize(SlideStyle &s)
{
	s.layout = std::clamp(s.layout, 0, int(Layout::FullScreen));
	s.fill = std::clamp(s.fill, 0, int(Fill::Picture));
	s.textCase = std::clamp(s.textCase, 0, int(TextCase::FirstLetter));
	s.sizePercent = std::clamp(s.sizePercent, kMinSize, kMaxSize);
	s.color1 &= 0xFFFFFF;
	s.color2 &= 0xFFFFFF;
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
	to.picture = from.picture;
}

} // namespace laycue::slides
