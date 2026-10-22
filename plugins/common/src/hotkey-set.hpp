// A group of OBS hotkeys (Settings > Hotkeys) that run on the UI thread. Each plugin of the pack
// uses one for its own actions, so a keyboard, a Stream Deck or a foot pedal can drive it. Bindings
// are saved with the scene collection under `saveKey`.
#pragma once

#include <obs.h>

#include <QObject>
#include <QPointer>

#include <functional>
#include <string>
#include <vector>

namespace laycue {

class HotkeySet {
public:
	struct Definition {
		const char *name;           // stable id, saved with the binding ("Laycue.Titles.Toggle")
		const char *descriptionKey; // locale key for the text shown in OBS's Hotkeys settings
	};

	// `context` is a UI object; `handler(index)` runs on its thread, where OBS's hotkey thread isn't.
	HotkeySet(QObject *context, std::string saveKey, const std::vector<Definition> &definitions,
		  std::function<void(int)> handler);
	~HotkeySet();

	HotkeySet(const HotkeySet &) = delete;
	HotkeySet &operator=(const HotkeySet &) = delete;

private:
	struct Entry {
		HotkeySet *owner = nullptr;
		int index = 0;
		std::string name;
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	};

	static void pressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool down);
	static void saveCallback(obs_data_t *saveData, bool saving, void *param);

	QPointer<QObject> context_;
	std::string saveKey_;
	std::function<void(int)> handler_;
	std::vector<Entry *> entries_; // owned; freed in the destructor
};

} // namespace laycue
