#include "hotkey-set.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>

namespace laycue {

HotkeySet::HotkeySet(QObject *context, std::string saveKey, const std::vector<Definition> &definitions,
		     std::function<void(int)> handler)
	: context_(context),
	  saveKey_(std::move(saveKey)),
	  handler_(std::move(handler))
{
	int index = 0;
	for (const Definition &definition : definitions) {
		auto *entry = new Entry();
		entry->owner = this;
		entry->index = index++;
		entry->name = definition.name;
		entry->id = obs_hotkey_register_frontend(definition.name, obs_module_text(definition.descriptionKey),
							 pressed, entry);
		entries_.push_back(entry);
	}
	obs_frontend_add_save_callback(saveCallback, this);
}

HotkeySet::~HotkeySet()
{
	obs_frontend_remove_save_callback(saveCallback, this);
	for (Entry *entry : entries_) {
		if (entry->id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(entry->id);
		delete entry;
	}
}

void HotkeySet::pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool down)
{
	// Runs on OBS's hotkey thread: only pass the request on to the UI thread.
	if (!down)
		return;
	try {
		auto *entry = static_cast<Entry *>(data);
		HotkeySet *self = entry ? entry->owner : nullptr;
		if (!self || !self->context_ || !self->handler_)
			return;
		const int index = entry->index;
		QMetaObject::invokeMethod(
			self->context_.data(),
			[self, index]() {
				try {
					self->handler_(index);
				} catch (...) {
					obs_log(LOG_ERROR, "A Laycue hotkey failed");
				}
			},
			Qt::QueuedConnection);
	} catch (...) {
		obs_log(LOG_ERROR, "A Laycue hotkey failed");
	}
}

void HotkeySet::saveCallback(obs_data_t *saveData, bool saving, void *param)
{
	auto *self = static_cast<HotkeySet *>(param);
	try {
		if (saving) {
			OBSDataAutoRelease bindings = obs_data_create();
			for (const Entry *entry : self->entries_) {
				OBSDataArrayAutoRelease array = obs_hotkey_save(entry->id);
				obs_data_set_array(bindings, entry->name.c_str(), array);
			}
			obs_data_set_obj(saveData, self->saveKey_.c_str(), bindings);
		} else {
			OBSDataAutoRelease bindings = obs_data_get_obj(saveData, self->saveKey_.c_str());
			if (!bindings)
				return;
			for (const Entry *entry : self->entries_) {
				OBSDataArrayAutoRelease array = obs_data_get_array(bindings, entry->name.c_str());
				if (array)
					obs_hotkey_load(entry->id, array);
			}
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Could not %s Laycue's hotkeys", saving ? "save" : "load");
	}
}

} // namespace laycue
