#include "store/library-io.hpp"

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
	return c;
}

} // namespace

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
		lib.holdOverride = obs_data_has_user_value(root, "hold") ? int(obs_data_get_int(root, "hold")) : -1;
		lib.targetSource = str(obs_data_get_string(root, "target"));
		parseColour(str(obs_data_get_string(root, "brand")), lib.brand);
		{
			OBSDataAutoRelease draft = obs_data_get_obj(root, "draft");
			lib.draft = contentFromData(draft);
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
		{
			OBSDataArrayAutoRelease queue = obs_data_get_array(root, "queue");
			const size_t n = queue ? obs_data_array_count(queue) : 0;
			for (size_t i = 0; i < n; ++i) {
				OBSDataAutoRelease item = obs_data_array_item(queue, i);
				QueueItem q;
				q.styleId = str(obs_data_get_string(item, "style"));
				OBSDataAutoRelease content = obs_data_get_obj(item, "content");
				q.content = contentFromData(content);
				lib.queue.push_back(std::move(q));
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
		obs_data_set_string(root, "target", lib.targetSource.c_str());
		obs_data_set_string(root, "brand", colourToString(lib.brand).c_str());
		{
			OBSDataAutoRelease placements = obs_data_create();
			for (const auto &kv : lib.placements) {
				OBSDataAutoRelease item = obs_data_create();
				obs_data_set_int(item, "align", kv.second.align);
				obs_data_set_int(item, "percent", kv.second.percent);
				obs_data_set_obj(placements, kv.first.c_str(), item);
			}
			obs_data_set_obj(root, "placements", placements);
		}
		{
			OBSData draft = contentToData(lib.draft);
			obs_data_set_obj(root, "draft", draft);
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
			OBSDataArrayAutoRelease queue = obs_data_array_create();
			for (const QueueItem &q : lib.queue) {
				OBSDataAutoRelease item = obs_data_create();
				obs_data_set_string(item, "style", q.styleId.c_str());
				OBSData content = contentToData(q.content);
				obs_data_set_obj(item, "content", content);
				obs_data_array_push_back(queue, item);
			}
			obs_data_set_array(root, "queue", queue);
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
