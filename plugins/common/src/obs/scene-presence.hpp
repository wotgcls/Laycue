// Where a source (a Laycue Title or Clock layer) sits: which scenes hold it, and whether the scene that is
// on Program right now shows it. UI thread only.
#pragma once

#include <string>
#include <vector>

namespace laycue {

struct ScenePresence {
	std::vector<std::string> scenes; // every scene that holds the source, in scene-list order
	std::string programScene;        // the scene on Program right now
	bool inProgramScene = false;     // the Program scene holds the source
	bool shownInProgram = false;     // ...and its eye icon is on, so viewers can see it
};

ScenePresence presenceOf(const std::string &sourceName);

} // namespace laycue
