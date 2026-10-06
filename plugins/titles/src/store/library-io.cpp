#include "store/library-io.hpp"

#include <algorithm>

#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>
#include <util/platform.h>

#include <map>

namespace laycue::titles {

namespace {

std::string str(const char *s)
{
	return s ? std::string(s) : std::string();
}

std::string filePath()
{
	char *path = obs_module_config_path("library.json");
	std::string result = str(path);
	bfree(path);
	return result;
}

OBSData contentToData(const TitleContent &c)
{
	OBSData d = obs_data_create();
	obs_data_set_string(d, "line1", c.line1.c_str());
	obs_data_set_string(d, "line2", c.line2.c_str());
	obs_data_set_string(d, "tag", c.tag.c_str());
	obs_data_set_string(d, "note", c.note.c_str());
	obs_data_set_string(d, "logo", c.logo.c_str());
	obs_data_set_int(d, "logo_cut", c.logoCut);
	obs_data_set_bool(d, "logo_stacked", c.logoStacked);
	obs_data_set_bool(d, "logo_mark_second", c.logoMarkSecond);
	OBSDataArrayAutoRelease rows = obs_data_array_create();
	for (const Row &r : c.rows) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "label", r.label.c_str());
		obs_data_set_string(item, "value", r.value.c_str());
		obs_data_array_push_back(rows, item);
	}
	obs_data_set_array(d, "rows", rows);
	return d;
}

TitleContent contentFromData(obs_data_t *d)
{
	TitleContent c;
	if (!d)
		return c;
	c.line1 = str(obs_data_get_string(d, "line1"));
	c.line2 = str(obs_data_get_string(d, "line2"));
	c.tag = str(obs_data_get_string(d, "tag"));
	c.note = str(obs_data_get_string(d, "note"));
	c.logo = str(obs_data_get_string(d, "logo"));
	c.logoCut = int(obs_data_get_int(d, "logo_cut"));
	c.logoStacked = obs_data_get_bool(d, "logo_stacked");
	c.logoMarkSecond = obs_data_get_bool(d, "logo_mark_second");
	OBSDataArrayAutoRelease rows = obs_data_get_array(d, "rows");
	const size_t n = rows ? std::min(obs_data_array_count(rows), kMaxRows) : 0;
	for (size_t i = 0; i < n; ++i) {
		OBSDataAutoRelease item = obs_data_array_item(rows, i);
		c.rows.push_back({str(obs_data_get_string(item, "label")), str(obs_data_get_string(item, "value"))});
	}
	return c;
}

} // namespace

// A list of queued titles <-> obs_data. The caller owns the returned array (queueFromArray frees its argument).
static std::vector<QueueItem> queueFromArray(obs_data_array_t *array)
{
	OBSDataArrayAutoRelease owned(array);
	std::vector<QueueItem> items;
	const size_t n = owned ? obs_data_array_count(owned) : 0;
	for (size_t i = 0; i < n; ++i) {
		OBSDataAutoRelease item = obs_data_array_item(owned, i);
		QueueItem q;
		q.styleId = str(obs_data_get_string(item, "style"));
		q.layer = str(obs_data_get_string(item, "layer"));
		OBSDataAutoRelease content = obs_data_get_obj(item, "content");
		q.content = contentFromData(content);
		items.push_back(std::move(q));
	}
	return items;
}

static obs_data_array_t *queueToArray(const std::vector<QueueItem> &items)
{
	obs_data_array_t *array = obs_data_array_create();
	for (const QueueItem &q : items) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "style", q.styleId.c_str());
		obs_data_set_string(item, "layer", q.layer.c_str());
		OBSData content = contentToData(q.content);
		obs_data_set_obj(item, "content", content);
		obs_data_array_push_back(array, item);
	}
	return array;
}

Library loadLibrary()
{
	Library lib;
	try {
		const std::string path = filePath();
		if (path.empty() || !os_file_exists(path.c_str()))
			return lib;
		OBSDataAutoRelease root = obs_data_create_from_json_file(path.c_str());
		if (!root)
			return lib;

		lib.selected = str(obs_data_get_string(root, "selected"));
		lib.useBrand = obs_data_get_bool(root, "useBrand");
		lib.speed = obs_data_has_user_value(root, "speed") ? int(obs_data_get_int(root, "speed"))
								   : kDefaultSpeed;
		lib.holdOverride = obs_data_has_user_value(root, "hold") ? int(obs_data_get_int(root, "hold")) : -1;
		lib.targetSource = str(obs_data_get_string(root, "target"));
		parseColour(str(obs_data_get_string(root, "brand")), lib.brand);
		{
			OBSDataAutoRelease draft = obs_data_get_obj(root, "draft");
			lib.draft = contentFromData(draft);
		}
		{
			OBSDataArrayAutoRelease layers = obs_data_get_array(root, "layers");
			const size_t n = layers ? obs_data_array_count(layers) : 0;
			for (size_t i = 0; i < n; ++i) {
				OBSDataAutoRelease item = obs_data_array_item(layers, i);
				const std::string name = str(obs_data_get_string(item, "name"));
				if (name.empty())
					continue;
				OBSDataAutoRelease content = obs_data_get_obj(item, "content");
				lib.layers[name] = LayerState{str(obs_data_get_string(item, "selected")),
							      contentFromData(content)};
			}
		}
		{
			OBSDataArrayAutoRelease customs = obs_data_get_array(root, "customs");
			const size_t n = customs ? obs_data_array_count(customs) : 0;
			for (size_t i = 0; i < n; ++i) {
				OBSDataAutoRelease item = obs_data_array_item(customs, i);
				std::map<std::string, std::string> values;
				for (obs_data_item_t *entry = obs_data_first(item); entry; obs_data_item_next(&entry)) {
					const char *name = obs_data_item_get_name(entry);
					if (name && obs_data_item_gettype(entry) == OBS_DATA_STRING)
						values[name] = str(obs_data_item_get_string(entry));
				}
				lib.customs.push_back(fromMap(values));
			}
		}
		lib.queue = queueFromArray(obs_data_get_array(root, "queue"));
		lib.queueCurrent = obs_data_has_user_value(root, "queueCurrent")
					   ? int(obs_data_get_int(root, "queueCurrent"))
					   : -1;
		lib.serviceName = str(obs_data_get_string(root, "service"));
		{
			OBSDataArrayAutoRelease services = obs_data_get_array(root, "services");
			const size_t n = services ? obs_data_array_count(services) : 0;
			for (size_t i = 0; i < n; ++i) {
				OBSDataAutoRelease entry = obs_data_array_item(services, i);
				const std::string name = str(obs_data_get_string(entry, "name"));
				if (!name.empty())
					lib.services[name] = queueFromArray(obs_data_get_array(entry, "items"));
			}
		}
		{
			OBSDataAutoRelease placements = obs_data_get_obj(root, "placements");
			if (placements) {
				for (obs_data_item_t *entry = obs_data_first(placements); entry;
				     obs_data_item_next(&entry)) {
					const char *name = obs_data_item_get_name(entry);
					if (!name || obs_data_item_gettype(entry) != OBS_DATA_OBJECT)
						continue;
					OBSDataAutoRelease item = obs_data_item_get_obj(entry);
					Placement p;
					p.align = int(obs_data_get_int(item, "align"));
					p.percent = obs_data_has_user_value(item, "percent")
							    ? int(obs_data_get_int(item, "percent"))
							    : 100;
					p.fit = int(obs_data_get_int(item, "fit"));
					p.width = obs_data_has_user_value(item, "width")
							  ? int(obs_data_get_int(item, "width"))
							  : kDefaultFitWidth;
					lib.placements[name] = p;
				}
			}
		}
		sanitize(lib);
	} catch (...) {
		obs_log(LOG_WARNING, "Laycue Titles: could not read the saved library");
		return Library();
	}
	return lib;
}

bool saveLibrary(const Library &lib)
{
	try {
		const std::string path = filePath();
		if (path.empty())
			return false;
		OBSDataAutoRelease root = obs_data_create();
		obs_data_set_int(root, "version", 1);
		obs_data_set_string(root, "selected", lib.selected.c_str());
		obs_data_set_bool(root, "useBrand", lib.useBrand);
		obs_data_set_int(root, "hold", lib.holdOverride);
		obs_data_set_int(root, "speed", lib.speed);
		obs_data_set_string(root, "target", lib.targetSource.c_str());
		obs_data_set_string(root, "brand", colourToString(lib.brand).c_str());
		{
			OBSDataAutoRelease placements = obs_data_create();
			for (const auto &kv : lib.placements) {
				OBSDataAutoRelease item = obs_data_create();
				obs_data_set_int(item, "align", kv.second.align);
				obs_data_set_int(item, "percent", kv.second.percent);
				obs_data_set_int(item, "fit", kv.second.fit);
				obs_data_set_int(item, "width", kv.second.width);
				obs_data_set_obj(placements, kv.first.c_str(), item);
			}
			obs_data_set_obj(root, "placements", placements);
		}
		{
			OBSData draft = contentToData(lib.draft);
			obs_data_set_obj(root, "draft", draft);
		}
		{
			OBSDataArrayAutoRelease layers = obs_data_array_create();
			for (const auto &kv : lib.layers) {
				OBSDataAutoRelease item = obs_data_create();
				obs_data_set_string(item, "name", kv.first.c_str());
				obs_data_set_string(item, "selected", kv.second.selected.c_str());
				OBSData content = contentToData(kv.second.draft);
				obs_data_set_obj(item, "content", content);
				obs_data_array_push_back(layers, item);
			}
			obs_data_set_array(root, "layers", layers);
		}
		{
			OBSDataArrayAutoRelease customs = obs_data_array_create();
			for (const Overrides &o : lib.customs) {
				OBSDataAutoRelease item = obs_data_create();
				for (const auto &kv : toMap(o))
					obs_data_set_string(item, kv.first.c_str(), kv.second.c_str());
				obs_data_array_push_back(customs, item);
			}
			obs_data_set_array(root, "customs", customs);
		}
		{
			OBSDataArrayAutoRelease queue = queueToArray(lib.queue);
			obs_data_set_array(root, "queue", queue);
			obs_data_set_int(root, "queueCurrent", lib.queueCurrent);
			obs_data_set_string(root, "service", lib.serviceName.c_str());
			OBSDataArrayAutoRelease services = obs_data_array_create();
			for (const auto &kv : lib.services) {
				OBSDataAutoRelease entry = obs_data_create();
				obs_data_set_string(entry, "name", kv.first.c_str());
				OBSDataArrayAutoRelease items = queueToArray(kv.second);
				obs_data_set_array(entry, "items", items);
				obs_data_array_push_back(services, entry);
			}
			obs_data_set_array(root, "services", services);
		}
		// Make sure the folder exists, then write through a temporary file so a crash never leaves half a file.
		char *dir = obs_module_config_path("");
		if (dir) {
			os_mkdirs(dir);
			bfree(dir);
		}
		return obs_data_save_json_safe(root, path.c_str(), "tmp", "bak");
	} catch (...) {
		obs_log(LOG_WARNING, "Laycue Titles: could not save the library");
		return false;
	}
}

} // namespace laycue::titles
