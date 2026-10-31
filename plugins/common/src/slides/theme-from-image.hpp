// A theme read out of a picture: the two colours of the background gradient, a third for the moving glow,
// an accent for the reference line, and a word colour that stays readable over all of it. The picture is
// never copied — only its colour feeling is.
#pragma once

#include "slides/slide-style.hpp"

#include <QString>

namespace laycue::slides {

// `ok` is false when the picture could not be read or carried no colour worth using; the caller keeps the
// style it had.
struct ImageTheme {
	bool ok = false;
	uint32_t color1 = 0, color2 = 0, color3 = 0, text = 0xFFFFFF, accent = 0xE0A91E;
};

ImageTheme themeFromImage(const QString &pictureFile);

// The theme laid onto a style: only the colours move — layout, fonts, fill choice stay as the operator set
// them (a Moving fill is switched to Gradient, which is what a theme's two colours are for).
void applyTheme(SlideStyle &style, const ImageTheme &theme);

} // namespace laycue::slides
