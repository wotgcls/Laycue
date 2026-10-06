#include "obs/title-source.hpp"

#include "core/library.hpp"

#include "core/ticker.hpp"
#include "core/title-clock.hpp"
#include "core/title-templates.hpp"
#include "render/title-renderer.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>
#include <util/platform.h>

#include <QImage>

#include <algorithm>
#include <map>
#include <mutex>
#include <string>

namespace laycue::titles {

namespace {

const FontSet *g_fonts = nullptr;

struct TitleSource {
	obs_source_t *source = nullptr;
	std::mutex mutex;

	// What the settings say.
	TitleTemplate tpl;
	TitleText text;
	bool useAccent = false;
	Rgba accent;
	int align = 0;
	double scale = 1.0;
	int speed = kDefaultSpeed;
	int fit = 0;
	int fitWidth = kDefaultFitWidth;

	// Animation.
	TitleClock clock;
	TickerState ticker;
	bool dirty = true; // the picture must be drawn again

	// Cueing. A title that is "shown" while its scene is not on the program output yet (it is only in
	// Preview) is cued: it waits there, drawn fully on screen so the operator can check it, and plays its
	// arrival when the scene goes live. Without this the animation would run in Preview and be over by
	// the time of the cut.
	bool armed = false;   // the operator has shown it and has not hidden it since
	bool wasLive = false; // was the source on the program output at the last tick
	bool showing = false; // a picture is being drawn (live, or cued in Preview)

	// The drawn picture, and the GPU copy of it.
	QImage image;
	int imageX = 0;
	int imageY = 0;
	bool uploadPending = false;
	gs_texture_t *texture = nullptr;
	uint32_t textureW = 0;
	uint32_t textureH = 0;
	uint32_t canvasW = 1920;
	uint32_t canvasH = 1080;
};

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

Rgba fromObsColour(uint32_t value)
{
	// OBS colours are 0xAABBGGRR.
	return Rgba{uint8_t(value & 0xFF), uint8_t((value >> 8) & 0xFF), uint8_t((value >> 16) & 0xFF), 255};
}

uint32_t toObsColour(const Rgba &c)
{
	return 0xFF000000u | (uint32_t(c.b) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.r);
}

// Reads the custom template (an obs_data object of text values) from the settings.
Overrides readOverrides(obs_data_t *settings)
{
	std::map<std::string, std::string> values;
	OBSDataAutoRelease custom = obs_data_get_obj(settings, key::custom);
	if (custom) {
		for (obs_data_item_t *item = obs_data_first(custom); item; obs_data_item_next(&item)) {
			const char *name = obs_data_item_get_name(item);
			if (name && obs_data_item_gettype(item) == OBS_DATA_STRING)
				values[name] = str(obs_data_item_get_string(item));
		}
	}
	return fromMap(values);
}

// With the source's mutex held.
bool isLive(const TitleSource &s)
{
	return s.source && obs_source_active(s.source);
}

// "Show": play the arrival now if the title is on the program output, otherwise wait (cued) for it.
void cueLocked(TitleSource &s)
{
	s.armed = true;
	if (isLive(s)) {
		s.wasLive = true;
		s.clock.show();
	}
	s.dirty = true;
}

bool isCued(const TitleSource &s)
{
	return s.armed && !s.clock.visible() && !isLive(s);
}

void applyClockConfig(TitleSource &s)
{
	const double k = transitionScale(s.speed);
	s.clock.configure(s.tpl.inSeconds * k, s.tpl.outSeconds * k, s.tpl.holdSeconds);
	s.ticker.configure(g_fonts ? tickerStripWidth(*g_fonts, s.tpl, s.text) : 1.0, continuousSpeed(s.tpl));
}

const char *getName(void *)
{
	return obs_module_text("Laycue.Titles.SourceName");
}

void getDefaults(obs_data_t *settings)
{
	const TitleTemplate &first = builtInTemplates().front();
	obs_data_set_default_string(settings, key::templateId, first.id.c_str());
	const auto sample = [&](int field) {
		const std::string k = sampleKey(first, field);
		const char *text = obs_module_text(k.c_str());
		return text && k != text ? text : "";
	};
	obs_data_set_default_string(settings, key::line1, sample(1));
	obs_data_set_default_string(settings, key::line2, sample(2));
	obs_data_set_default_string(settings, key::tag, "");
	obs_data_set_default_bool(settings, key::accentOn, false);
	obs_data_set_default_int(settings, key::accent, toObsColour(first.palette.accent));
	obs_data_set_default_int(settings, key::hold, -1);
	obs_data_set_default_int(settings, key::speed, kDefaultSpeed);
	obs_data_set_default_int(settings, key::fit, 0);
	obs_data_set_default_int(settings, key::fitWidth, kDefaultFitWidth);
	obs_data_set_default_int(settings, key::align, 0);
	obs_data_set_default_int(settings, key::sizePercent, 100);
	obs_data_set_default_bool(settings, key::onAir, false);
}

void update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<TitleSource *>(data);
	try {
		std::lock_guard<std::mutex> lock(s->mutex);
		const TitleTemplate *base = findTemplate(str(obs_data_get_string(settings, key::templateId)));
		if (!base)
			base = &builtInTemplates().front();
		Overrides overrides = readOverrides(settings);
		overrides.baseId = base->id;
		s->tpl = overrides.baseId.empty() ? *base : resolve(*base, overrides);
		s->tpl.id = base->id; // custom templates draw like the template they came from

		s->text.line1 = QString::fromUtf8(obs_data_get_string(settings, key::line1));
		s->text.line2 = QString::fromUtf8(obs_data_get_string(settings, key::line2));
		s->text.tag = QString::fromUtf8(obs_data_get_string(settings, key::tag));
		s->text.note = QString::fromUtf8(obs_data_get_string(settings, key::note));
		s->text.logoPath = QString::fromUtf8(obs_data_get_string(settings, key::logo));
		s->text.rows.clear();
		{
			OBSDataArrayAutoRelease rows = obs_data_get_array(settings, key::rows);
			const size_t n = rows ? std::min<size_t>(obs_data_array_count(rows), 8) : 0;
			for (size_t i = 0; i < n; ++i) {
				OBSDataAutoRelease item = obs_data_array_item(rows, i);
				s->text.rows.append({QString::fromUtf8(obs_data_get_string(item, "label")),
						     QString::fromUtf8(obs_data_get_string(item, "value"))});
			}
		}
		if (s->text.rows.isEmpty()) {
			// Rows typed in the Properties window, one "label | value" per line.
			OBSDataArrayAutoRelease list = obs_data_get_array(settings, "rows_list");
			const size_t n = list ? std::min<size_t>(obs_data_array_count(list), 8) : 0;
			for (size_t i = 0; i < n; ++i) {
				OBSDataAutoRelease item = obs_data_array_item(list, i);
				const QString line = QString::fromUtf8(obs_data_get_string(item, "value"));
				const int bar = int(line.indexOf(QLatin1Char('|')));
				s->text.rows.append(
					bar < 0 ? qMakePair(QString(), line.trimmed())
						: qMakePair(line.left(bar).trimmed(), line.mid(bar + 1).trimmed()));
			}
		}
		s->useAccent = obs_data_get_bool(settings, key::accentOn);
		s->accent = fromObsColour(uint32_t(obs_data_get_int(settings, key::accent)));

		s->align = std::clamp(int(obs_data_get_int(settings, key::align)), 0, 2);
		s->scale = std::clamp(int(obs_data_get_int(settings, key::sizePercent)), kMinSizePercent,
				      kMaxSizePercent) /
			   100.0;
		s->fit = std::clamp(int(obs_data_get_int(settings, key::fit)), 0, 2);
		s->fitWidth = std::clamp(int(obs_data_get_int(settings, key::fitWidth)), kMinFitWidth, kMaxFitWidth);
		s->speed = std::clamp(int(obs_data_get_int(settings, key::speed)), 0, kSpeedSteps - 1);
		const int hold = int(obs_data_get_int(settings, key::hold));
		if (hold >= 0)
			s->tpl.holdSeconds = hold;
		applyClockConfig(*s);
		s->dirty = true;

		// "on_air" is a one-shot command (used by scenes saved with a title on air, and by simple
		// scripts): it is read once and cleared, so a later settings change never shows a title again.
		if (obs_data_get_bool(settings, key::onAir)) {
			cueLocked(*s);
			obs_data_set_bool(settings, key::onAir, false);
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Title source: could not apply settings");
	}
}

void save(void *, obs_data_t *settings)
{
	// A title is never saved "on air": a scene collection always opens with every title hidden.
	obs_data_set_bool(settings, key::onAir, false);
}

void *create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new TitleSource();
	s->source = source;
	canvasSize(s->canvasW, s->canvasH);
	update(s, settings);
	return s;
}

void destroy(void *data)
{
	auto *s = static_cast<TitleSource *>(data);
	if (s->texture) {
		obs_enter_graphics();
		gs_texture_destroy(s->texture);
		obs_leave_graphics();
	}
	delete s;
}

uint32_t getWidth(void *data)
{
	return static_cast<TitleSource *>(data)->canvasW;
}

uint32_t getHeight(void *data)
{
	return static_cast<TitleSource *>(data)->canvasH;
}

void tick(void *data, float seconds)
{
	auto *s = static_cast<TitleSource *>(data);
	try {
		std::lock_guard<std::mutex> lock(s->mutex);
		if (!g_fonts)
			return;
		uint32_t w = s->canvasW, h = s->canvasH;
		canvasSize(w, h);
		if (w != s->canvasW || h != s->canvasH) {
			s->canvasW = w;
			s->canvasH = h;
			s->dirty = true;
		}

		// Going live starts the arrival of a cued title; leaving the program output ends it without a show.
		const bool live = isLive(*s);
		if (live && !s->wasLive && s->armed) {
			s->clock.reset();
			s->clock.show();
			s->dirty = true;
		} else if (!live && s->wasLive && s->clock.visible()) {
			s->clock.reset();
			s->dirty = true;
		}
		s->wasLive = live;

		const bool wasShowing = s->showing;
		const bool wasPlaying = s->clock.visible();
		s->clock.advance(seconds);
		if (wasPlaying && !s->clock.visible())
			s->armed = false; // it finished leaving: it does not come back by itself
		const bool still = s->armed && !live && !s->clock.visible(); // cued: drawn fully on, no animation
		const bool visible = s->clock.visible() || still;
		const bool ticking = isContinuous(s->tpl) && visible;
		if (ticking)
			s->ticker.advance(seconds);

		s->showing = visible;
		if (!visible) {
			if (wasShowing) { // it just finished leaving: drop the picture, free nothing else
				s->image = QImage();
				s->uploadPending = false;
			}
			s->dirty = false;
			return;
		}
		if (!(s->dirty || s->clock.animating() || ticking || !wasShowing))
			return;

		RenderRequest request;
		request.tpl = s->tpl;
		request.text = s->text;
		request.frame.in = still ? 1.0 : s->clock.in();
		request.frame.out = still ? 0.0 : s->clock.out();
		request.frame.tickerPx = s->ticker.offset();
		request.canvasWidth = int(s->canvasW);
		request.canvasHeight = int(s->canvasH);
		request.useAccentOverride = s->useAccent;
		request.accentOverride = s->accent;
		request.align = s->align;
		request.scale = s->scale;
		request.fit = s->fit;
		request.fitWidth = s->fitWidth;
		RenderedTitle rendered = renderTitle(*g_fonts, request);
		if (rendered.image.isNull())
			return;
		// OBS draws with straight (not premultiplied) alpha.
		s->image = rendered.image.convertToFormat(QImage::Format_RGBA8888);
		s->imageX = rendered.x;
		s->imageY = rendered.y;
		s->uploadPending = true;
		s->dirty = false;
	} catch (...) {
		obs_log(LOG_ERROR, "Title source: could not draw a frame");
	}
}

void render(void *data, gs_effect_t *effect)
{
	auto *s = static_cast<TitleSource *>(data);
	try {
		std::lock_guard<std::mutex> lock(s->mutex);
		if (!s->showing || s->image.isNull())
			return;
		const uint32_t w = uint32_t(s->image.width());
		const uint32_t h = uint32_t(s->image.height());
		if (!s->texture || s->textureW != w || s->textureH != h) {
			if (s->texture)
				gs_texture_destroy(s->texture);
			const uint8_t *bits = s->image.constBits();
			s->texture = gs_texture_create(w, h, GS_RGBA, 1, &bits, GS_DYNAMIC);
			s->textureW = w;
			s->textureH = h;
			s->uploadPending = false;
		} else if (s->uploadPending) {
			gs_texture_set_image(s->texture, s->image.constBits(), uint32_t(s->image.bytesPerLine()),
					     false);
			s->uploadPending = false;
		}
		if (!s->texture)
			return;

		// OBS has already started the default effect for us (this is not a custom-draw source).
		if (!effect)
			return;
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), s->texture);
		gs_matrix_push();
		gs_matrix_translate3f(float(s->imageX), float(s->imageY), 0.0f);
		gs_draw_sprite(s->texture, 0, w, h);
		gs_matrix_pop();
	} catch (...) {
		obs_log(LOG_ERROR, "Title source: could not draw");
	}
}

// ---- Control: the panel, hotkeys and the Properties window all go through these ---------------------

void showTitle(TitleSource &s)
{
	std::lock_guard<std::mutex> lock(s.mutex);
	cueLocked(s);
}

void hideTitle(TitleSource &s)
{
	std::lock_guard<std::mutex> lock(s.mutex);
	s.armed = false;
	s.clock.hide();
	s.dirty = true;
}

void procShow(void *data, calldata_t *)
{
	showTitle(*static_cast<TitleSource *>(data));
}

void procHide(void *data, calldata_t *)
{
	hideTitle(*static_cast<TitleSource *>(data));
}

// Returns the phase (0 hidden, 1 arriving, 2 on air, 3 leaving) and the seconds left before it hides
// itself (-1 = never), so the panel can show what is on air.
void procState(void *data, calldata_t *cd)
{
	auto *s = static_cast<TitleSource *>(data);
	std::lock_guard<std::mutex> lock(s->mutex);
	// 0 hidden, 1 arriving, 2 on air, 3 leaving, 4 cued (waiting in Preview for its scene to go live)
	calldata_set_int(cd, "phase", isCued(*s) ? 4 : static_cast<long long>(s->clock.phase()));
	calldata_set_float(cd, "hold_remaining", s->clock.holdRemaining());
}

void toggleHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *s = static_cast<TitleSource *>(data);
	bool visible;
	{
		std::lock_guard<std::mutex> lock(s->mutex);
		visible = (s->clock.visible() && s->clock.phase() != TitleClock::Phase::Out) || isCued(*s);
	}
	if (visible)
		hideTitle(*s);
	else
		showTitle(*s);
}

void showHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		showTitle(*static_cast<TitleSource *>(data));
}

void hideHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		hideTitle(*static_cast<TitleSource *>(data));
}

void *createWithControls(obs_data_t *settings, obs_source_t *source)
{
	auto *s = static_cast<TitleSource *>(create(settings, source));
	proc_handler_t *ph = obs_source_get_proc_handler(source);
	proc_handler_add(ph, "void laycue_title_show()", procShow, s);
	proc_handler_add(ph, "void laycue_title_hide()", procHide, s);
	proc_handler_add(ph, "void laycue_title_state(out int phase, out float hold_remaining)", procState, s);
	obs_hotkey_register_source(source, "laycue_title.toggle", obs_module_text("Laycue.Titles.Hotkey.Toggle"),
				   toggleHotkey, s);
	obs_hotkey_register_source(source, "laycue_title.show", obs_module_text("Laycue.Titles.Hotkey.Show"),
				   showHotkey, s);
	obs_hotkey_register_source(source, "laycue_title.hide", obs_module_text("Laycue.Titles.Hotkey.Hide"),
				   hideHotkey, s);
	return s;
}

// ---- The Properties window ----------------------------------------------------------------------------

bool templateChanged(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const TitleTemplate *t = findTemplate(str(obs_data_get_string(settings, key::templateId)));
	if (!t)
		return false;
	obs_property_set_visible(obs_properties_get(props, key::line1), t->usesLine1);
	obs_property_set_visible(obs_properties_get(props, key::line2), t->usesLine2);
	obs_property_set_visible(obs_properties_get(props, key::tag), t->usesTag);
	obs_property_set_visible(obs_properties_get(props, key::note), t->usesNote);
	obs_property_set_visible(obs_properties_get(props, key::logo), t->usesLogo);
	obs_property_set_visible(obs_properties_get(props, "rows_list"), t->maxRows > 0);
	obs_property_set_visible(obs_properties_get(props, key::align), supportsPlacement(*t));
	obs_property_set_visible(obs_properties_get(props, key::sizePercent), supportsPlacement(*t));
	const int fitMode = int(obs_data_get_int(settings, key::fit));
	obs_property_set_visible(obs_properties_get(props, key::fit), supportsFit(*t));
	obs_property_set_visible(obs_properties_get(props, key::fitWidth), supportsFit(*t) && fitMode == 2);
	// Say what the fields mean for this template: "Verse" and "Reference", "Name" and "Role"...
	const auto label = [&](const char *name, int field) {
		const std::string k = fieldKey(*t, field);
		const char *text = obs_module_text(k.c_str());
		if (text && *text && text != k)
			obs_property_set_description(obs_properties_get(props, name), text);
	};
	label(key::line1, 1);
	label(key::line2, 2);
	label(key::tag, 3);
	label(key::note, 4);
	return true;
}

bool showClicked(obs_properties_t *, obs_property_t *, void *data)
{
	if (data)
		showTitle(*static_cast<TitleSource *>(data));
	return false;
}

bool hideClicked(obs_properties_t *, obs_property_t *, void *data)
{
	if (data)
		hideTitle(*static_cast<TitleSource *>(data));
	return false;
}

obs_properties_t *getProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *list = obs_properties_add_list(props, key::templateId,
						       obs_module_text("Laycue.Titles.Template"), OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_STRING);
	// Grouped by category: a greyed heading, then that category's templates.
	for (Category cat : {Category::People, Category::Worship, Category::Announce, Category::Notes, Category::News,
			     Category::Live, Category::Social, Category::Ticker}) {
		bool first = true;
		for (const TitleTemplate &t : builtInTemplates()) {
			if (t.category != cat)
				continue;
			if (first) {
				const std::string heading =
					std::string("\u2014 ") + obs_module_text(categoryKey(cat)) + " \u2014";
				const size_t at = obs_property_list_add_string(list, heading.c_str(), "");
				obs_property_list_item_disable(list, at, true);
				first = false;
			}
			obs_property_list_add_string(list, obs_module_text(nameKey(t).c_str()), t.id.c_str());
		}
	}
	obs_property_set_modified_callback(list, templateChanged);

	obs_properties_add_text(props, key::line1, obs_module_text("Laycue.Titles.Line1"), OBS_TEXT_MULTILINE);
	obs_properties_add_text(props, key::line2, obs_module_text("Laycue.Titles.Line2"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, key::tag, obs_module_text("Laycue.Titles.Tag"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, key::note, obs_module_text("Laycue.Titles.Note"), OBS_TEXT_DEFAULT);
	obs_properties_add_path(props, key::logo, obs_module_text("Laycue.Titles.Logo"), OBS_PATH_FILE,
				"Pictures (*.png *.jpg *.jpeg *.webp *.bmp)", nullptr);
	obs_properties_add_editable_list(props, "rows_list", obs_module_text("Laycue.Titles.RowsList"),
					 OBS_EDITABLE_LIST_TYPE_STRINGS, nullptr, nullptr);
	obs_property_t *alignList = obs_properties_add_list(props, key::align,
							    obs_module_text("Laycue.Titles.Position"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(alignList, obs_module_text("Laycue.Titles.AlignLeft"), 0);
	obs_property_list_add_int(alignList, obs_module_text("Laycue.Titles.AlignCenter"), 1);
	obs_property_list_add_int(alignList, obs_module_text("Laycue.Titles.AlignRight"), 2);
	obs_property_t *fitList = obs_properties_add_list(props, key::fit, obs_module_text("Laycue.Titles.ShapeWidth"),
							  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(fitList, obs_module_text("Laycue.Titles.FitDefault"), 0);
	obs_property_list_add_int(fitList, obs_module_text("Laycue.Titles.FitText"), 1);
	obs_property_list_add_int(fitList, obs_module_text("Laycue.Titles.FitManual"), 2);
	obs_property_set_modified_callback(fitList, templateChanged); // shows the width slider only for Manual
	obs_properties_add_int_slider(props, key::fitWidth, obs_module_text("Laycue.Titles.ShapeWidthManual"),
				      kMinFitWidth, kMaxFitWidth, 50);
	obs_properties_add_int_slider(props, key::sizePercent, obs_module_text("Laycue.Titles.SizePercent"),
				      kMinSizePercent, kMaxSizePercent, 5);

	obs_property_t *accentOn =
		obs_properties_add_bool(props, key::accentOn, obs_module_text("Laycue.Titles.UseBrandColour"));
	(void)accentOn;
	obs_properties_add_color(props, key::accent, obs_module_text("Laycue.Titles.BrandColour"));
	obs_property_t *speedList = obs_properties_add_list(props, key::speed, obs_module_text("Laycue.Titles.Speed"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < kSpeedSteps; ++i) {
		const std::string k = "Laycue.Titles.Speed" + std::to_string(i);
		obs_property_list_add_int(speedList, obs_module_text(k.c_str()), i);
	}
	obs_properties_add_int(props, key::hold, obs_module_text("Laycue.Titles.Hold"), -1, 300, 1);

	obs_properties_add_button2(props, "show", obs_module_text("Laycue.Titles.Show"), showClicked, data);
	obs_properties_add_button2(props, "hide", obs_module_text("Laycue.Titles.Hide"), hideClicked, data);
	return props;
}

} // namespace

void registerTitleSource(const FontSet *fonts)
{
	g_fonts = fonts;
	obs_source_info info = {};
	info.id = kTitleSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	info.get_name = getName;
	info.create = createWithControls;
	info.destroy = destroy;
	info.update = update;
	info.save = save;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.get_width = getWidth;
	info.get_height = getHeight;
	info.video_tick = tick;
	info.video_render = render;
	obs_register_source(&info);
}

} // namespace laycue::titles
