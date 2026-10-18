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
	// The looping Bible picture beside the words: its own small picture and texture, redrawn 30 times a second.
	QImage decorFrame;
	QRect decorBox;
	double decorSeconds = 0.0;
	double decorSince = 1.0;
	bool decorUpload = false;
	gs_texture_t *decorTexture = nullptr;
	uint32_t decorTextureW = 0, decorTextureH = 0;
};

constexpr double kDecorInterval = 1.0 / 30.0;

bool hasWords(const Content &c)
{
	const auto blank = [](const std::string &t) { return t.find_first_not_of(" 	

") == std::string::npos; };
	return !blank(c.text) || !blank(c.caption);
}

std::map<std::string, std::string> &nameKeys()
{
	static std::map<std::string, std::string> keys; // source id -> locale key
	return keys;
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
	if (s->texture || s->decorTexture) {
		obs_enter_graphics();
		if (s->texture)
			gs_texture_destroy(s->texture);
		if (s->decorTexture)
			gs_texture_destroy(s->decorTexture);
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
		// The looping picture: redrawn on its own while the words are up.
		const Content &now = s->showing;
		if (now.style.decor != int(Decor::None) && hasWords(now) && s->opacity > 0.0) {
			s->decorSeconds += std::max(0.0, double(seconds));
			s->decorSince += std::max(0.0, double(seconds));
			if (s->decorSince >= kDecorInterval || s->decorFrame.isNull() || s->opacity < 1.0) {
				s->decorSince = 0.0;
				const QRect box = decorRect(now.style, int(s->canvasW), int(s->canvasH));
				if (box.isEmpty()) {
					s->decorFrame = QImage();
				} else {
					s->decorBox = box;
					s->decorFrame = renderDecor(now.style, s->decorSeconds, box.size(), s->opacity)
								.convertToFormat(QImage::Format_RGBA8888);
					s->decorUpload = true;
				}
			}
		} else if (!s->decorFrame.isNull()) {
			s->decorFrame = QImage();
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
		gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, s->texture);
		gs_draw_sprite(s->texture, 0, w, h);
		if (!s->decorFrame.isNull() && !s->decorBox.isEmpty()) {
			const uint32_t dw = uint32_t(s->decorFrame.width()), dh = uint32_t(s->decorFrame.height());
			if (!s->decorTexture || s->decorTextureW != dw || s->decorTextureH != dh) {
				if (s->decorTexture)
					gs_texture_destroy(s->decorTexture);
				const uint8_t *bits = s->decorFrame.constBits();
				s->decorTexture = gs_texture_create(dw, dh, GS_RGBA, 1, &bits, GS_DYNAMIC);
				s->decorTextureW = dw;
				s->decorTextureH = dh;
				s->decorUpload = false;
			} else if (s->decorUpload) {
				gs_texture_set_image(s->decorTexture, s->decorFrame.constBits(),
						     uint32_t(s->decorFrame.bytesPerLine()), false);
				s->decorUpload = false;
			}
			if (s->decorTexture) {
				gs_matrix_push();
				gs_matrix_translate3f(float(s->decorBox.x()), float(s->decorBox.y()), 0.0f);
				gs_effect_set_texture(image, s->decorTexture);
				gs_draw_sprite(s->decorTexture, 0, dw, dh);
				gs_matrix_pop();
			}
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Slide source: could not draw");
	}
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
	info.get_width = getWidth;
	info.get_height = getHeight;
	info.video_tick = tick;
	info.video_render = render;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}

} // namespace laycue::slides
