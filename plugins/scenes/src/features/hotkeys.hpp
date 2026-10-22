// Laycue's actions as OBS hotkeys (Settings → Hotkeys), so a keyboard, a Stream Deck or a foot
// pedal can run the service. Bindings are saved with the scene collection.
#pragma once

#include <obs.h>

#include <QObject>
#include <QPointer>

#include <functional>
#include <vector>

namespace laycue {

enum class HotkeyAction { GoLive, Cut, Unpin, NextCamera, PreviousCamera, ClearUpNext };

class Hotkeys {
public:
	// `context` is the panel; `handler` runs on the UI thread, where OBS's hotkey thread isn't.
	Hotkeys(QObject *context, std::function<void(HotkeyAction)> handler);
	~Hotkeys();

	Hotkeys(const Hotkeys &) = delete;
	Hotkeys &operator=(const Hotkeys &) = delete;

private:
	struct Entry {
		Hotkeys *owner = nullptr;
		HotkeyAction action = HotkeyAction::GoLive;
		const char *name = "";
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	};

	static void pressed(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool down);
	static void saveCallback(obs_data_t *saveData, bool saving, void *param);

	QPointer<QObject> context_;
	std::function<void(HotkeyAction)> handler_;
	std::vector<Entry *> entries_; // owned; freed in the destructor
};

} // namespace laycue
