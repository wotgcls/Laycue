#include "templates.hpp"

namespace laycue::lyrics {

namespace {

using slides::Align;
using slides::Fill;
using slides::Font;
using slides::Layout;

slides::SlideStyle style(Layout layout, Fill fill, uint32_t background, uint32_t background2, uint32_t text,
			 uint32_t accent, Align align = Align::Centre, Font font = Font::Modern)
{
	slides::SlideStyle s;
	s.layout = int(layout);
	s.fill = int(fill);
	s.color1 = background;
	s.color2 = background2;
	s.text = text;
	s.accent = accent;
	s.align = int(align);
	s.font = int(font);
	return s;
}

} // namespace

const char *lyricsPackKey(int pack)
{
	static const char *const keys[kLyricsPackCount] = {"Laycue.Lyrics.Pack.Classic", "Laycue.Lyrics.Pack.Modern",
							   "Laycue.Lyrics.Pack.Sanctuary"};
	return keys[pack < 0 ? 0 : (pack >= kLyricsPackCount ? kLyricsPackCount - 1 : pack)];
}

// The first four keep their places (saved looks are stored by position); the rest follow.
const std::vector<LyricsTemplate> &lyricsTemplates()
{
	static const std::vector<LyricsTemplate> all = {
		{"Laycue.Lyrics.T.Black", 0,
		 style(Layout::LowerThird, Fill::Solid, 0x000000, 0x000000, 0xFFFFFF, 0xBFC5CF)},
		{"Laycue.Lyrics.T.White", 0,
		 style(Layout::LowerThird, Fill::Solid, 0xFFFFFF, 0xFFFFFF, 0x111111, 0x5A6170)},
		{"Laycue.Lyrics.T.Faded", 0,
		 style(Layout::LowerThird, Fill::SeeThrough, 0x000000, 0x000000, 0xFFFFFF, 0xE0A91E)},
		{"Laycue.Lyrics.T.None", 0,
		 style(Layout::LowerThird, Fill::None, 0x000000, 0x000000, 0xFFFFFF, 0xFFE99A)},
		{"Laycue.Lyrics.T.Rule", 1,
		 style(Layout::Rule, Fill::SeeThrough, 0x0B0F19, 0x0B0F19, 0xFFFFFF, 0x19C3A5, Align::Left,
		       Font::Clean)},
		{"Laycue.Lyrics.T.Clean", 1,
		 style(Layout::LowerThird, Fill::Solid, 0xFFFFFF, 0xFFFFFF, 0x14161C, 0xD8352A, Align::Left,
		       Font::Clean)},
		{"Laycue.Lyrics.T.Night", 2,
		 style(Layout::FullScreen, Fill::Gradient, 0x0B1B3A, 0x18386B, 0xFFFFFF, 0xF2C14E, Align::Centre,
		       Font::Classic)},
		{"Laycue.Lyrics.T.Parchment", 2,
		 style(Layout::Card, Fill::Solid, 0xF3E9D2, 0xF3E9D2, 0x2B2118, 0x9A3B26, Align::Left, Font::Classic)},
	};
	return all;
}

std::vector<int> templatesOfPack(int pack)
{
	std::vector<int> out;
	const auto &all = lyricsTemplates();
	for (size_t i = 0; i < all.size(); ++i) {
		if (all[i].pack == pack)
			out.push_back(int(i));
	}
	return out;
}

} // namespace laycue::lyrics
