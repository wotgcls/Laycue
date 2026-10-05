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

} // namespace laycue::titles
