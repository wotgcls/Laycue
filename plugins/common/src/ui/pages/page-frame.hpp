// The frame every full-panel page shares: a Back button, a title, and a scrolling body.
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace laycue::ui {

class PageFrame : public QWidget {
	Q_OBJECT

public:
	PageFrame(const QString &backLabel, const QString &title, QWidget *parent = nullptr);

	// Add the page's content here.
	QVBoxLayout *body() { return body_; }

	// A widget pinned under the scrolling body (a primary action, for example).
	void setFooter(QWidget *footer);
	void setTitle(const QString &title);

signals:
	void backRequested();

private:
	QLabel *title_ = nullptr;
	QVBoxLayout *body_ = nullptr;
	QVBoxLayout *root_ = nullptr;
};

} // namespace laycue::ui
