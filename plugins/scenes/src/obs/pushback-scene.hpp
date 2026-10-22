// Builds and drives a pushback's own OBS scene: the pushback picture on top, and the cameras (or
// other feeds) underneath, each fitted to the picture's window. Laycue only ever changes the
// scene it made and the Chroma Key it adds; the operator's own scenes and sources are shared into
// it, never edited.
#pragma once

#include "core/config.hpp"

#include <memory>
#include <string>

namespace laycue {

struct BuildResult {
	bool ok = false;
	std::string error;
	Pushback pushback; // with sceneUuid and sceneName filled in
};

// Makes the pushback's scene if it doesn't exist (or repairs it if it does), adds the picture and
// the feeds, and fits everything. Safe to call again at any time.
BuildResult buildPushbackScene(const Pushback &pushback);

// Fits every feed to the window and, if `applyVisibility`, shows only the active one. Cheap; call
// before a pushback is shown, because a camera's real size is only known once OBS has drawn it.
bool layoutPushback(const Pushback &pushback, bool applyVisibility = true);

// True if the pushback's scene still exists in OBS.
bool pushbackSceneExists(const Pushback &pushback);

// Renames Laycue's scene to match a new pushback name.
bool renamePushbackScene(const Pushback &pushback, const std::string &newName);

// Shows one feed inside the window. With a fade, the new feed fades in over the old one (call
// hideInactiveFeeds() when the fade is over). Without, the switch is instant.
bool showFeed(const Pushback &pushback, const std::string &feedUuid, bool fade, int fadeMs);

// Hides every feed except the active one.
void hideInactiveFeeds(const Pushback &pushback);

// Removes Laycue's scene for the pushback, and the picture too if Laycue made it. Never touches the
// operator's cameras or their own sources.
bool removePushbackScene(const Pushback &pushback);

// Keeps an OBS source alive while it is held. A source nothing uses yet would otherwise be
// destroyed the moment it is made, so hold it until it has been added to a scene.
using SourceHold = std::shared_ptr<void>;

// A new image source for a picture file. Returns a hold on it (null if OBS couldn't make it) and
// sets `uuid`.
SourceHold createImageSource(const std::string &name, const std::string &file, std::string &uuid);

// Adds (or updates) Laycue's Chroma Key on a source, keying out `key` with the given similarity.
bool setChromaKey(const std::string &sourceUuid, const Rgb &key, int similarity);

// Adds Laycue's Chroma Key with the gentlest strength that clears the window without eating the
// rest of the picture, checked by looking at what OBS draws. Returns the similarity used, or 0 if
// nothing worked (the key is then removed).
int tuneChromaKey(const std::string &sourceUuid, const Rgb &key, const Rect &expectedWindow);

// True if the source has Laycue's Chroma Key.
bool hasLaycueKey(const std::string &sourceUuid);

} // namespace laycue
