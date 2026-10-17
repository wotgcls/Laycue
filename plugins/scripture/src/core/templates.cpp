#include "core/templates.hpp"

namespace laycue::scripture {

namespace {

slides::SlideStyle style(slides::Layout layout, slides::Fill fill, uint32_t c1, uint32_t c2, uint32_t accent)
{
	slides::SlideStyle s;
	s.layout = int(layout);
	s.fill = int(fill);
	s.color1 = c1;
	s.color2 = c2;
	s.text = 0xFFFFFF;
	s.accent = accent;
	return s;
}

} // namespace

const std::vector<ScriptureTemplate> &scriptureTemplates()
{
	using slides::Fill;
	using slides::Layout;
	static const std::vector<ScriptureTemplate> all = {
		{"Laycue.Scripture.T.Bar", style(Layout::LowerThird, Fill::Gradient, 0x0C101C, 0x1B2A4A, 0xE0A91E)},
		{"Laycue.Scripture.T.Card", style(Layout::Card, Fill::Solid, 0x14213D, 0x14213D, 0xE0A91E)},
		{"Laycue.Scripture.T.Full", style(Layout::FullScreen, Fill::Gradient, 0x14245C, 0x3A1E5E, 0xE0A91E)},
	};
	return all;
}

} // namespace laycue::scripture
