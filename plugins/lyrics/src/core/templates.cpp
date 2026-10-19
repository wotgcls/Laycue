#include "templates.hpp"

namespace laycue::lyrics {

namespace {

using slides::Align;
using slides::Decor;
using slides::Fill;
using slides::Font;
using slides::Layout;

slides::SlideStyle style(Layout layout, Fill fill, uint32_t background, uint32_t background2, uint32_t text,
			 uint32_t accent, Align align = Align::Centre, Font font = Font::Modern, Decor decor = Decor::None,
			 uint32_t c3 = 0x7B2FF7)
{
	slides::SlideStyle s;
	s.decor = int(decor);
	s.color3 = c3;
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
							   "Laycue.Lyrics.Pack.Sanctuary", "Laycue.Lyrics.Pack.Worship"};
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
		// Worship: a moving music picture beside the words, or moving colours.
		{"Laycue.Lyrics.T.Notes", 3,
		 style(Layout::LowerThird, Fill::Gradient, 0x12082E, 0x2A0F5C, 0xFFFFFF, 0xFFC94D, Align::Left,
		       Font::Modern, Decor::Notes)},
		{"Laycue.Lyrics.T.Equalizer", 3,
		 style(Layout::Tab, Fill::SeeThrough, 0x07090F, 0x07090F, 0xFFFFFF, 0x2FD18B, Align::Left, Font::Clean,
		       Decor::Equalizer)},
		{"Laycue.Lyrics.T.Vinyl", 3,
		 style(Layout::Card, Fill::Gradient, 0x1B1B22, 0x34202E, 0xFFFFFF, 0xFF5C8A, Align::Left, Font::Modern,
		       Decor::Vinyl)},
		{"Laycue.Lyrics.T.Aurora", 3,
		 style(Layout::LowerThird, Fill::Moving, 0x1A0B4A, 0xC2185B, 0xFFFFFF, 0xFFD54F, Align::Centre,
		       Font::Modern, Decor::None, 0x3D5AFE)},
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
