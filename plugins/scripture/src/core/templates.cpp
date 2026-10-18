#include "templates.hpp"

namespace laycue::scripture {

namespace {

using slides::Align;
using slides::Decor;
using slides::Fill;
using slides::Font;
using slides::Layout;

slides::SlideStyle style(Layout layout, Fill fill, uint32_t c1, uint32_t c2, uint32_t text, uint32_t accent,
			 Align align, Font font, Decor decor = Decor::None)
{
	slides::SlideStyle s;
	s.layout = int(layout);
	s.fill = int(fill);
	s.color1 = c1;
	s.color2 = c2;
	s.text = text;
	s.accent = accent;
	s.align = int(align);
	s.font = int(font);
	s.decor = int(decor);
	return s;
}

} // namespace

const char *scripturePackKey(int pack)
{
	static const char *const keys[kScripturePackCount] = {"Laycue.Scripture.Pack.Classic",
							      "Laycue.Scripture.Pack.Modern",
							      "Laycue.Scripture.Pack.Sanctuary"};
	return keys[pack < 0 ? 0 : (pack >= kScripturePackCount ? kScripturePackCount - 1 : pack)];
}

// The first three keep their places (saved looks are stored by position); the rest follow.
const std::vector<ScriptureTemplate> &scriptureTemplates()
{
	static const std::vector<ScriptureTemplate> all = {
		// Classic: dependable bars and cards.
		{"Laycue.Scripture.T.Bar", 0,
		 style(Layout::LowerThird, Fill::Gradient, 0x0C101C, 0x1B2A4A, 0xFFFFFF, 0xE0A91E, Align::Centre,
		       Font::Modern)},
		{"Laycue.Scripture.T.Card", 0,
		 style(Layout::Card, Fill::Solid, 0x14213D, 0x14213D, 0xFFFFFF, 0xE0A91E, Align::Centre, Font::Modern)},
		{"Laycue.Scripture.T.Full", 0,
		 style(Layout::FullScreen, Fill::Gradient, 0x14245C, 0x3A1E5E, 0xFFFFFF, 0xE0A91E, Align::Centre,
		       Font::Modern)},
		{"Laycue.Scripture.T.Clear", 0,
		 style(Layout::LowerThird, Fill::None, 0x000000, 0x000000, 0xFFFFFF, 0xFFE99A, Align::Centre,
		       Font::Modern)},
		// Modern: clean lines, left-aligned.
		{"Laycue.Scripture.T.Tab", 1,
		 style(Layout::Tab, Fill::SeeThrough, 0x0E0E10, 0x0E0E10, 0xFFFFFF, 0xD8352A, Align::Left,
		       Font::Compact)},
		{"Laycue.Scripture.T.Rule", 1,
		 style(Layout::Rule, Fill::SeeThrough, 0x0B0F19, 0x0B0F19, 0xFFFFFF, 0x19C3A5, Align::Left,
		       Font::Clean)},
		{"Laycue.Scripture.T.Clean", 1,
		 style(Layout::LowerThird, Fill::Solid, 0xFFFFFF, 0xFFFFFF, 0x14161C, 0xD8352A, Align::Left,
		       Font::Clean)},
		{"Laycue.Scripture.T.Pulpit", 1,
		 style(Layout::Quote, Fill::None, 0x000000, 0x000000, 0xFFFFFF, 0xFFB31A, Align::Centre,
		       Font::Classic)},
		// Sanctuary: warm and reverent, with the looping Bible beside the words.
		{"Laycue.Scripture.T.OpenBible", 2,
		 style(Layout::Card, Fill::Gradient, 0x141B3C, 0x2B1F4F, 0xFFFFFF, 0xE0B04A, Align::Left, Font::Classic,
		       Decor::OpenBible)},
		{"Laycue.Scripture.T.Glow", 2,
		 style(Layout::FullScreen, Fill::Gradient, 0x0B1B3A, 0x18386B, 0xFFFFFF, 0xF2C14E, Align::Right,
		       Font::Classic, Decor::GlowBible)},
		{"Laycue.Scripture.T.Parchment", 2,
		 style(Layout::Card, Fill::Solid, 0xF3E9D2, 0xF3E9D2, 0x2B2118, 0x9A3B26, Align::Left, Font::Classic)},
		{"Laycue.Scripture.T.Royal", 2,
		 style(Layout::Rule, Fill::Gradient, 0x2D1252, 0x5B1F7A, 0xFFFFFF, 0xF0C75E, Align::Left, Font::Classic,
		       Decor::GlowBible)},
	};
	return all;
}

std::vector<int> templatesOfPack(int pack)
{
	std::vector<int> out;
	const auto &all = scriptureTemplates();
	for (size_t i = 0; i < all.size(); ++i) {
		if (all[i].pack == pack)
			out.push_back(int(i));
	}
	return out;
}

} // namespace laycue::scripture
