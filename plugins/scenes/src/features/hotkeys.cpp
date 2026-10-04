#include "features/hotkeys.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <obs-module.h>
#include <plugin-support.h>

namespace laycue {

namespace {
constexpr const char *kSaveKey = "laycue_hotkeys";
}

Hotkeys::Hotkeys(QObject *context, std::function<void(HotkeyAction)> handler)
	: context_(context),
	  handler_(std::move(handler))
{
	struct Definition {
		HotkeyAction action;
		const char *name;        // stable id, saved with the bindings
		const char *description; // locale key shown in OBS's Hotkeys settings
	};
	const Definition definitions[] = {
		{HotkeyAction::GoLive, "Laycue.GoLive", "Laycue.Hotkey.GoLive"},
		{HotkeyAction::Cut, "Laycue.Cut", "Laycue.Hotkey.Cut"},
		{HotkeyAction::Unpin, "Laycue.Unpin", "Laycue.Hotkey.Unpin"},
		{HotkeyAction::NextCamera, "Laycue.NextCamera", "Laycue.Hotkey.NextCamera"},
		{HotkeyAction::PreviousCamera, "Laycue.PreviousCamera", "Laycue.Hotkey.PreviousCamera"},
		{HotkeyAction::ClearUpNext, "Laycue.ClearUpNext", "Laycue.Hotkey.ClearUpNext"},
	};
	for (const Definition &definition : definitions) {
		auto *entry = new Entry();
		entry->owner = this;
		entry->action = definition.action;
		entry->name = definition.name;
		entry->id = obs_hotkey_register_frontend(definition.name, obs_module_text(definition.description),
							 pressed, entry);
		entries_.push_back(entry);
	}
	obs_frontend_add_save_callback(saveCallback, this);
}

Hotkeys::~Hotkeys()
{
	obs_frontend_remove_save_callback(saveCallback, this);
	for (Entry *entry : entries_) {
		if (entry->id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(entry->id);
		delete entry;
	}
}

void Hotkeys::pressed(void *data, obs_hotkey_id, obs_hotkey_t *, bool down)
{
	// Runs on OBS's hotkey thread: only pass the request on to the UI thread.
	if (!down)
		return;
	try {
		auto *entry = static_cast<Entry *>(data);
		Hotkeys *self = entry ? entry->owner : nullptr;
		if (!self || !self->context_ || !self->handler_)
			return;
		const HotkeyAction action = entry->action;
		QMetaObject::invokeMethod(
			self->context_.data(),
			[self, action]() {
				try {
					self->handler_(action);
				} catch (...) {
					obs_log(LOG_ERROR, "A Laycue hotkey failed");
				}
			},
			Qt::QueuedConnection);
	} catch (...) {
		obs_log(LOG_ERROR, "A Laycue hotkey failed");
	}
}

void Hotkeys::saveCallback(obs_data_t *saveData, bool saving, void *param)
{
	auto *self = static_cast<Hotkeys *>(param);
	try {
		if (saving) {
			OBSDataAutoRelease bindings = obs_data_create();
			for (const Entry *entry : self->entries_) {
				OBSDataArrayAutoRelease array = obs_hotkey_save(entry->id);
				obs_data_set_array(bindings, entry->name, array);
			}
			obs_data_set_obj(saveData, kSaveKey, bindings);
		} else {
			OBSDataAutoRelease bindings = obs_data_get_obj(saveData, kSaveKey);
			if (!bindings)
				return;
			for (const Entry *entry : self->entries_) {
				OBSDataArrayAutoRelease array = obs_data_get_array(bindings, entry->name);
				if (array)
					obs_hotkey_load(entry->id, array);
			}
		}
	} catch (...) {
		obs_log(LOG_ERROR, "Could not %s Laycue's hotkeys", saving ? "save" : "load");
	}
}

} // namespace laycue
