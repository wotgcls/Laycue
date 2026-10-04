// The Overlays tab: switch labelled overlays on and off on the live scene.
#pragma once

#include "core/geometry.hpp"

#include <QString>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

class QGridLayout;
class QLabel;
class QVBoxLayout;

namespace laycue::ui {

struct OverlayRow {
	std::string sceneUuid;
	int64_t itemId = 0;
	std::string sourceUuid;
	QString name;
	QString tag;
	bool visible = false;
	RectF spot; // as fractions of the canvas (0..1), for the little screen icon

	bool operator==(const OverlayRow &o) const
	{
		return sceneUuid == o.sceneUuid && itemId == o.itemId && sourceUuid == o.sourceUuid && name == o.name &&
		       tag == o.tag && visible == o.visible && spot.x == o.spot.x && spot.y == o.spot.y &&
		       spot.w == o.spot.w && spot.h == o.spot.h;
	}
};

class OverlaysTab : public QWidget {
	Q_OBJECT

public:
	explicit OverlaysTab(QWidget *parent = nullptr);

	// `sceneName` is the live scene; `anyLabelled` is whether any source has a label yet.
	void setRows(const std::vector<OverlayRow> &rows, const QString &sceneName, bool anyLabelled, bool locked);

signals:
	void toggled(const std::string &sceneUuid, int64_t itemId, bool show);
	void setupRequested();

private:
	void rebuild();

	std::vector<OverlayRow> rows_;
	QString sceneName_;
	bool anyLabelled_ = false;
	bool locked_ = false;
	bool built_ = false;
	QVBoxLayout *root_ = nullptr;
};

} // namespace laycue::ui
