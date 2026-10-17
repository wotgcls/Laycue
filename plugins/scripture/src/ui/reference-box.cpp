#include "ui/reference-box.hpp"

#include "core/books.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"

#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QValidator>

#include <algorithm>

namespace laycue::scripture {

using namespace laycue::ui;

namespace {

QLineEdit *part(QWidget *parent, const QString &placeholder, int px)
{
	auto *edit = new QLineEdit(parent);
	edit->setPlaceholderText(placeholder);
	edit->setAccessibleName(placeholder);
	edit->setFrame(false);
	QFont f = edit->font();
	f.setPixelSize(px);
	f.setWeight(QFont::DemiBold);
	edit->setFont(f);
	edit->setStyleSheet(
		QStringLiteral("QLineEdit { background: transparent; color: %1; border: none; padding: 0 6px; }")
			.arg(hex(color::cream)));
	edit->setMinimumHeight(size::touch - 6);
	return edit;
}

// The book part only takes letters that can still become a book: "gen" goes in, "genb" does not. Short forms
// count too ("jn", "1cor"), so anything the Space key would accept is also something that can be typed.
class BookValidator : public QValidator {
public:
	explicit BookValidator(QObject *parent) : QValidator(parent) {}
	State validate(QString &input, int &) const override
	{
		if (input.trimmed().isEmpty())
			return Acceptable;
		return suggestBooks(input.toStdString(), 1).empty() ? Invalid : Acceptable;
	}
};

QWidget *divider(QWidget *parent)
{
	auto *line = new QWidget(parent);
	line->setFixedWidth(1);
	line->setAttribute(Qt::WA_StyledBackground, true);
	line->setStyleSheet(QStringLiteral("background: %1;").arg(hex(color::lineStrong)));
	return line;
}

} // namespace

ReferenceBox::ReferenceBox(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(size::touch + 4);
	auto *h = new QHBoxLayout(this);
	h->setContentsMargins(6, 3, 6, 3);
	h->setSpacing(0);

	book_ = part(this, lt("Laycue.Scripture.Book"), 18);
	chapter_ = part(this, lt("Laycue.Scripture.Chapter"), 18);
	verse_ = part(this, lt("Laycue.Scripture.Verse"), 18);
	book_->setValidator(new BookValidator(book_));
	chapter_->setValidator(
		new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,3}")), chapter_));
	verse_->setValidator(
		new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,3}(-\\d{0,3})?")), verse_));
	chapter_->setAlignment(Qt::AlignCenter);
	verse_->setAlignment(Qt::AlignCenter);
	chapter_->setFixedWidth(48); // three digits and a little air
	verse_->setFixedWidth(84);   // "16-18" and "100-120" fit

	// The book box is always wide enough for the longest book name, so the whole name stays in view.
	int widest = 0;
	const QFontMetrics fm(book_->font());
	for (const BookInfo &info : books())
		widest = std::max(widest, fm.horizontalAdvance(QString::fromStdString(info.name)));
	auto *bookArea = new QWidget(this);
	bookArea->setMinimumWidth(widest + 24);
	auto *b = new QHBoxLayout(bookArea);
	b->setContentsMargins(0, 0, 0, 0);
	b->setSpacing(0);
	b->addWidget(book_, 1);
	hint_ = new QLabel(bookArea);
	hint_->setStyleSheet(labelStyle(color::textTertiary, uiFont(12, 600)) + QStringLiteral(" padding-right: 6px;"));
	hint_->hide();
	b->addWidget(hint_);

	h->addWidget(bookArea, 1);
	h->addWidget(divider(this));
	h->addWidget(chapter_);
	auto *colon = new QLabel(QStringLiteral(":"), this); // chapter:verse, as in "John 3:16"
	colon->setAlignment(Qt::AlignCenter);
	colon->setFixedWidth(12);
	colon->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: 20px; font-weight: 600;")
				     .arg(hex(color::textTertiary)));
	h->addWidget(colon);
	h->addWidget(verse_);

	for (QLineEdit *edit : {book_, chapter_, verse_}) {
		edit->installEventFilter(this);
		connect(edit, &QLineEdit::returnPressed, this, &ReferenceBox::submitted);
		connect(edit, &QLineEdit::textEdited, this, [this]() { emit edited(); });
	}
	connect(book_, &QLineEdit::textEdited, this, &ReferenceBox::bookEdited);
}

void ReferenceBox::paintEvent(QPaintEvent *)
{
	// One box around the three parts; the border lights up while typing in any of them.
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	const bool focused = book_->hasFocus() || chapter_->hasFocus() || verse_->hasFocus();
	QPainterPath box;
	box.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), size::radius, size::radius);
	p.fillPath(box, color::recess);
	p.setPen(QPen(focused ? color::setup : color::lineStrong, focused ? 2 : 1));
	p.drawPath(box);
}

QString ReferenceBox::text() const
{
	QString out = book_->text().trimmed();
	if (!chapter_->text().isEmpty())
		out += QLatin1Char(' ') + chapter_->text();
	if (!chapter_->text().isEmpty() && !verse_->text().isEmpty())
		out += QLatin1Char(':') + verse_->text();
	return out;
}

void ReferenceBox::setReference(const Reference &ref)
{
	const BookInfo *book = bookByNumber(ref.book);
	filling_ = true;
	book_->setText(book ? QString::fromStdString(book->name) : QString());
	chapter_->setText(ref.chapter > 0 ? QString::number(ref.chapter) : QString());
	QString verse = ref.verseFrom > 0 ? QString::number(ref.verseFrom) : QString();
	if (ref.verseTo > ref.verseFrom)
		verse += QStringLiteral("-%1").arg(ref.verseTo);
	verse_->setText(verse);
	hint_->hide();
	suggestion_.clear();
	lastBookLength_ = book_->text().length();
	book_->setCursorPosition(0); // show the start of the name
	filling_ = false;
}

void ReferenceBox::setText(const QString &typed)
{
	Reference ref;
	if (parseReference(typed.toStdString(), ref))
		setReference(ref);
}

void ReferenceBox::focusBook()
{
	moveTo(book_);
}

void ReferenceBox::moveTo(QLineEdit *edit)
{
	edit->setFocus();
	QTimer::singleShot(0, edit, [edit]() { edit->selectAll(); });
	update();
}

void ReferenceBox::bookEdited(const QString &typed)
{
	if (filling_)
		return;
	const bool deleting = typed.length() < lastBookLength_;
	lastBookLength_ = typed.length();
	const std::string text = typed.toStdString();
	const QString hint = QString::fromStdString(bookHint(text));
	suggestion_ = hint;
	const QString inlineName = QString::fromStdString(inlineBookCompletion(text));
	if (!deleting && !inlineName.isEmpty()) {
		// Fill the rest of the name in and select it: keep typing to replace it, Space to take it.
		filling_ = true;
		book_->setText(inlineName);
		book_->setSelection(int(typed.length()), int(inlineName.length() - typed.length()));
		filling_ = false;
		hint_->hide();
		return;
	}
	hint_->setText(hint.isEmpty() || hint.compare(typed.trimmed(), Qt::CaseInsensitive) == 0
			       ? QString()
			       : QStringLiteral("→ %1").arg(hint));
	hint_->setVisible(!hint_->text().isEmpty());
}

void ReferenceBox::acceptBook()
{
	if (!suggestion_.isEmpty()) {
		filling_ = true;
		book_->setText(suggestion_);
		filling_ = false;
	}
	hint_->hide();
	lastBookLength_ = book_->text().length();
	moveTo(chapter_);
	emit edited();
}

bool ReferenceBox::eventFilter(QObject *watched, QEvent *event)
{
	auto *edit = qobject_cast<QLineEdit *>(watched);
	if (!edit)
		return QWidget::eventFilter(watched, event);
	if (event->type() == QEvent::FocusIn) {
		// Clicking a part selects all of it, so a new chapter or verse can be typed straight over it.
		QTimer::singleShot(0, edit, [edit]() { edit->selectAll(); });
		update();
	} else if (event->type() == QEvent::FocusOut) {
		if (edit == book_)
			book_->setCursorPosition(0);
		update();
	}
	if (event->type() != QEvent::KeyPress)
		return QWidget::eventFilter(watched, event);
	auto *key = static_cast<QKeyEvent *>(event);
	const QString typed = key->text();
	if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
		emit stepped(key->key() == Qt::Key_Down ? 1 : -1);
		return true;
	}
	if (edit == book_) {
		const QString book = book_->text();
		bool letters = false;
		for (const QChar c : book)
			letters = letters || c.isLetter();
		// Space, Tab or ":" takes the book (a lone "1 " stays: it starts 1 John, 1 Kings...).
		if ((key->key() == Qt::Key_Space || key->key() == Qt::Key_Tab || typed == QStringLiteral(":")) &&
		    letters) {
			acceptBook();
			return true;
		}
		// A number typed straight after the book starts the chapter ("john3").
		if (letters && typed.size() == 1 && typed[0].isDigit()) {
			acceptBook();
			chapter_->setText(typed);
			return true;
		}
		return false;
	}
	if (edit == chapter_) {
		if (key->key() == Qt::Key_Space || key->key() == Qt::Key_Tab || typed == QStringLiteral(":") ||
		    typed == QStringLiteral(".")) {
			moveTo(verse_);
			return true;
		}
		if (key->key() == Qt::Key_Backspace && chapter_->text().isEmpty()) {
			book_->setFocus();
			return true;
		}
		return false;
	}
	if (edit == verse_ && key->key() == Qt::Key_Backspace && verse_->text().isEmpty()) {
		chapter_->setFocus();
		return true;
	}
	if (edit == verse_ && key->key() == Qt::Key_Backtab) {
		moveTo(chapter_);
		return true;
	}
	return QWidget::eventFilter(watched, event);
}

} // namespace laycue::scripture
