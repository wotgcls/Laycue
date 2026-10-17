// How a panel finds and drives its slide source (the "Laycue Scripture" or "Laycue Lyrics" layer). UI thread.
#pragma once

#include <string>
#include <vector>

namespace laycue::slides {

struct Slide {
	std::string text;
	std::string caption;
	int look = 0;
	std::string background;
	bool shown = false;
};

// Names of every source of this type in the scene collection.
std::vector<std::string> sourcesOf(const char *id);

// The first source of this type; if there is none, one named `name` is made and put on top of the scene that is
// live now. Returns its name, or "" with `error` set.
std::string ensureSource(const char *id, const std::string &name, std::string &error);

// Puts the source in the live scene (on top) if that scene doesn't have it yet. False if there is no live scene.
bool addToLiveScene(const std::string &sourceName);

// Whether the source is set to show (false if it is gone).
bool isShown(const std::string &sourceName);

// Fades out every Scripture and Lyrics layer except those of `keepId`, so a verse and a song never stack.
void hideOthers(const char *keepId);

// Writes the slide into the source; it fades to the new words (or out, when `shown` is false).
bool send(const std::string &sourceName, const Slide &slide);

} // namespace laycue::slides
