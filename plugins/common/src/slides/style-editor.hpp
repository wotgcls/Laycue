// The "Edit this look" controls shared by Scripture and Lyrics: where the slide sits, what is behind the words
// (nothing, a colour, a see-through colour, a gradient or a picture), the colours, the text size and, for songs,
// ALL CAPS or a capital at the start of each line.
#pragma once

#include "slides/slide-style.hpp"

#include <QAbstractButton>
#include <QColor>
#include <QWidget>

class QLabel;

namespace laycue::ui {
class Segmented;
class ActionButton;
} // namespace laycue::ui

namespace laycue::slides {

// A small round-cornered colour chip with a word under... beside it. Opens the colour picker when clicked.
class ColourSwatch : public QAbstractButton {
	Q_OBJECT

public:
	ColourSwatch(const QString &label, QWidget *parent = nullptr);
	void setColour(const QColor &colour);
	QColor colour() const { return colour_; }
	QSize sizeHint() const override;

signals:
	void colourChosen(const QColor &colour);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QColor colour_;
};

class StyleEditor : public QWidget {
	Q_OBJECT

public:
	struct Options {
		bool showCase = false; // the ALL CAPS / First letter choice (songs)
		bool showDecor = false; // the looping Bible picture (verses)
		QString accentLabel;   // what the second colour is for: "Reference", "Song name"
	};

	StyleEditor(const Options &options, QWidget *parent = nullptr);

	void setStyle(const SlideStyle &style); // shows it; does not emit changed()
	SlideStyle style() const { return style_; }

signals:
	void changed(const laycue::slides::SlideStyle &style);
	void resetRequested();

private:
	void refresh();
	void emitChanged();
	void choosePicture();

	SlideStyle style_;
	ui::Segmented *layout_ = nullptr;
	ui::Segmented *fill_ = nullptr;
	ui::Segmented *case_ = nullptr;
	ui::Segmented *align_ = nullptr;
	ui::Segmented *font_ = nullptr;
	ui::Segmented *transition_ = nullptr;
	ui::Segmented *decor_ = nullptr;
	ColourSwatch *colour1_ = nullptr;
	ColourSwatch *colour2_ = nullptr;
	ColourSwatch *text_ = nullptr;
	ColourSwatch *accent_ = nullptr;
	QWidget *pictureRow_ = nullptr;
	QLabel *pictureName_ = nullptr;
	QLabel *size_ = nullptr;
};

} // namespace laycue::slides
