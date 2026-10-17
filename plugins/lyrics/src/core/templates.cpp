#include "core/templates.hpp"

namespace laycue::lyrics {

namespace {

slides::SlideStyle style(slides::Fill fill, uint32_t background, uint32_t text, uint32_t accent)
{
	slides::SlideStyle s;
	s.layout = int(slides::Layout::LowerThird);
	s.fill = int(fill);
	s.color1 = background;
	s.color2 = background;
	s.text = text;
	s.accent = accent;
	return s;
}

} // namespace

const std::vector<LyricsTemplate> &lyricsTemplates()
{
	using slides::Fill;
	static const std::vector<LyricsTemplate> all = {
		{"Laycue.Lyrics.T.Black", style(Fill::Solid, 0x000000, 0xFFFFFF, 0xBFC5CF)},
		{"Laycue.Lyrics.T.White", style(Fill::Solid, 0xFFFFFF, 0x111111, 0x5A6170)},
		{"Laycue.Lyrics.T.Faded", style(Fill::SeeThrough, 0x000000, 0xFFFFFF, 0xE0A91E)},
		{"Laycue.Lyrics.T.None", style(Fill::None, 0x000000, 0xFFFFFF, 0xFFE99A)},
	};
	return all;
}

} // namespace laycue::lyrics
