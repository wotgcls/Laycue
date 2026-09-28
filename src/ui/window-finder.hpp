// "Find a pushback window" (M1): pick a pushback image or an OBS source and see the
// window Laycue finds in it, outlined. Nothing in OBS is changed.
#pragma once

#include "core/detect.hpp"
#include "obs/pixel-reader.hpp"

#include <QDialog>
#include <QImage>
#include <QWidget>

#include <string>

class QComboBox;
class QLabel;
class QPushButton;

namespace laycue::ui {

// The picture with the found window outlined and everything else dimmed.
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

class WindowFinderDialog : public QDialog {
	Q_OBJECT

public:
	explicit WindowFinderDialog(QWidget *parent = nullptr);

private:
	void chooseFile();
	void chooseSource(int index);
	void reloadSources();
	void setPicture(RgbaImage picture, const QString &origin, bool fromSource);
	void detect(bool automatic);
	void showResult();

	WindowPreview *preview_ = nullptr;
	QComboBox *sourceBox_ = nullptr;
	QPushButton *seeThroughButton_ = nullptr;
	QPushButton *greenButton_ = nullptr;
	QLabel *originLabel_ = nullptr;
	QLabel *resultDot_ = nullptr;
	QLabel *resultLabel_ = nullptr;
	QLabel *detailLabel_ = nullptr;

	RgbaImage picture_;
	bool fromSource_ = false;
	DetectResult result_;
	WindowKind kind_ = WindowKind::SeeThrough;
};

} // namespace laycue::ui
