#include "slides/slide-source.hpp"

#include "slides/slide-render.hpp"
#include "slides/slide-style-io.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <map>
#include <mutex>
#include <string>

namespace laycue::slides {

namespace {

constexpr double kFadeSeconds = 0.25;

struct Content {
	std::string text;
	std::string caption;
	SlideStyle style;
	bool operator==(const Content &o) const { return text == o.text && caption == o.caption && style == o.style; }
	bool operator!=(const Content &o) const { return !(*this == o); }
};

// A picture drawn over part of the canvas: `frame` is stretched over `box`.
struct Layer {
	QImage frame;
	QRect box;
	bool upload = false;
	gs_texture_t *texture = nullptr;
	uint32_t textureW = 0, textureH = 0;

	void set(const QImage &image, const QRect &where)
	{
		frame = image.isNull() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
		box = where;
		upload = true;
	}
	void clear() { frame = QImage(); }
	// Graphics thread only.
	void draw(gs_eparam_t *image)
	{
		if (frame.isNull() || box.isEmpty())
			return;
		const uint32_t w = uint32_t(frame.width()), h = uint32_t(frame.height());
		if (!texture || textureW != w || textureH != h) {
			if (texture)
				gs_texture_destroy(texture);
			const uint8_t *bits = frame.constBits();
			texture = gs_texture_create(w, h, GS_RGBA, 1, &bits, GS_DYNAMIC);
			textureW = w;
			textureH = h;
			upload = false;
		} else if (upload) {
			gs_texture_set_image(texture, frame.constBits(), uint32_t(frame.bytesPerLine()), false);
			upload = false;
		}
		if (!texture)
			return;
		gs_matrix_push();
		gs_matrix_translate3f(float(box.x()), float(box.y()), 0.0f);
		gs_effect_set_texture(image, texture);
		gs_draw_sprite(texture, 0, uint32_t(box.width()), uint32_t(box.height()));
		gs_matrix_pop();
	}
	void destroy()
	{
		if (texture)
			gs_texture_destroy(texture);
		texture = nullptr;
	}
};

struct SlideSource {
	obs_source_t *source = nullptr;
	std::mutex mutex;
	Content showing; // what the picture holds
	Content wanted;  // what the panel asked for
	bool shown = false;
	double opacity = 0.0;
	bool rerender = true;
	QImage base;  // the full-opacity picture of `showing`
	QImage frame; // what is uploaded (base at the current opacity)
	double frameOpacity = -1.0;
	bool uploadPending = false;
	QImage backgroundImage;
	std::string backgroundPath;
	gs_texture_t *texture = nullptr;
	uint32_t textureW = 0, textureH = 0;
	uint32_t canvasW = 1920, canvasH = 1080;
	// What moves while the words are up, each its own picture and texture, redrawn 30 times a second: the
	// moving colours under the slide, the looping picture beside the words, and the sweeping light on the line.
	Layer backdrop;
	Layer decor;
	Layer accent;
	double animSeconds = 0.0;
	double animSince = 1.0;
};

constexpr double kDecorInterval = 1.0 / 30.0;

bool hasWords(const Content &c)
{
	const auto blank = [](const std::string &t) {
		return t.find_first_not_of(" \t\r\n") == std::string::npos;
	};
	return !blank(c.text) || !blank(c.caption);
}

std::map<std::string, std::string> &nameKeys()
{
	static std::map<std::string, std::string> keys; // source id -> locale key
	return keys;
}

std::map<std::string, std::function<void()>> &reveals()
{
	static std::map<std::string, std::function<void()>> map; // source id -> bring the panel forward
	return map;
}

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

void canvasSize(uint32_t &w, uint32_t &h)
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
		w = ovi.base_width;
		h = ovi.base_height;
	}
}

void update(void *data, obs_data_t *d)
{
	auto *s = static_cast<SlideSource *>(data);
	try {
		std::lock_guard<std::mutex> lock(s->mutex);
		Content c;
		c.text = str(obs_data_get_string(d, key::text));
		c.caption = str(obs_data_get_string(d, key::caption));
		c.style = readStyle(d);
		s->wanted = c;
		s->shown = obs_data_get_bool(d, key::shown);
	} catch (...) {
		obs_log(LOG_ERROR, "Slide source: could not apply settings");
	}
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new SlideSource();
	s->source = source;
	canvasSize(s->canvasW, s->canvasH);
	update(s, settings);
	s->showing = s->wanted;
	s->opacity = s->shown ? 1.0 : 0.0; // a saved slide comes back as it was, without a fade
	return s;
}

void destroy(void *data)
{
	auto *s = static_cast<SlideSource *>(data);
	if (s->texture || s->decor.texture || s->backdrop.texture || s->accent.texture) {
		obs_enter_graphics();
		if (s->texture)
			gs_texture_destroy(s->texture);
		s->decor.destroy();
		s->backdrop.destroy();
		s->accent.destroy();
		obs_leave_graphics();
	}
	delete s;
}

const char *getName(void *typeData)
{
	const auto *id = static_cast<const std::string *>(typeData);
	const auto found = id ? nameKeys().find(*id) : nameKeys().end();
	return found != nameKeys().end() ? obs_module_text(found->second.c_str()) : "Laycue";
}

uint32_t getWidth(void *data)
{
	return static_cast<SlideSource *>(data)->canvasW;
}

uint32_t getHeight(void *data)
{
	return static_cast<SlideSource *>(data)->canvasH;
}

void getDefaults(obs_data_t *d)
{
	obs_data_set_default_bool(d, key::shown, false);
}

void tick(void *data, float seconds)
{
	auto *s = static_cast<SlideSource *>(data);
	try {
		std::lock_guard<std::mutex> lock(s->mutex);
		uint32_t w = s->canvasW, h = s->canvasH;
		canvasSize(w, h);
		if (w != s->canvasW || h != s->canvasH) {
			s->canvasW = w;
			s->canvasH = h;
			s->rerender = true;
		}
		// New words while on screen: the old ones fade out first, then the new ones fade in.
		// With "Cut" they swap at once and the picture stays where it is.
		bool changing = s->wanted != s->showing;
		if (changing && s->wanted.style.transition == int(Transition::Cut)) {
			s->showing = s->wanted;
			s->rerender = true;
			changing = false;
		}
		const double target = (s->shown && !changing) ? 1.0 : 0.0;
		const double step = std::max(0.0, double(seconds)) / kFadeSeconds;
		if (s->opacity < target)
			s->opacity = std::min(target, s->opacity + step);
		else if (s->opacity > target)
			s->opacity = std::max(target, s->opacity - step);
		if (changing && s->opacity <= 0.0) {
			s->showing = s->wanted;
			s->rerender = true;
		}
		if (s->rerender) {
			const std::string wantedPicture =
				s->showing.style.fill == int(Fill::Picture) ? s->showing.style.picture : std::string();
			if (wantedPicture != s->backgroundPath) {
				s->backgroundPath = wantedPicture;
				s->backgroundImage = s->backgroundPath.empty()
							     ? QImage()
							     : QImage(QString::fromStdString(s->backgroundPath));
			}
			SlideRequest r;
			r.text = QString::fromStdString(s->showing.text);
			r.caption = QString::fromStdString(s->showing.caption);
			r.style = s->showing.style;
			r.picture = s->backgroundImage;
			r.canvasWidth = int(s->canvasW);
			r.canvasHeight = int(s->canvasH);
			s->base = renderSlide(r);
			s->rerender = false;
			s->frameOpacity = -1.0;
		}
		if (s->opacity != s->frameOpacity && !s->base.isNull()) {
			if (s->opacity >= 1.0) {
				s->frame = s->base.convertToFormat(QImage::Format_RGBA8888);
			} else {
				QImage faded(s->base.size(), QImage::Format_ARGB32_Premultiplied);
				faded.fill(Qt::transparent);
				if (s->opacity > 0.0) {
					QPainter p(&faded);
					p.setOpacity(s->opacity);
					p.drawImage(0, 0, s->base);
				}
				s->frame = faded.convertToFormat(QImage::Format_RGBA8888); // OBS draws straight alpha
			}
			s->frameOpacity = s->opacity;
			s->uploadPending = true;
		}
		// What moves: redrawn on its own while the words are up.
		const Content &now = s->showing;
		if (isAnimated(now.style) && hasWords(now) && s->opacity > 0.0) {
			s->animSeconds += std::max(0.0, double(seconds));
			s->animSince += std::max(0.0, double(seconds));
			if (s->animSince >= kDecorInterval || s->opacity < 1.0 ||
			    (s->decor.frame.isNull() && s->backdrop.frame.isNull())) {
				s->animSince = 0.0;
				const int W = int(s->canvasW), H = int(s->canvasH);
				const QRect decorBox = decorRect(now.style, W, H);
				if (decorBox.isEmpty())
					s->decor.clear();
				else
					s->decor.set(renderDecor(now.style, s->animSeconds, decorBox.size(),
								 s->opacity),
						     decorBox);
				const QRect backBox = backdropRect(now.style, W, H);
				if (backBox.isEmpty())
					s->backdrop.clear();
				else
					s->backdrop.set(renderBackdrop(now.style, s->animSeconds, W, H, 0.25,
								       s->opacity),
							backBox);
				const QRect lineBox = accentLineRect(now.style, W, H);
				if (lineBox.isEmpty())
					s->accent.clear();
				else
					s->accent.set(renderAccentLine(now.style, s->animSeconds, lineBox.size(),
								       s->opacity),
						      lineBox);
			}
		} else {
			s->decor.clear();
			s->backdrop.clear();
			s->accent.clear();
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Slide source: could not draw a frame");
	}
}

void render(void *data, gs_effect_t *effect)
{
	auto *s = static_cast<SlideSource *>(data);
	try {
		std::lock_guard<std::mutex> lock(s->mutex);
		if (s->frame.isNull() || !effect || s->frameOpacity <= 0.0)
			return;
		gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
		s->backdrop.draw(image); // the moving colours go under the words
		const uint32_t w = uint32_t(s->frame.width()), h = uint32_t(s->frame.height());
		if (!s->texture || s->textureW != w || s->textureH != h) {
			if (s->texture)
				gs_texture_destroy(s->texture);
			const uint8_t *bits = s->frame.constBits();
			s->texture = gs_texture_create(w, h, GS_RGBA, 1, &bits, GS_DYNAMIC);
			s->textureW = w;
			s->textureH = h;
			s->uploadPending = false;
		} else if (s->uploadPending) {
			gs_texture_set_image(s->texture, s->frame.constBits(), uint32_t(s->frame.bytesPerLine()),
					     false);
			s->uploadPending = false;
		}
		if (!s->texture)
			return;
		gs_effect_set_texture(image, s->texture);
		gs_draw_sprite(s->texture, 0, w, h);
		s->decor.draw(image);
		s->accent.draw(image); // the sweeping light rides exactly on the line
	} catch (...) {
		obs_log(LOG_ERROR, "Slide source: could not draw");
	}
}

static bool openPanel(obs_properties_t *, obs_property_t *, void *data)
{
	auto *s = static_cast<SlideSource *>(data);
	const char *id = s && s->source ? obs_source_get_id(s->source) : nullptr;
	if (!id)
		return false;
	const auto bring = reveals().find(id);
	if (bring != reveals().end() && bring->second)
		bring->second();
	return true;
}

// What a double click on the source in the Sources list opens: one clear button that brings the panel
// forward, since the source itself is driven from its panel and has no settings of its own.
obs_properties_t *getProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	auto *s = static_cast<SlideSource *>(data);
	const char *id = s && s->source ? obs_source_get_id(s->source) : nullptr;
	const auto bring = id ? reveals().find(id) : reveals().end();
	if (bring != reveals().end() && bring->second) {
		const auto name = nameKeys().find(id);
		std::string label = name != nameKeys().end() ? obs_module_text(name->second.c_str()) : "Laycue";
		label += " panel"; // e.g. "Laycue Lyrics panel"
		obs_properties_add_button(props, "laycue_open_panel", label.c_str(), openPanel);
	}
	return props;
}

} // namespace

void registerSlideSource(const char *id, const char *nameKey)
{
	nameKeys()[id] = nameKey;
	obs_source_info info = {};
	info.id = id;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = getName;
	info.type_data = new std::string(id); // lives as long as the module
	info.free_type_data = [](void *p) {
		delete static_cast<std::string *>(p);
	};
	info.create = create;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.get_width = getWidth;
	info.get_height = getHeight;
	info.video_tick = tick;
	info.video_render = render;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}

void registerSourceReveal(const char *id, const std::function<void()> &bringForward)
{
	if (bringForward)
		reveals()[id] = bringForward;
}

} // namespace laycue::slides
