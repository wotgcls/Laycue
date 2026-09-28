#include "ui/style.hpp"

#include "ui/tokens.hpp"

#include <QStyle>
#include <QWidget>

namespace laycue::ui {

QString panelStyleSheet()
{
	QString css = QStringLiteral(R"(
#LaycueContent { background: @ink; }
#LaycueContent QScrollArea, #LaycueContent QScrollArea > QWidget > QWidget { background: transparent; border: none; }
#LaycueContent QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
#LaycueContent QScrollBar::handle:vertical { background: @control; border-radius: 4px; min-height: 24px; }
#LaycueContent QScrollBar::add-line, #LaycueContent QScrollBar::sub-line { height: 0; }
#LaycueContent QLabel { background: transparent; }
#LaycueContent QPushButton { margin: 0; } /* OBS's own theme adds margins to push buttons */

#LaycueContent QPushButton[role="control"] { border-radius: @radiusPx; padding: 0 14px; min-height: @controlPx; font-size: 13px; font-weight: 600; }
#LaycueContent[large="true"] QPushButton[role="control"] { min-height: @touchPx; }
#LaycueContent QPushButton:focus { border: 2px solid @setup; }

#LaycueContent QPushButton#laycuePrimary { background: @cream; color: @ink; border: none; }
#LaycueContent QPushButton#laycuePrimary:hover { background: #FBF6EA; }
#LaycueContent QPushButton#laycuePrimary:pressed { background: #D8D2C4; }
#LaycueContent QPushButton#laycuePrimary:disabled { background: @control; color: @textDisabled; }

#LaycueContent QPushButton#laycueSecondary { background: @control; color: @cream; border: 1px solid rgba(255,255,255,26); }
#LaycueContent QPushButton#laycueSecondary:hover { background: #363430; }
#LaycueContent QPushButton#laycueSecondary:pressed { background: #23211F; }
#LaycueContent QPushButton#laycueSecondary:disabled { color: @textDisabled; background: @graphite; border-color: rgba(255,255,255,10); }

#LaycueContent QPushButton#laycueDanger { background: @control; color: @live; border: 1px solid rgba(255,255,255,26); }
#LaycueContent QPushButton#laycueDanger:hover { background: #363430; }

#LaycueContent QPushButton#laycueLink { background: transparent; color: @setup; border: none; border-radius: @radiusPx; padding: 0 8px; min-height: @controlPx; font-size: 13px; font-weight: 600; }
#LaycueContent[large="true"] QPushButton#laycueLink { min-height: @touchPx; }
#LaycueContent QPushButton#laycueLink:hover { background: rgba(46,139,255,30); }
#LaycueContent QPushButton#laycueLink:disabled { color: @textDisabled; }

#LaycueContent QPushButton#laycueGoLive { background: @live; border: none; border-radius: @radiusPx; }
#LaycueContent QPushButton#laycueGoLive:hover { background: @livePressed; }
#LaycueContent QPushButton#laycueGoLive:pressed { background: @livePressed; }
#LaycueContent QPushButton#laycueGoLive:disabled { background: @control; }
#LaycueContent QPushButton#laycueCut { background: @control; border: 1px solid rgba(255,255,255,26); border-radius: @radiusPx; }
#LaycueContent QPushButton#laycueCut:hover { background: #363430; }
#LaycueContent QPushButton#laycueCut:pressed { background: #23211F; }
#LaycueContent QPushButton#laycueCut:disabled { background: @graphite; border-color: rgba(255,255,255,10); }
#LaycueContent QPushButton#laycueGoLive:focus, #LaycueContent QPushButton#laycueCut:focus { border: 2px solid @setup; }

#LaycueContent QPushButton#laycueChip { background: transparent; color: @textSecondary; border: 1px solid rgba(255,255,255,30); border-radius: 13px; padding: 0 12px; min-height: 26px; font-size: 12px; font-weight: 600; }
#LaycueContent QPushButton#laycueChip:hover { background: rgba(255,255,255,12); }
#LaycueContent QPushButton#laycueChip:checked { background: rgba(255,179,26,36); color: @cream; border: 1px solid @overlay; }

#LaycueContent QPushButton#laycueFeed { background: @control; color: @cream; border: 1px solid rgba(255,255,255,20); border-radius: @radiusPx; padding: 0 8px; font-size: 12px; font-weight: 600; }
#LaycueContent QPushButton#laycueFeed:hover { background: #363430; }
#LaycueContent QPushButton#laycueFeed:checked { background: #2A1714; border: 1.5px solid @live; }

#LaycueContent QLineEdit { background: @recess; color: @cream; border: 1px solid rgba(255,255,255,30); border-radius: @radiusPx; padding: 0 10px; min-height: @controlPx; font-size: 13px; selection-background-color: @setup; }
#LaycueContent[large="true"] QLineEdit { min-height: @touchPx; }
#LaycueContent QLineEdit:focus { border: 2px solid @setup; }

QMenu { background: @stage; color: @cream; border: 1px solid rgba(255,255,255,30); border-radius: 10px; padding: 4px; font-size: 13px; }
QMenu::item { padding: 7px 22px 7px 14px; border-radius: 6px; }
QMenu::item:selected { background: @control; }
QMenu::separator { height: 1px; background: rgba(255,255,255,20); margin: 4px 8px; }
QToolTip { background: @stage; color: @cream; border: 1px solid rgba(255,255,255,30); padding: 5px 8px; font-size: 12px; }
)");

	const struct {
		const char *token;
		QString value;
	} tokens[] = {
		{"@radiusPx", QString::number(size::radius) + "px"},
		{"@controlPx", QString::number(size::controlHeight) + "px"},
		{"@touchPx", QString::number(size::touch) + "px"},
		{"@textDisabled", hex(color::textDisabled)},
		{"@textSecondary", hex(color::textSecondary)},
		{"@overlay", hex(color::overlay)},
		{"@livePressed", hex(color::livePressed)},
		{"@graphite", hex(color::graphite)},
		{"@control", hex(color::control)},
		{"@recess", hex(color::recess)},
		{"@stage", hex(color::stage)},
		{"@cream", hex(color::cream)},
		{"@setup", hex(color::setup)},
		{"@live", hex(color::live)},
		{"@ink", hex(color::ink)},
	};
	for (const auto &t : tokens)
		css.replace(QString::fromLatin1(t.token), t.value);

	if (!uiFamily().isEmpty())
		css += QStringLiteral(
			       "#LaycueContent QLabel, #LaycueContent QPushButton, #LaycueContent QLineEdit { font-family: \"%1\"; }")
			       .arg(uiFamily());
	return css;
}

void setLargeControls(QWidget *content, bool large)
{
	if (!content)
		return;
	content->setProperty("large", large);
	content->style()->unpolish(content);
	content->style()->polish(content);
	for (QWidget *child : content->findChildren<QWidget *>()) {
		child->setProperty("large", large); // custom controls (Segmented) read it directly
		child->style()->unpolish(child);
		child->style()->polish(child);
		child->updateGeometry();
		child->update();
	}
}

} // namespace laycue::ui
