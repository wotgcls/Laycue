#include "obs/brand-transitions.hpp"

#include <obs-module.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace laycue {

namespace {

enum class Kind { Push, Wipe, Zoom, Doors, Sting, Card, Stripes, Move, Morph };

struct Info {
	const char *id;
	const char *nameKey;
	Kind kind;
};

const Info kTransitions[] = {
	{"laycue_push_transition", "Laycue.Transition.Push", Kind::Push},
	{"laycue_wipe_transition", "Laycue.Transition.Wipe", Kind::Wipe},
	{"laycue_zoom_transition", "Laycue.Transition.Zoom", Kind::Zoom},
	{"laycue_doors_transition", "Laycue.Transition.Doors", Kind::Doors},
	{"laycue_sting_transition", "Laycue.Transition.Sting", Kind::Sting},
	{"laycue_card_transition", "Laycue.Transition.Card", Kind::Card},
	{"laycue_stripes_transition", "Laycue.Transition.Stripes", Kind::Stripes},
	{"laycue_move_transition", "Laycue.Transition.Move", Kind::Move},
	{"laycue_morph_transition", "Laycue.Transition.Morph", Kind::Morph},
};

constexpr const char *kColour1 = "colour1";
constexpr const char *kColour2 = "colour2";
constexpr const char *kLogo = "logo";

struct Transition {
	obs_source_t *source = nullptr;
	Kind kind = Kind::Push;
	uint32_t colour1 = 0xFF8A2BE2; // OBS colours are 0xAABBGGRR
	uint32_t colour2 = 0xFF1A0A2E;
	std::string logoPath;
	gs_image_file_t logo = {};
	bool logoLoaded = false;
	std::vector<gs_texrender_t *> renders; // Move: one picture per moving source, kept between frames
};

// A shader that draws a picture at an opacity (OBS's own default one has no opacity). Made once, on first use.
gs_effect_t *opacityEffect()
{
	static gs_effect_t *effect = nullptr;
	static bool tried = false;
	if (!tried) {
		tried = true;
		char *path = obs_module_file("effects/laycue-opacity.effect");
		if (path) {
			effect = gs_effect_create_from_file(path, nullptr);
			bfree(path);
		}
	}
	return effect;
}

// Smooth start and end, the broadcast feel.
float ease(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float stage(float t, float from, float to)
{
	return std::clamp((t - from) / std::max(0.0001f, to - from), 0.0f, 1.0f);
}

// ---- Drawing (graphics thread) -------------------------------------------------------------------------------

// The effect to draw with: OBS's default, or Laycue's when the picture is see-through.
gs_effect_t *effectFor(float opacity)
{
	gs_effect_t *fade = opacity < 0.999f ? opacityEffect() : nullptr;
	if (!fade)
		return obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_float(gs_effect_get_param_by_name(fade, "opacity"), std::clamp(opacity, 0.0f, 1.0f));
	return fade;
}

void drawTexture(gs_texture_t *tex, float x, float y, float sx, float sy, uint32_t cx, uint32_t cy,
		 float opacity = 1.0f)
{
	if (!tex || opacity <= 0.0f)
		return;
	gs_effect_t *effect = effectFor(opacity);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(sx, sy, 1.0f);
	gs_effect_set_texture_srgb(image, tex);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(tex, 0, cx, cy);
	gs_matrix_pop();
}

// Part of a texture (x0, y0, w, h of it) stretched over (x, y, dw, dh) of the canvas.
void drawPartScaled(gs_texture_t *tex, float x0, float y0, float w, float h, float x, float y, float dw, float dh)
{
	if (!tex || w <= 0.5f || h <= 0.5f || dw <= 0.5f || dh <= 0.5f)
		return;
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(dw / w, dh / h, 1.0f);
	gs_effect_set_texture_srgb(image, tex);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite_subregion(tex, 0, uint32_t(std::max(0.0f, x0)), uint32_t(std::max(0.0f, y0)),
					 uint32_t(w), uint32_t(h));
	gs_matrix_pop();
}

// ---- Move: sources that are in both shots glide from where they were to where they will be ------------------

struct Box {
	float x = 0, y = 0, w = 0, h = 0;
};

struct Piece {
	obs_source_t *source = nullptr; // not owned: the scenes hold it for as long as the transition runs
	Box box;                        // where it sits on the canvas
	obs_sceneitem_crop crop = {};
};

struct Collect {
	std::vector<Piece> *out;
	Box parent;
	float canvasW, canvasH;
	int depth;
};

void collectPieces(obs_scene_t *scene, const Box &parent, float canvasW, float canvasH, int depth,
		   std::vector<Piece> &out);

bool collectOne(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *c = static_cast<Collect *>(param);
	if (!obs_sceneitem_visible(item) || c->out->size() >= 24)
		return true;
	obs_source_t *source = obs_sceneitem_get_source(item);
	struct matrix4 m;
	obs_sceneitem_get_box_transform(item, &m);
	// The box transform maps a unit square onto the item's box; only position and size are used (no rotation).
	Box box{m.t.x, m.t.y, m.x.x, m.y.y};
	if (box.w < 0) {
		box.x += box.w;
		box.w = -box.w;
	}
	if (box.h < 0) {
		box.y += box.h;
		box.h = -box.h;
	}
	// Into the parent's place (a scene inside a scene is scaled into its item's box).
	box.x = c->parent.x + box.x * c->parent.w / c->canvasW;
	box.y = c->parent.y + box.y * c->parent.h / c->canvasH;
	box.w *= c->parent.w / c->canvasW;
	box.h *= c->parent.h / c->canvasH;
	obs_scene_t *inner = obs_scene_from_source(source);
	if (!inner)
		inner = obs_group_from_source(source);
	if (inner && c->depth < 1) {
		collectPieces(inner, box, c->canvasW, c->canvasH, c->depth + 1, *c->out);
		return true;
	}
	Piece piece;
	piece.source = source;
	piece.box = box;
	obs_sceneitem_get_crop(item, &piece.crop);
	c->out->push_back(piece);
	return true;
}

void collectPieces(obs_scene_t *scene, const Box &parent, float canvasW, float canvasH, int depth,
		   std::vector<Piece> &out)
{
	Collect c{&out, parent, canvasW, canvasH, depth};
	obs_scene_enum_items(scene, collectOne, &c);
}

std::vector<Piece> piecesOf(obs_source_t *transition, enum obs_transition_target target, float W, float H)
{
	std::vector<Piece> pieces;
	obs_source_t *source = obs_transition_get_source(transition, target);
	if (!source)
		return pieces;
	obs_scene_t *scene = obs_scene_from_source(source);
	if (scene)
		collectPieces(scene, Box{0, 0, W, H}, W, H, 0, pieces);
	obs_source_release(source);
	return pieces;
}

float mix(float a, float b, float t)
{
	return a + (b - a) * t;
}

// Draws the sources found in both shots at a place between the two.
void drawMoving(Transition *t, float e, float W, float H)
{
	const std::vector<Piece> from = piecesOf(t->source, OBS_TRANSITION_SOURCE_A, W, H);
	const std::vector<Piece> to = piecesOf(t->source, OBS_TRANSITION_SOURCE_B, W, H);
	size_t used = 0;
	for (const Piece &a : from) {
		const Piece *b = nullptr;
		for (const Piece &candidate : to) {
			if (candidate.source == a.source)
				b = &candidate;
		}
		if (!b)
			continue;
		const uint32_t sw = obs_source_get_width(a.source), sh = obs_source_get_height(a.source);
		if (sw == 0 || sh == 0)
			continue;
		if (used >= t->renders.size())
			t->renders.push_back(gs_texrender_create(GS_RGBA, GS_ZS_NONE));
		gs_texrender_t *render = t->renders[used++];
		gs_texrender_reset(render);
		if (!gs_texrender_begin(render, sw, sh))
			continue;
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, float(sw), 0.0f, float(sh), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		obs_source_video_render(a.source);
		gs_blend_state_pop();
		gs_texrender_end(render);
		gs_texture_t *tex = gs_texrender_get_texture(render);
		const float cl = mix(float(a.crop.left), float(b->crop.left), e);
		const float ct = mix(float(a.crop.top), float(b->crop.top), e);
		const float cr = mix(float(a.crop.right), float(b->crop.right), e);
		const float cb = mix(float(a.crop.bottom), float(b->crop.bottom), e);
		drawPartScaled(tex, cl, ct, float(sw) - cl - cr, float(sh) - ct - cb, mix(a.box.x, b->box.x, e),
			       mix(a.box.y, b->box.y, e), mix(a.box.w, b->box.w, e), mix(a.box.h, b->box.h, e));
	}
}

// Part of a texture (x0..x0+w, y0..y0+h of it), drawn at (x, y).
void drawPart(gs_texture_t *tex, float x0, float y0, float w, float h, float x, float y)
{
	if (!tex || w <= 0.5f || h <= 0.5f)
		return;
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_effect_set_texture_srgb(image, tex);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite_subregion(tex, 0, uint32_t(std::max(0.0f, x0)), uint32_t(std::max(0.0f, y0)),
					 uint32_t(w), uint32_t(h));
	gs_matrix_pop();
}

// A filled rectangle in an OBS colour, with `alpha` (0..1) on top of the colour's own.
void drawRect(float x, float y, float w, float h, uint32_t colour, float alpha = 1.0f)
{
	if (w <= 0.0f || h <= 0.0f || alpha <= 0.0f)
		return;
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *param = gs_effect_get_param_by_name(solid, "color");
	struct vec4 c;
	vec4_from_rgba_srgb(&c, colour);
	c.w *= alpha;
	gs_effect_set_vec4(param, &c);
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, uint32_t(std::ceil(w)), uint32_t(std::ceil(h)));
	gs_matrix_pop();
}

// The logo (if one is chosen) centred on (cx, cy), fitted into a box `size` high, grown by `scale`.
void drawLogo(Transition *t, float cx, float cy, float size, float scale)
{
	if (!t->logoLoaded || !t->logo.texture || t->logo.cx == 0 || t->logo.cy == 0 || scale <= 0.01f)
		return;
	const float fit = size / float(std::max(t->logo.cx, t->logo.cy)) * scale;
	const float w = float(t->logo.cx) * fit, h = float(t->logo.cy) * fit;
	drawTexture(t->logo.texture, cx - w / 2.0f, cy - h / 2.0f, fit, fit, t->logo.cx, t->logo.cy);
}

void renderFrame(void *data, gs_texture_t *a, gs_texture_t *b, float progress, uint32_t cx, uint32_t cy)
{
	auto *t = static_cast<Transition *>(data);
	const float W = float(cx), H = float(cy);
	const float e = ease(progress);
	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);
	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	switch (t->kind) {
	case Kind::Push: {
		// The new shot pushes the old one off to the left, a band of brand colour between them.
		const float band = W * 0.05f;
		const float shift = (W + band) * e;
		drawTexture(a, -shift, 0, 1, 1, cx, cy);
		drawRect(W - shift, 0, band, H, t->colour1);
		drawTexture(b, W + band - shift, 0, 1, 1, cx, cy);
		break;
	}
	case Kind::Wipe: {
		// A wide brand bar sweeps across with the logo on it; the new shot is behind it.
		const float bar = W * 0.32f;
		const float lead = -bar + (W + bar * 2.0f) * e; // the bar's leading (right) edge
		const float edge = std::clamp(lead - bar, 0.0f, W);
		drawTexture(a, 0, 0, 1, 1, cx, cy);
		drawPart(b, 0, 0, edge, H, 0, 0);
		drawRect(lead - bar, 0, bar, H, t->colour1);
		drawRect(lead - bar - W * 0.02f, 0, W * 0.02f, H, t->colour2);
		drawLogo(t, lead - bar / 2.0f, H / 2.0f, H * 0.22f, 1.0f);
		break;
	}
	case Kind::Zoom: {
		// The old shot rushes toward you into a wash of brand colour; the new one settles out of it.
		const float wash = progress < 0.5f ? ease(stage(progress, 0.15f, 0.5f))
						   : 1.0f - ease(stage(progress, 0.5f, 0.85f));
		if (progress < 0.5f) {
			const float s = 1.0f + 0.6f * ease(progress * 2.0f);
			drawTexture(a, W / 2.0f * (1.0f - s), H / 2.0f * (1.0f - s), s, s, cx, cy);
		} else {
			const float s = 0.82f + 0.18f * ease((progress - 0.5f) * 2.0f);
			drawRect(0, 0, W, H, t->colour2);
			drawTexture(b, W / 2.0f * (1.0f - s), H / 2.0f * (1.0f - s), s, s, cx, cy);
		}
		drawRect(0, 0, W, H, t->colour1, wash);
		drawLogo(t, W / 2.0f, H / 2.0f, H * 0.24f, wash);
		break;
	}
	case Kind::Doors: {
		// The old shot splits down the middle and slides apart, brand edges on the doors.
		const float open = (W / 2.0f) * e;
		const float strip = W * 0.012f;
		drawTexture(b, 0, 0, 1, 1, cx, cy);
		drawPart(a, 0, 0, W / 2.0f, H, -open, 0);
		drawPart(a, W / 2.0f, 0, W / 2.0f, H, W / 2.0f + open, 0);
		if (progress > 0.0f && progress < 1.0f) {
			drawRect(W / 2.0f - open - strip, 0, strip, H, t->colour1);
			drawRect(W / 2.0f + open, 0, strip, H, t->colour1);
		}
		break;
	}
	case Kind::Sting: {
		// A brand panel rises over the picture, the logo pops on it, then it lifts away to show the new shot.
		const float rise = ease(stage(progress, 0.0f, 0.35f));
		const float lift = ease(stage(progress, 0.65f, 1.0f));
		drawTexture(progress < 0.5f ? a : b, 0, 0, 1, 1, cx, cy);
		const float top = H * (1.0f - rise) - H * lift;
		drawRect(0, top, W, H, t->colour1);
		drawRect(0, top - H * 0.03f, W, H * 0.03f, t->colour2);
		const float pop = stage(progress, 0.3f, 0.5f);
		const float settle = ease(stage(progress, 0.5f, 0.6f));
		const float scale = pop < 1.0f ? 0.6f + 0.55f * ease(pop) : 1.15f - 0.15f * settle;
		drawLogo(t, W / 2.0f, top + H / 2.0f, H * 0.3f, pop > 0.0f ? scale : 0.0f);
		break;
	}
	case Kind::Card: {
		// The old shot steps back and dims; the new one slides up over it like a card.
		const float s = 1.0f - 0.08f * e;
		drawTexture(a, W / 2.0f * (1.0f - s), H / 2.0f * (1.0f - s), s, s, cx, cy);
		drawRect(0, 0, W, H, 0xFF000000, 0.55f * e);
		const float y = H * (1.0f - e);
		drawRect(0, y - H * 0.012f, W, H * 0.012f, t->colour1);
		drawTexture(b, 0, y, 1, 1, cx, cy);
		break;
	}
	case Kind::Move: {
		// Everything that is in both shots glides to its new place; the rest cross-fades underneath.
		drawTexture(a, 0, 0, 1, 1, cx, cy);
		drawTexture(b, 0, 0, 1, 1, cx, cy, e);
		if (progress > 0.0f && progress < 1.0f)
			drawMoving(t, e, W, H);
		break;
	}
	case Kind::Morph: {
		// The old shot swells and melts into the new one, which settles in from slightly smaller.
		const float sa = 1.0f + 0.12f * e, sb = 0.9f + 0.1f * e;
		drawTexture(a, W / 2.0f * (1.0f - sa), H / 2.0f * (1.0f - sa), sa, sa, cx, cy);
		drawTexture(b, W / 2.0f * (1.0f - sb), H / 2.0f * (1.0f - sb), sb, sb, cx, cy, e);
		const float glow = std::sin(float(progress) * 3.14159265f);
		drawRect(0, 0, W, H, t->colour1, 0.18f * glow);
		break;
	}
	case Kind::Stripes: {
		// Bands of brand colour slide in one after another to cover the picture, then slide on out.
		const int count = 6;
		const float bandH = H / float(count);
		drawTexture(progress < 0.5f ? a : b, 0, 0, 1, 1, cx, cy);
		float covered = 1.0f;
		for (int i = 0; i < count; ++i) {
			const float delay = 0.05f * float(i);
			const float in = ease(stage(progress, delay, 0.42f + delay * 0.2f));
			const float out = ease(stage(progress, 0.55f + delay, 0.95f + delay * 0.1f));
			const float x = out > 0.0f ? W * out : -W + W * in;
			drawRect(x, bandH * float(i), W, bandH + 1.0f, i % 2 ? t->colour2 : t->colour1);
			covered = std::min(covered, in - out);
		}
		drawLogo(t, W / 2.0f, H / 2.0f, H * 0.26f, std::max(0.0f, covered));
		break;
	}
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous);
}

// ---- The OBS source -----------------------------------------------------------------------------------------

float mixA(void *, float t)
{
	return 1.0f - t;
}

float mixB(void *, float t)
{
	return t;
}

void loadLogo(Transition *t, const std::string &path)
{
	if (path == t->logoPath && (t->logoLoaded || path.empty()))
		return;
	obs_enter_graphics();
	if (t->logoLoaded)
		gs_image_file_free(&t->logo);
	obs_leave_graphics();
	t->logoLoaded = false;
	t->logoPath = path;
	if (path.empty())
		return;
	gs_image_file_init(&t->logo, path.c_str());
	obs_enter_graphics();
	gs_image_file_init_texture(&t->logo);
	obs_leave_graphics();
	t->logoLoaded = t->logo.loaded;
}

void update(void *data, obs_data_t *settings)
{
	auto *t = static_cast<Transition *>(data);
	t->colour1 = uint32_t(obs_data_get_int(settings, kColour1)) | 0xFF000000;
	t->colour2 = uint32_t(obs_data_get_int(settings, kColour2)) | 0xFF000000;
	const char *logo = obs_data_get_string(settings, kLogo);
	loadLogo(t, logo ? logo : "");
}

void *create(obs_data_t *settings, obs_source_t *source, Kind kind)
{
	auto *t = new Transition();
	t->source = source;
	t->kind = kind;
	update(t, settings);
	return t;
}

void destroy(void *data)
{
	auto *t = static_cast<Transition *>(data);
	obs_enter_graphics();
	if (t->logoLoaded)
		gs_image_file_free(&t->logo);
	for (gs_texrender_t *render : t->renders)
		gs_texrender_destroy(render);
	obs_leave_graphics();
	delete t;
}

void tick(void *data, float seconds)
{
	auto *t = static_cast<Transition *>(data);
	if (!t->logoLoaded || !t->logo.is_animated_gif)
		return;
	if (gs_image_file_tick(&t->logo, uint64_t(double(seconds) * 1000000000.0))) {
		obs_enter_graphics();
		gs_image_file_update_texture(&t->logo);
		obs_leave_graphics();
	}
}

void render(void *data, gs_effect_t *)
{
	auto *t = static_cast<Transition *>(data);
	obs_transition_video_render(t->source, renderFrame);
}

bool audioRender(void *data, uint64_t *tsOut, struct obs_source_audio_mix *audio, uint32_t mixers, size_t channels,
		 size_t sampleRate)
{
	auto *t = static_cast<Transition *>(data);
	return obs_transition_audio_render(t->source, tsOut, audio, mixers, channels, sampleRate, mixA, mixB);
}

void defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, kColour1, 0xFFE22B8A); // violet
	obs_data_set_default_int(settings, kColour2, 0xFF2E0A1A); // deep plum
	obs_data_set_default_string(settings, kLogo, "");
}

obs_properties_t *properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_color(props, kColour1, obs_module_text("Laycue.Transition.Colour1"));
	obs_properties_add_color(props, kColour2, obs_module_text("Laycue.Transition.Colour2"));
	obs_properties_add_path(props, kLogo, obs_module_text("Laycue.Transition.Logo"), OBS_PATH_FILE,
				"Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)", nullptr);
	return props;
}

template<int I> void *createKind(obs_data_t *settings, obs_source_t *source)
{
	return create(settings, source, kTransitions[I].kind);
}

template<int I> const char *nameOf(void *)
{
	return obs_module_text(kTransitions[I].nameKey);
}

template<int I> void registerOne()
{
	obs_source_info info = {};
	info.id = kTransitions[I].id;
	info.type = OBS_SOURCE_TYPE_TRANSITION;
	info.get_name = nameOf<I>;
	info.create = createKind<I>;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = defaults;
	info.get_properties = properties;
	info.video_tick = tick;
	info.video_render = render;
	info.audio_render = audioRender;
	obs_register_source(&info);
}

} // namespace

void registerBrandTransitions()
{
	registerOne<0>();
	registerOne<1>();
	registerOne<2>();
	registerOne<3>();
	registerOne<4>();
	registerOne<5>();
	registerOne<6>();
	registerOne<7>();
	registerOne<8>();
}

} // namespace laycue
