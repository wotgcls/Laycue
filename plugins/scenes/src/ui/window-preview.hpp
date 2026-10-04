// A picture with the found pushback window outlined and everything else dimmed.
#pragma once

#include "core/detect.hpp"

#include <QImage>
#include <QWidget>

namespace laycue::ui {

class WindowPreview : public QWidget {
	Q_OBJECT

public:
	explicit WindowPreview(QWidget *parent = nullptr);

	void setPicture(const QImage &image);
	void setWindow(const Rect &window, bool found, bool confident);

	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override;
	QSize sizeHint() const override { return {420, 236}; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QImage image_;
	Rect window_;
	bool found_ = false;
	bool confident_ = false;
};

} // namespace laycue::ui
