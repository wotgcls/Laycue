// Small building blocks shared by the full-panel pages.
#pragma once

#include "ui/controls.hpp"
#include "ui/tokens.hpp"

#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QString>

namespace laycue::ui::page {

inline QLabel *label(QWidget *parent, const QString &text, int px, int weight, const QColor &colour)
{
	auto *l = new QLabel(text, parent);
	l->setFont(uiFont(px, weight));
	l->setStyleSheet(labelStyle(colour, l->font()));
	l->setWordWrap(true);
	l->setContentsMargins(size::shadowMargin, 0, size::shadowMargin, 0); // line up with the cards' edges
	return l;
}

// A label with no indent, for use inside a card.
inline QLabel *innerLabel(QWidget *parent, const QString &text, int px, int weight, const QColor &colour)
{
	QLabel *l = label(parent, text, px, weight, colour);
	l->setContentsMargins(0, 0, 0, 0);
	return l;
}

// `object` names the look: laycuePrimary, laycueSecondary, laycueDanger or laycueLink.
inline ActionButton *button(QWidget *parent, const QString &text, const char *object)
{
	const QString name = QString::fromLatin1(object);
	ActionButton::Kind kind = ActionButton::Kind::Secondary;
	if (name == "laycuePrimary")
		kind = ActionButton::Kind::Primary;
	else if (name == "laycueDanger")
		kind = ActionButton::Kind::Danger;
	else if (name == "laycueLink")
		kind = ActionButton::Kind::Link;
	return new ActionButton(text, kind, parent);
}

inline void clear(QLayout *layout)
{
	while (QLayoutItem *item = layout->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		if (item->layout())
			clear(item->layout());
		delete item;
	}
}

} // namespace laycue::ui::page
