// The EasyWorship-style reference box: one framed box holding three parts, Book | Chapter | Verse.
// The book fills in as you type ("joh" shows "John" with the rest selected), Space moves to the next part, Enter
// shows the verse. Clicking a part selects all of it, so a new number can be typed straight over the old one.
#pragma once

#include "core/reference.hpp"

#include <QLineEdit>
#include <QString>
#include <QWidget>

class QLabel;

namespace laycue::scripture {

class ReferenceBox : public QWidget {
	Q_OBJECT

public:
	explicit ReferenceBox(QWidget *parent = nullptr);

	// "John 3:16", ready for parseReference.
	QString text() const;
	void setReference(const Reference &ref); // fills the three parts (does not emit edited)
	void setText(const QString &typed);      // a saved reference such as "John 3:16-18"
	void focusBook();

signals:
	void edited();
	void submitted();            // Enter
	void stepped(int direction); // Up / Down: previous / next verse

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

private:
	void bookEdited(const QString &typed);
	void acceptBook(); // takes the suggestion and moves on to the chapter
	void moveTo(QLineEdit *edit);

	QLineEdit *book_ = nullptr;
	QLineEdit *chapter_ = nullptr;
	QLineEdit *verse_ = nullptr;
	QLabel *hint_ = nullptr; // "→ John" when the typed letters are a short form, not the name's start
	QString suggestion_;
	qsizetype lastBookLength_ = 0;
	bool filling_ = false;
};

} // namespace laycue::scripture
