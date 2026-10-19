// The top of every Laycue panel: the Laycue mark and name, then a slim row of names (Clock, Lyrics, Network, Scenes,
// Scripture, Titles). Clicking one shows that
// panel in the same place as this one, so you can move between them without opening more panels.
// The other panel takes this panel's place (docked or floating, as this one is), so nothing new opens. A panel
// that is already open, for instance one you dragged out into its own window, stays where it is and comes forward.
#pragma once

#include <QPixmap>
#include <QRect>
#include <QString>
#include <QWidget>

#include <vector>

namespace laycue::ui {

class PackSwitcher : public QWidget {
public:
	explicit PackSwitcher(QWidget *parent);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	struct Chip {
		QString name;
		QWidget *content = nullptr;
		QRect rect;
		bool current = false;
	};
	void layoutChips();
	std::vector<Chip> chips_;
	QPixmap mark_; // the Laycue mark, for this panel's light or dark theme
};

} // namespace laycue::ui
