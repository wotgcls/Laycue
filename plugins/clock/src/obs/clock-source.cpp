#include "obs/clock-source.hpp"

#include "core/clock-core.hpp"
#include "core/clock-settings.hpp"
#include "render/clock-renderer.hpp"

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>
#include <util/platform.h>

#include <QImage>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <mutex>
#include <string>

namespace laycue::clock {

namespace {

const ClockFonts *g_fonts = nullptr;

struct ClockSource {
	obs_source_t *source = nullptr;
	std::mutex mutex;

	ClockSettings settings;
	Timer timer;
	QString lastText; // what the picture shows now, to draw again only when it changes
	Level lastLevel = Level::Normal;
	int lastFraction = -1;
	bool dirty = true;

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

double steadySeconds()
{
	return double(os_gettime_ns()) / 1.0e9;
}

int nowSecondOfDay()
{
	const time_t t = time(nullptr);
	tm local = {};
#ifdef _WIN32
	localtime_s(&local, &t);
#else
	localtime_r(&t, &local);
#endif
	return local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
}

void canvasSize(uint32_t &w, uint32_t &h)
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi) && ovi.base_width > 0 && ovi.base_height > 0) {
		w = ovi.base_width;
		h = ovi.base_height;
	}
}

QColor colourOf(uint32_t rgb, int alphaPercent = 100)
{
	return QColor(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF),
		      int(std::lround(2.55 * alphaPercent)));
}

// OBS colours are 0xAABBGGRR; the settings keep 0xRRGGBB.
uint32_t fromObs(long long v)
{
	return (uint32_t((v) & 0xFF) << 16) | (uint32_t((v >> 8) & 0xFF) << 8) | uint32_t((v >> 16) & 0xFF);
}

// Sets the timer up from the settings, with the time to count for the mode. It starts again from the beginning.
void configureTimer(ClockSource &s, bool keepRunning)
{
	double seconds = 0.0;
	const bool wasRunning = s.timer.running();
	const double now = steadySeconds();
	s.timer.setWarning(s.settings.warnSeconds);
	s.timer.setOvertime(s.settings.overtime);
	const Mode mode = Mode(s.settings.mode);
	if (mode == Mode::CountUp) {
		s.timer.configure(Mode::CountUp, 0.0);
	} else if (mode == Mode::Until) {
		int target = 0;
		parseTimeOfDay(s.settings.target, target);
		s.timer.configure(Mode::Countdown, secondsUntilTimeOfDay(nowSecondOfDay(), target));
	} else {
		parseDuration(s.settings.duration, seconds);
		s.timer.configure(Mode::Countdown, seconds);
	}
	s.timer.reset();
	// A clock counting down to a time of day runs by itself; the others wait for Start unless they were running.
	if (mode == Mode::Until || (keepRunning && wasRunning))
		s.timer.start(now);
	s.dirty = true;
}

void update(void *data, obs_data_t *d)
{
	auto *s = static_cast<ClockSource *>(data);
	try {
		std::lock_guard<std::mutex> lock(s->mutex);
		const ClockSettings before = s->settings;
		ClockSettings n;
		n.mode = int(obs_data_get_int(d, key::mode));
		n.duration = str(obs_data_get_string(d, key::duration));
		n.target = str(obs_data_get_string(d, key::target));
		n.label = str(obs_data_get_string(d, key::label));
		n.design = int(obs_data_get_int(d, key::design));
		n.background = int(obs_data_get_int(d, key::background));
		n.warnSeconds = int(obs_data_get_int(d, key::warn));
		n.overtime = obs_data_get_bool(d, key::overtime);
		n.text = fromObs(obs_data_get_int(d, key::text));
		n.accent = fromObs(obs_data_get_int(d, key::accent));
		n.warning = fromObs(obs_data_get_int(d, key::warning));
		n.over = fromObs(obs_data_get_int(d, key::over));
		n.panel = fromObs(obs_data_get_int(d, key::panel));
		n.panelOpacity = int(obs_data_get_int(d, key::panelOpacity));
		n.align = int(obs_data_get_int(d, key::align));
		n.vertical = int(obs_data_get_int(d, key::vertical));
		n.sizePercent = int(obs_data_get_int(d, key::sizePercent));
		sanitize(n);
		s->settings = n;
		// Looks change at once. A change to what is being counted starts the count again.
		const bool counting = n.mode != before.mode || n.duration != before.duration ||
				      n.target != before.target || n.warnSeconds != before.warnSeconds ||
				      n.overtime != before.overtime;
		if (counting)
			configureTimer(*s, false);
		s->dirty = true;
	} catch (...) {
		obs_log(LOG_ERROR, "Clock source: could not apply settings");
	}
}

const char *getName(void *)
{
	return obs_module_text("Laycue.Clock.SourceName");
}

void getDefaults(obs_data_t *d)
{
	const ClockSettings def;
	const auto obsColour = [](uint32_t rgb) {
		return (long long)(0xFF000000u | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF));
	};
	obs_data_set_default_int(d, key::mode, def.mode);
	obs_data_set_default_string(d, key::duration, def.duration.c_str());
	obs_data_set_default_string(d, key::target, def.target.c_str());
	obs_data_set_default_string(d, key::label, def.label.c_str());
	obs_data_set_default_int(d, key::design, def.design);
	obs_data_set_default_int(d, key::background, def.background);
	obs_data_set_default_int(d, key::warn, def.warnSeconds);
	obs_data_set_default_bool(d, key::overtime, def.overtime);
	obs_data_set_default_int(d, key::text, obsColour(def.text));
	obs_data_set_default_int(d, key::accent, obsColour(def.accent));
	obs_data_set_default_int(d, key::warning, obsColour(def.warning));
	obs_data_set_default_int(d, key::over, obsColour(def.over));
	obs_data_set_default_int(d, key::panel, obsColour(def.panel));
	obs_data_set_default_int(d, key::panelOpacity, def.panelOpacity);
	obs_data_set_default_int(d, key::align, def.align);
	obs_data_set_default_int(d, key::vertical, def.vertical);
	obs_data_set_default_int(d, key::sizePercent, def.sizePercent);
}

void destroy(void *data)
{
	auto *s = static_cast<ClockSource *>(data);
	if (s->texture) {
		obs_enter_graphics();
		gs_texture_destroy(s->texture);
		obs_leave_graphics();
	}
	delete s;
}

uint32_t getWidth(void *data)
{
	return static_cast<ClockSource *>(data)->canvasW;
}

uint32_t getHeight(void *data)
{
	return static_cast<ClockSource *>(data)->canvasH;
}

void tick(void *data, float)
{
	auto *s = static_cast<ClockSource *>(data);
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
		const Reading reading = s->timer.read(steadySeconds());
		const bool countdown = s->timer.mode() == Mode::Countdown;
		std::string text = formatTime(reading.seconds, countdown && !reading.overtime);
		if (reading.overtime)
			text = "+" + text;
		const QString shown = QString::fromStdString(text);
		const int fraction = int(reading.fraction * 360.0); // the ring or bar moves in 360 small steps
		const bool ringLike = s->settings.design == int(Design::Ring) ||
				      s->settings.design == int(Design::ProgressBar);
		if (!s->dirty && shown == s->lastText && reading.level == s->lastLevel &&
		    (!ringLike || fraction == s->lastFraction))
			return;

		RenderRequest r;
		r.design = Design(s->settings.design);
		r.background = Background(s->settings.background);
		r.label = QString::fromStdString(s->settings.label);
		r.time = shown;
		r.countdown = countdown;
		r.level = reading.level;
		r.fraction = countdown ? reading.fraction : std::fmod(reading.seconds, 60.0) / 60.0;
		r.text = colourOf(s->settings.text);
		r.accent = colourOf(s->settings.accent);
		r.warning = colourOf(s->settings.warning);
		r.over = colourOf(s->settings.over);
		r.panel = colourOf(s->settings.panel, s->settings.panelOpacity);
		r.align = s->settings.align;
		r.vertical = s->settings.vertical;
		r.sizePercent = s->settings.sizePercent;
		r.canvasWidth = int(s->canvasW);
		r.canvasHeight = int(s->canvasH);
		Rendered rendered = renderClock(*g_fonts, r);
		if (rendered.image.isNull())
			return;
		s->image = rendered.image.convertToFormat(QImage::Format_RGBA8888); // OBS draws straight alpha
		s->imageX = rendered.x;
		s->imageY = rendered.y;
		s->uploadPending = true;
		s->lastText = shown;
		s->lastLevel = reading.level;
		s->lastFraction = fraction;
		s->dirty = false;
	} catch (...) {
		obs_log(LOG_ERROR, "Clock source: could not draw a frame");
	}
}

void render(void *data, gs_effect_t *effect)
{
	auto *s = static_cast<ClockSource *>(data);
	try {
		std::lock_guard<std::mutex> lock(s->mutex);
		if (s->image.isNull() || !effect)
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
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), s->texture);
		gs_matrix_push();
		gs_matrix_translate3f(float(s->imageX), float(s->imageY), 0.0f);
		gs_draw_sprite(s->texture, 0, w, h);
		gs_matrix_pop();
	} catch (...) {
		obs_log(LOG_ERROR, "Clock source: could not draw");
	}
}

// ---- Control: the dock, hotkeys and the Properties window all use these ----------------------------

void startClock(ClockSource &s)
{
	std::lock_guard<std::mutex> lock(s.mutex);
	if (s.timer.mode() == Mode::Countdown && s.settings.mode != int(Mode::Until)) {
		// A finished countdown starts again from the full time.
		const Reading r = s.timer.read(steadySeconds());
		if (r.finished && !s.timer.running())
			configureTimer(s, false);
	}
	s.timer.start(steadySeconds());
	s.dirty = true;
}

void pauseClock(ClockSource &s)
{
	std::lock_guard<std::mutex> lock(s.mutex);
	s.timer.pause(steadySeconds());
	s.dirty = true;
}

void toggleClock(ClockSource &s)
{
	bool running;
	{
		std::lock_guard<std::mutex> lock(s.mutex);
		running = s.timer.running();
	}
	if (running)
		pauseClock(s);
	else
		startClock(s);
}

void resetClock(ClockSource &s)
{
	std::lock_guard<std::mutex> lock(s.mutex);
	configureTimer(s, false);
}

void procStart(void *data, calldata_t *)
{
	startClock(*static_cast<ClockSource *>(data));
}

void procPause(void *data, calldata_t *)
{
	pauseClock(*static_cast<ClockSource *>(data));
}

void procToggle(void *data, calldata_t *)
{
	toggleClock(*static_cast<ClockSource *>(data));
}

void procReset(void *data, calldata_t *)
{
	resetClock(*static_cast<ClockSource *>(data));
}

void procAdjust(void *data, calldata_t *cd)
{
	auto *s = static_cast<ClockSource *>(data);
	std::lock_guard<std::mutex> lock(s->mutex);
	s->timer.adjust(double(calldata_int(cd, "seconds")), steadySeconds());
	s->dirty = true;
}

// running (0/1), the number on screen in seconds, level (0 normal, 1 warning, 2 over), finished (0/1), overtime (0/1)
void procState(void *data, calldata_t *cd)
{
	auto *s = static_cast<ClockSource *>(data);
	std::lock_guard<std::mutex> lock(s->mutex);
	const Reading r = s->timer.read(steadySeconds());
	calldata_set_int(cd, "running", s->timer.running() ? 1 : 0);
	calldata_set_float(cd, "seconds", r.seconds);
	calldata_set_int(cd, "level", int(r.level));
	calldata_set_int(cd, "finished", r.finished ? 1 : 0);
	calldata_set_int(cd, "overtime", r.overtime ? 1 : 0);
	calldata_set_int(cd, "countdown", s->timer.mode() == Mode::Countdown ? 1 : 0);
}

void toggleHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		toggleClock(*static_cast<ClockSource *>(data));
}

void resetHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		resetClock(*static_cast<ClockSource *>(data));
}

void *createWithControls(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new ClockSource();
	s->source = source;
	canvasSize(s->canvasW, s->canvasH);
	update(s, settings);
	{
		std::lock_guard<std::mutex> lock(s->mutex);
		configureTimer(*s, false); // an "until" clock starts by itself
	}
	proc_handler_t *ph = obs_source_get_proc_handler(source);
	proc_handler_add(ph, "void laycue_clock_start()", procStart, s);
	proc_handler_add(ph, "void laycue_clock_pause()", procPause, s);
	proc_handler_add(ph, "void laycue_clock_toggle()", procToggle, s);
	proc_handler_add(ph, "void laycue_clock_reset()", procReset, s);
	proc_handler_add(ph, "void laycue_clock_adjust(int seconds)", procAdjust, s);
	proc_handler_add(
		ph,
		"void laycue_clock_state(out int running, out float seconds, out int level, out int finished, out int overtime, out int countdown)",
		procState, s);
	obs_hotkey_register_source(source, "laycue_clock.toggle", obs_module_text("Laycue.Clock.Hotkey.Toggle"),
				   toggleHotkey, s);
	obs_hotkey_register_source(source, "laycue_clock.reset", obs_module_text("Laycue.Clock.Hotkey.Reset"),
				   resetHotkey, s);
	return s;
}

bool startClicked(obs_properties_t *, obs_property_t *, void *data)
{
	startClock(*static_cast<ClockSource *>(data));
	return false;
}

bool pauseClicked(obs_properties_t *, obs_property_t *, void *data)
{
	pauseClock(*static_cast<ClockSource *>(data));
	return false;
}

bool resetClicked(obs_properties_t *, obs_property_t *, void *data)
{
	resetClock(*static_cast<ClockSource *>(data));
	return false;
}

obs_properties_t *getProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *mode = obs_properties_add_list(props, key::mode, obs_module_text("Laycue.Clock.Mode"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, obs_module_text("Laycue.Clock.Mode.0"), 0);
	obs_property_list_add_int(mode, obs_module_text("Laycue.Clock.Mode.1"), 1);
	obs_property_list_add_int(mode, obs_module_text("Laycue.Clock.Mode.2"), 2);
	obs_properties_add_text(props, key::duration, obs_module_text("Laycue.Clock.Duration"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, key::target, obs_module_text("Laycue.Clock.Target"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(props, key::label, obs_module_text("Laycue.Clock.Label"), OBS_TEXT_DEFAULT);
	obs_property_t *design = obs_properties_add_list(props, key::design, obs_module_text("Laycue.Clock.Design"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	for (int i = 0; i < kDesignCount; ++i)
		obs_property_list_add_int(design, obs_module_text(designKey(Design(i))), i);
	obs_property_t *bg = obs_properties_add_list(props, key::background, obs_module_text("Laycue.Clock.Background"),
						     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(bg, obs_module_text("Laycue.Clock.Background.0"), 0);
	obs_property_list_add_int(bg, obs_module_text("Laycue.Clock.Background.1"), 1);
	obs_property_list_add_int(bg, obs_module_text("Laycue.Clock.Background.2"), 2);
	obs_properties_add_color(props, key::panel, obs_module_text("Laycue.Clock.PanelColour"));
	obs_properties_add_int_slider(props, key::panelOpacity, obs_module_text("Laycue.Clock.PanelOpacity"), 0, 100,
				      5);
	obs_properties_add_color(props, key::text, obs_module_text("Laycue.Clock.TextColour"));
	obs_properties_add_color(props, key::accent, obs_module_text("Laycue.Clock.AccentColour"));
	obs_properties_add_color(props, key::warning, obs_module_text("Laycue.Clock.WarningColour"));
	obs_properties_add_color(props, key::over, obs_module_text("Laycue.Clock.OverColour"));
	obs_properties_add_int(props, key::warn, obs_module_text("Laycue.Clock.WarnSeconds"), 0, 3600, 5);
	obs_properties_add_bool(props, key::overtime, obs_module_text("Laycue.Clock.Overtime"));
	obs_property_t *align = obs_properties_add_list(props, key::align, obs_module_text("Laycue.Clock.Align"),
							OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(align, obs_module_text("Laycue.Clock.Left"), 0);
	obs_property_list_add_int(align, obs_module_text("Laycue.Clock.Centre"), 1);
	obs_property_list_add_int(align, obs_module_text("Laycue.Clock.Right"), 2);
	obs_property_t *vertical = obs_properties_add_list(props, key::vertical,
							   obs_module_text("Laycue.Clock.Vertical"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(vertical, obs_module_text("Laycue.Clock.Top"), 0);
	obs_property_list_add_int(vertical, obs_module_text("Laycue.Clock.Middle"), 1);
	obs_property_list_add_int(vertical, obs_module_text("Laycue.Clock.Bottom"), 2);
	obs_properties_add_int_slider(props, key::sizePercent, obs_module_text("Laycue.Clock.Size"), kMinSizePercent,
				      kMaxSizePercent, 5);
	obs_properties_add_button2(props, "start", obs_module_text("Laycue.Clock.Start"), startClicked, data);
	obs_properties_add_button2(props, "pause", obs_module_text("Laycue.Clock.Pause"), pauseClicked, data);
	obs_properties_add_button2(props, "reset", obs_module_text("Laycue.Clock.Reset"), resetClicked, data);
	return props;
}

} // namespace

void registerClockSource(const ClockFonts *fonts)
{
	g_fonts = fonts;
	obs_source_info info = {};
	info.id = kClockSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	info.get_name = getName;
	info.create = createWithControls;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.get_width = getWidth;
	info.get_height = getHeight;
	info.video_tick = tick;
	info.video_render = render;
	obs_register_source(&info);
}

} // namespace laycue::clock
