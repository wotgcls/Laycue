#include "slides/slide-style-io.hpp"

namespace laycue::slides {

void writeStyle(obs_data_t *d, const SlideStyle &s)
{
	obs_data_set_int(d, "layout", s.layout);
	obs_data_set_int(d, "fill", s.fill);
	obs_data_set_int(d, "color1", s.color1);
	obs_data_set_int(d, "color2", s.color2);
	obs_data_set_string(d, "picture", s.picture.c_str());
	obs_data_set_int(d, "textColor", s.text);
	obs_data_set_int(d, "accent", s.accent);
	obs_data_set_int(d, "case", s.textCase);
	obs_data_set_int(d, "size", s.sizePercent);
}

SlideStyle readStyle(obs_data_t *d)
{
	SlideStyle s;
	if (!d)
		return s;
	const auto number = [&](const char *key, long long fallback) {
		return obs_data_has_user_value(d, key) ? obs_data_get_int(d, key) : fallback;
	};
	s.layout = int(number("layout", s.layout));
	s.fill = int(number("fill", s.fill));
	s.color1 = uint32_t(number("color1", s.color1));
	s.color2 = uint32_t(number("color2", s.color2));
	const char *picture = obs_data_get_string(d, "picture");
	s.picture = picture ? picture : "";
	s.text = uint32_t(number("textColor", s.text));
	s.accent = uint32_t(number("accent", s.accent));
	s.textCase = int(number("case", s.textCase));
	s.sizePercent = int(number("size", s.sizePercent));
	sanitize(s);
	return s;
}

} // namespace laycue::slides
