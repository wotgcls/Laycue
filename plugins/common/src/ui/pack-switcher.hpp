// A slim row of names (Clock, Network, Scenes, Titles) at the top of every Laycue panel. Clicking one shows that
// panel in the same place as this one (as a tab beside it), so you can move between them without opening more panels.
// A panel you have dragged out into its own window stays where it is and is just brought to the front.
#pragma once

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
};

} // namespace laycue::ui
