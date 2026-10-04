#include "store/config-io.hpp"

namespace laycue {

namespace {

const char *scopeName(Scope scope)
{
	switch (scope) {
	case Scope::SameScene:
		return "same";
	case Scope::AllScenes:
		return "all";
	case Scope::ChosenScenes:
		return "chosen";
	}
	return "same";
}

Scope scopeFromName(const std::string &name)
{
	if (name == "all")
		return Scope::AllScenes;
	if (name == "chosen")
		return Scope::ChosenScenes;
	return Scope::SameScene;
}

std::string getString(obs_data_t *data, const char *key)
{
	const char *value = obs_data_get_string(data, key);
	return value ? std::string(value) : std::string();
}

// A list of plain strings is stored as an array of {"v": "..."} objects.
void putStrings(obs_data_t *data, const char *key, const std::vector<std::string> &values)
{
	OBSDataArrayAutoRelease array = obs_data_array_create();
	for (const std::string &value : values) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "v", value.c_str());
		obs_data_array_push_back(array, item);
	}
	obs_data_set_array(data, key, array);
}

std::vector<std::string> getStrings(obs_data_t *data, const char *key)
{
	std::vector<std::string> values;
	OBSDataArrayAutoRelease array = obs_data_get_array(data, key);
	const size_t count = array ? obs_data_array_count(array) : 0;
	for (size_t i = 0; i < count; ++i) {
		OBSDataAutoRelease item = obs_data_array_item(array, i);
		values.push_back(getString(item, "v"));
	}
	return values;
}

} // namespace

OBSData configToData(const Config &config)
{
	OBSData root = obs_data_create();
	obs_data_set_int(root, "version", config.version);
	obs_data_set_bool(root, "rulesInitialised", config.rulesInitialised);
	obs_data_set_bool(root, "wentLive", config.wentLive);
	putStrings(root, "dismissedTips", config.dismissedTips);

	{
		OBSDataAutoRelease s = obs_data_create();
		const Settings &v = config.settings;
		obs_data_set_bool(s, "insideFade", v.insideFade);
		obs_data_set_int(s, "insideFadeMs", v.insideFadeMs);
		obs_data_set_bool(s, "largeButtons", v.largeButtons);
		obs_data_set_bool(s, "locked", v.locked);
		obs_data_set_bool(s, "showTips", v.showTips);
		obs_data_set_bool(s, "autoKey", v.autoKey);
		obs_data_set_bool(s, "laycueView", v.laycueView);
		obs_data_set_obj(root, "settings", s);
	}

	{
		OBSDataArrayAutoRelease array = obs_data_array_create();
		for (const TagAssignment &t : config.tags) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "uuid", t.sourceUuid.c_str());
			obs_data_set_string(item, "name", t.sourceName.c_str());
			obs_data_set_string(item, "tag", t.tag.c_str());
			obs_data_array_push_back(array, item);
		}
		obs_data_set_array(root, "tags", array);
	}

	{
		OBSDataArrayAutoRelease array = obs_data_array_create();
		for (const Rule &r : config.rules) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "name", r.name.c_str());
			putStrings(item, "tags", r.tags);
			obs_data_set_string(item, "scope", scopeName(r.scope));
			putStrings(item, "scenes", r.scenes);
			obs_data_set_bool(item, "strict", r.strict);
			obs_data_set_bool(item, "paused", r.paused);
			obs_data_array_push_back(array, item);
		}
		obs_data_set_array(root, "rules", array);
	}

	{
		OBSDataArrayAutoRelease array = obs_data_array_create();
		for (const Pushback &p : config.pushbacks) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "id", p.id.c_str());
			obs_data_set_string(item, "name", p.name.c_str());
			obs_data_set_string(item, "sceneUuid", p.sceneUuid.c_str());
			obs_data_set_string(item, "sceneName", p.sceneName.c_str());
			obs_data_set_string(item, "imageUuid", p.imageUuid.c_str());
			obs_data_set_string(item, "imageName", p.imageName.c_str());
			obs_data_set_int(item, "imageWidth", p.imageWidth);
			obs_data_set_int(item, "imageHeight", p.imageHeight);
			obs_data_set_int(item, "windowX", p.window.x);
			obs_data_set_int(item, "windowY", p.window.y);
			obs_data_set_int(item, "windowW", p.window.width);
			obs_data_set_int(item, "windowH", p.window.height);
			obs_data_set_string(item, "fit", p.fit == FitMode::Whole ? "whole" : "fill");
			obs_data_set_string(item, "activeFeed", p.activeFeed.c_str());
			obs_data_set_bool(item, "keyed", p.keyed);
			obs_data_set_int(item, "keyR", p.keyColour.r);
			obs_data_set_int(item, "keyG", p.keyColour.g);
			obs_data_set_int(item, "keyB", p.keyColour.b);
			OBSDataArrayAutoRelease feeds = obs_data_array_create();
			for (const Feed &f : p.feeds) {
				OBSDataAutoRelease feed = obs_data_create();
				obs_data_set_string(feed, "uuid", f.uuid.c_str());
				obs_data_set_string(feed, "name", f.name.c_str());
				obs_data_array_push_back(feeds, feed);
			}
			obs_data_set_array(item, "feeds", feeds);
			obs_data_array_push_back(array, item);
		}
		obs_data_set_array(root, "pushbacks", array);
	}

	{
		OBSDataArrayAutoRelease array = obs_data_array_create();
		for (const Layout &l : config.layouts) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "id", l.id.c_str());
			obs_data_set_string(item, "name", l.name.c_str());
			obs_data_set_string(item, "template", l.templateId.c_str());
			obs_data_set_string(item, "sceneUuid", l.sceneUuid.c_str());
			obs_data_set_string(item, "sceneName", l.sceneName.c_str());
			obs_data_set_string(item, "fit", l.fit == FitMode::Whole ? "whole" : "fill");
			OBSDataArrayAutoRelease slots = obs_data_array_create();
			for (const SlotSource &slot : l.sources) {
				OBSDataAutoRelease entry = obs_data_create();
				obs_data_set_string(entry, "uuid", slot.uuid.c_str());
				obs_data_set_string(entry, "name", slot.name.c_str());
				obs_data_array_push_back(slots, entry);
			}
			obs_data_set_array(item, "slots", slots);
			obs_data_array_push_back(array, item);
		}
		obs_data_set_array(root, "layouts", array);
	}

	return root;
}

Config configFromData(obs_data_t *data)
{
	Config config;
	if (!data)
		return config;

	config.rulesInitialised = obs_data_get_bool(data, "rulesInitialised");
	config.wentLive = obs_data_get_bool(data, "wentLive");
	config.dismissedTips = getStrings(data, "dismissedTips");

	{
		OBSDataAutoRelease s = obs_data_get_obj(data, "settings");
		if (s) {
			Settings &v = config.settings;
			// Defaults first, so a setting missing from an older file keeps its default.
			obs_data_set_default_bool(s, "insideFade", v.insideFade);
			obs_data_set_default_int(s, "insideFadeMs", v.insideFadeMs);
			obs_data_set_default_bool(s, "largeButtons", v.largeButtons);
			obs_data_set_default_bool(s, "locked", v.locked);
			obs_data_set_default_bool(s, "showTips", v.showTips);
			obs_data_set_default_bool(s, "autoKey", v.autoKey);
			obs_data_set_default_bool(s, "laycueView", v.laycueView);
			v.insideFade = obs_data_get_bool(s, "insideFade");
			v.insideFadeMs = static_cast<int>(obs_data_get_int(s, "insideFadeMs"));
			v.largeButtons = obs_data_get_bool(s, "largeButtons");
			v.locked = obs_data_get_bool(s, "locked");
			v.showTips = obs_data_get_bool(s, "showTips");
			v.autoKey = obs_data_get_bool(s, "autoKey");
			v.laycueView = obs_data_get_bool(s, "laycueView");
		}
	}

	{
		OBSDataArrayAutoRelease array = obs_data_get_array(data, "tags");
		const size_t count = array ? obs_data_array_count(array) : 0;
		for (size_t i = 0; i < count; ++i) {
			OBSDataAutoRelease item = obs_data_array_item(array, i);
			config.tags.push_back(
				{getString(item, "uuid"), getString(item, "name"), getString(item, "tag")});
		}
	}

	{
		OBSDataArrayAutoRelease array = obs_data_get_array(data, "rules");
		const size_t count = array ? obs_data_array_count(array) : 0;
		for (size_t i = 0; i < count; ++i) {
			OBSDataAutoRelease item = obs_data_array_item(array, i);
			Rule r;
			r.name = getString(item, "name");
			r.tags = getStrings(item, "tags");
			r.scope = scopeFromName(getString(item, "scope"));
			r.scenes = getStrings(item, "scenes");
			r.strict = obs_data_get_bool(item, "strict");
			r.paused = obs_data_get_bool(item, "paused");
			config.rules.push_back(std::move(r));
		}
	}

	{
		OBSDataArrayAutoRelease array = obs_data_get_array(data, "pushbacks");
		const size_t count = array ? obs_data_array_count(array) : 0;
		for (size_t i = 0; i < count; ++i) {
			OBSDataAutoRelease item = obs_data_array_item(array, i);
			Pushback p;
			p.id = getString(item, "id");
			p.name = getString(item, "name");
			p.sceneUuid = getString(item, "sceneUuid");
			p.sceneName = getString(item, "sceneName");
			p.imageUuid = getString(item, "imageUuid");
			p.imageName = getString(item, "imageName");
			p.imageWidth = static_cast<int>(obs_data_get_int(item, "imageWidth"));
			p.imageHeight = static_cast<int>(obs_data_get_int(item, "imageHeight"));
			p.window = {static_cast<int>(obs_data_get_int(item, "windowX")),
				    static_cast<int>(obs_data_get_int(item, "windowY")),
				    static_cast<int>(obs_data_get_int(item, "windowW")),
				    static_cast<int>(obs_data_get_int(item, "windowH"))};
			p.fit = getString(item, "fit") == "whole" ? FitMode::Whole : FitMode::Fill;
			p.activeFeed = getString(item, "activeFeed");
			p.keyed = obs_data_get_bool(item, "keyed");
			p.keyColour = {static_cast<uint8_t>(obs_data_get_int(item, "keyR")),
				       static_cast<uint8_t>(obs_data_get_int(item, "keyG")),
				       static_cast<uint8_t>(obs_data_get_int(item, "keyB"))};
			OBSDataArrayAutoRelease feeds = obs_data_get_array(item, "feeds");
			const size_t feedCount = feeds ? obs_data_array_count(feeds) : 0;
			for (size_t f = 0; f < feedCount; ++f) {
				OBSDataAutoRelease feed = obs_data_array_item(feeds, f);
				p.feeds.push_back({getString(feed, "uuid"), getString(feed, "name")});
			}
			config.pushbacks.push_back(std::move(p));
		}
	}

	{
		OBSDataArrayAutoRelease array = obs_data_get_array(data, "layouts");
		const size_t count = array ? obs_data_array_count(array) : 0;
		for (size_t i = 0; i < count; ++i) {
			OBSDataAutoRelease item = obs_data_array_item(array, i);
			Layout l;
			l.id = getString(item, "id");
			l.name = getString(item, "name");
			l.templateId = getString(item, "template");
			l.sceneUuid = getString(item, "sceneUuid");
			l.sceneName = getString(item, "sceneName");
			l.fit = getString(item, "fit") == "whole" ? FitMode::Whole : FitMode::Fill;
			OBSDataArrayAutoRelease slots = obs_data_get_array(item, "slots");
			const size_t slotCount = slots ? obs_data_array_count(slots) : 0;
			for (size_t k = 0; k < slotCount; ++k) {
				OBSDataAutoRelease entry = obs_data_array_item(slots, k);
				l.sources.push_back({getString(entry, "uuid"), getString(entry, "name")});
			}
			config.layouts.push_back(std::move(l));
		}
	}

	sanitize(config);
	return config;
}

std::string configToJson(const Config &config)
{
	OBSData data = configToData(config);
	const char *json = obs_data_get_json_pretty(data);
	return json ? std::string(json) : std::string();
}

bool configFromJson(const std::string &json, Config &out)
{
	OBSDataAutoRelease data = obs_data_create_from_json(json.c_str());
	if (!data)
		return false;
	out = configFromData(data);
	return true;
}

} // namespace laycue
