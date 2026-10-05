#include "render/brand-icons.hpp"

namespace laycue::titles {

namespace {

struct IconSeg {
	char op; // M move, L line, C cubic, Z close
	float v[6];
};

#include "render/brand-icons-data.inc"

template<size_t N> QPainterPath build(const IconSeg (&segs)[N])
{
	QPainterPath path;
	path.setFillRule(Qt::WindingFill);
	for (const IconSeg &s : segs) {
		switch (s.op) {
		case 'M':
			path.moveTo(s.v[0], s.v[1]);
			break;
		case 'L':
			path.lineTo(s.v[0], s.v[1]);
			break;
		case 'C':
			path.cubicTo(s.v[0], s.v[1], s.v[2], s.v[3], s.v[4], s.v[5]);
			break;
		case 'Z':
			path.closeSubpath();
			break;
		default:
			break;
		}
	}
	return path;
}

} // namespace

QPainterPath brandIconPath(Brand brand)
{
	// Built once per thread: the panel preview and OBS's graphics thread both draw titles.
	thread_local const QPainterPath youtube = build(k_youtube);
	thread_local const QPainterPath instagram = build(k_instagram);
	thread_local const QPainterPath tiktok = build(k_tiktok);
	thread_local const QPainterPath facebook = build(k_facebook);
	switch (brand) {
	case Brand::YouTube:
		return youtube;
	case Brand::Instagram:
		return instagram;
	case Brand::TikTok:
		return tiktok;
	case Brand::Facebook:
		return facebook;
	}
	return QPainterPath();
}

const std::vector<LogoDot> &mixlrDots()
{
	static const std::vector<LogoDot> dots = {
		{0.5030f, 0.1432f, true},  {0.6805f, 0.1875f, true},  {0.3273f, 0.1943f, false},
		{0.8125f, 0.3143f, true},  {0.5072f, 0.3202f, false}, {0.2002f, 0.3265f, false},
		{0.6617f, 0.4055f, false}, {0.3558f, 0.4115f, false}, {0.8633f, 0.4898f, true},
		{0.1568f, 0.5042f, false}, {0.5072f, 0.5038f, true},  {0.6650f, 0.5823f, false},
		{0.3588f, 0.5883f, false}, {0.8200f, 0.6675f, false}, {0.5135f, 0.6733f, false},
		{0.2080f, 0.6800f, false}, {0.6935f, 0.7995f, false}, {0.3400f, 0.8067f, false},
		{0.5177f, 0.8503f, false},
	};
	return dots;
}

} // namespace laycue::titles
