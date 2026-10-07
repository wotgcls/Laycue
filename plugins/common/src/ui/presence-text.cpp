#include "ui/presence-text.hpp"

#include "ui/text.hpp"

namespace laycue::ui {

QString presenceText(const ScenePresence &presence, bool &live)
{
	live = false;
	if (presence.scenes.empty())
		return lt("Laycue.Presence.Nowhere");
	if (presence.inProgramScene && presence.shownInProgram) {
		live = true;
		return lt("Laycue.Presence.OnProgram").arg(QString::fromStdString(presence.programScene));
	}
	if (presence.inProgramScene)
		return lt("Laycue.Presence.EyeOff").arg(QString::fromStdString(presence.programScene));
	QString names = QString::fromStdString(presence.scenes.front());
	if (presence.scenes.size() > 1)
		names += lt("Laycue.Presence.More").arg(int(presence.scenes.size()) - 1);
	return lt("Laycue.Presence.NotLive").arg(names);
}

} // namespace laycue::ui
