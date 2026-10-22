#include "ui/update-card.hpp"

#include "ui/pages/helpers.hpp"
#include "ui/text.hpp"
#include "ui/tokens.hpp"
#include "update/updater.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace laycue::ui {

using page::innerLabel;
using update::Updater;

// A thin progress line in the panel's own style.
class UpdateBar : public QWidget {
public:
	explicit UpdateBar(QWidget *parent) : QWidget(parent) { setFixedHeight(6); }
	void setValue(double v)
	{
		value_ = std::clamp(v, 0.0, 1.0);
		update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		p.setPen(Qt::NoPen);
		p.setBrush(color::recess);
		p.drawRoundedRect(rect(), 3, 3);
		p.setBrush(color::setup);
		p.drawRoundedRect(QRectF(0, 0, width() * value_, height()), 3, 3);
	}

private:
	double value_ = 0.0;
};

namespace {

QString errorText(const Updater &u)
{
	switch (u.error()) {
	case Updater::Error::NoConnection:
		return lt("Laycue.Update.ErrNoConnection");
	case Updater::Error::NoSecure:
		return lt("Laycue.Update.ErrNoSecure");
	case Updater::Error::NoReleases:
		return lt("Laycue.Update.ErrNoReleases");
	case Updater::Error::BadAnswer:
		return lt("Laycue.Update.ErrBadAnswer");
	case Updater::Error::NoInstaller:
		return lt("Laycue.Update.ErrNoInstaller");
	case Updater::Error::NoChecksum:
		return lt("Laycue.Update.ErrNoChecksum");
	case Updater::Error::ChecksumMismatch:
		return lt("Laycue.Update.ErrChecksum");
	case Updater::Error::Disk:
		return lt("Laycue.Update.ErrDisk");
	case Updater::Error::Blocked:
		return lt("Laycue.Update.ErrBlocked");
	case Updater::Error::None:
		break;
	}
	return QString();
}

} // namespace

UpdateCard::UpdateCard(Updater &updater, QWidget *parent) : Surface(1, parent), updater_(updater)
{
	auto *v = new QVBoxLayout(this);
	v->setSpacing(size::gapSmall);
	setPadding(14, 10, 14, 12);

	status_ = innerLabel(this, QString(), 13, 600, color::cream);
	v->addWidget(status_);
	notes_ = innerLabel(this, QString(), 11, 400, color::textSecondary);
	v->addWidget(notes_);
	bar_ = new UpdateBar(this);
	v->addWidget(bar_);

	auto *row = new QHBoxLayout();
	row->setSpacing(size::gapSmall);
	primary_ = new ActionButton(QString(), ActionButton::Kind::Primary, this);
	connect(primary_, &QAbstractButton::clicked, this, &UpdateCard::primaryClicked);
	row->addWidget(primary_, 2);
	secondary_ = new ActionButton(QString(), ActionButton::Kind::Secondary, this);
	connect(secondary_, &QAbstractButton::clicked, this, [this]() {
		if (updater_.state() == Updater::State::Ready)
			updater_.showFile();
		else
			updater_.openReleasePage();
	});
	row->addWidget(secondary_, 1);
	v->addLayout(row);

	v->addWidget(innerLabel(this, lt("Laycue.Update.Privacy"), 11, 400, color::textTertiary));

	connect(&updater_, &Updater::changed, this, &UpdateCard::refresh);
	refresh();
}

void UpdateCard::primaryClicked()
{
	switch (updater_.state()) {
	case Updater::State::Available:
		if (updater_.hasInstaller())
			updater_.download();
		else
			updater_.openReleasePage();
		break;
	case Updater::State::Ready:
		if (Updater::canInstallOnExit())
			updater_.installOnExit();
		else
			updater_.showFile();
		break;
	case Updater::State::Checking:
	case Updater::State::Downloading:
		break;
	default:
		updater_.check();
		break;
	}
}

void UpdateCard::refresh()
{
	const Updater &u = updater_;
	QString status, notes, primary, secondary;
	bool primaryEnabled = true, showBar = false;
	QColor statusColour = color::cream;
	switch (u.state()) {
	case Updater::State::Idle:
		status = lt("Laycue.Update.Installed").arg(u.currentVersion());
		primary = lt("Laycue.Update.Check");
		break;
	case Updater::State::Checking:
		status = lt("Laycue.Update.Checking");
		primary = lt("Laycue.Update.Check");
		primaryEnabled = false;
		break;
	case Updater::State::UpToDate:
		status = lt("Laycue.Update.UpToDate").arg(u.currentVersion());
		statusColour = color::next;
		primary = lt("Laycue.Update.CheckAgain");
		break;
	case Updater::State::Available:
		status = lt("Laycue.Update.Available").arg(u.latestVersion(), u.currentVersion());
		statusColour = color::overlay;
		notes = u.notes();
		primary = u.hasInstaller() ? lt("Laycue.Update.Download") : lt("Laycue.Update.OpenPage");
		secondary = lt("Laycue.Update.WhatsNew");
		break;
	case Updater::State::Downloading:
		status = lt("Laycue.Update.Downloading").arg(int(u.progress() * 100.0));
		primary = lt("Laycue.Update.Download");
		primaryEnabled = false;
		showBar = true;
		break;
	case Updater::State::Ready:
		showBar = true;
		if (!Updater::canInstallOnExit()) {
			status = lt("Laycue.Update.ReadyManual");
			primary = lt("Laycue.Update.ShowFile");
		} else if (u.installScheduled()) {
			status = lt("Laycue.Update.Scheduled");
			statusColour = color::next;
			primary = lt("Laycue.Update.InstallOnExit");
			primaryEnabled = false;
		} else {
			status = lt("Laycue.Update.Ready");
			statusColour = color::next;
			primary = lt("Laycue.Update.InstallOnExit");
			secondary = lt("Laycue.Update.ShowFile");
		}
		break;
	case Updater::State::Failed:
		status = errorText(u);
		statusColour = color::overlay;
		primary = lt("Laycue.Update.TryAgain");
		secondary = lt("Laycue.Update.OpenPage");
		break;
	}
	status_->setText(status);
	status_->setStyleSheet(labelStyle(statusColour, uiFont(13, 600)));
	notes_->setText(notes);
	notes_->setVisible(!notes.isEmpty());
	bar_->setValue(u.state() == Updater::State::Ready ? 1.0 : u.progress());
	bar_->setVisible(showBar);
	primary_->setText(primary);
	primary_->setEnabled(primaryEnabled);
	secondary_->setText(secondary);
	secondary_->setVisible(!secondary.isEmpty());
}

} // namespace laycue::ui
