// "Check for updates": looks at the newest release in the pack's public releases repository, downloads its
// installer, checks the checksum, and (on Windows) runs the installer by itself once OBS has been closed.
//
// It only talks to the network when the operator presses a button, and it never touches a running OBS:
// nothing is installed while OBS is open (plugin files are in use then, and a stream must never be cut).
#pragma once

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <QCryptographicHash>
#include <QNetworkAccessManager>

class QNetworkReply;

namespace laycue::update {

class Updater : public QObject {
	Q_OBJECT

public:
	enum class State { Idle, Checking, UpToDate, Available, Downloading, Ready, Failed };
	enum class Error {
		None,
		NoConnection,     // the network did not answer
		NoSecure,         // this OBS cannot make https connections
		NoReleases,       // nothing has been published yet
		BadAnswer,        // GitHub answered with something unreadable
		NoInstaller,      // the release has no installer for this computer
		NoChecksum,       // ...or no checksum file for it
		ChecksumMismatch, // the download is damaged: it is deleted, never run
		Disk,             // the file could not be saved
		Blocked           // an address outside GitHub was refused
	};
	Q_ENUM(State)
	Q_ENUM(Error)

	explicit Updater(const QString &currentVersion, QObject *parent = nullptr);
	~Updater() override;

	State state() const { return state_; }
	Error error() const { return error_; }
	QString detail() const { return detail_; }
	QString currentVersion() const { return current_; }
	QString latestVersion() const { return latest_; }
	QString notes() const { return notes_; }
	double progress() const { return progress_; }
	QString downloadedFile() const { return file_; }
	bool hasInstaller() const { return !installerUrl_.isEmpty(); }
	// Windows can run the installer by itself when OBS closes; elsewhere the operator opens the file.
	static bool canInstallOnExit();
	bool installScheduled() const { return scheduled_; }

public slots:
	void check();
	void download();
	bool installOnExit();
	void showFile();
	void openReleasePage();

signals:
	void changed();

private:
	void fail(Error error, const QString &detail = QString());
	void setState(State state);
	QNetworkReply *get(const QUrl &url, int hops = 0);
	void readRelease(const QByteArray &json);
	void fetchChecksum();
	void fetchInstaller();
	void stopReply();
	bool allowed(const QUrl &url) const;

	QNetworkAccessManager manager_;
	QPointer<QNetworkReply> reply_;
	State state_ = State::Idle;
	Error error_ = Error::None;
	QString detail_;
	QString current_;
	QString latest_;
	QString notes_;
	QString pageUrl_;
	QUrl installerUrl_;
	QUrl checksumUrl_;
	QString installerName_;
	QString expectedHash_;
	QString file_;
	double progress_ = 0.0;
	bool scheduled_ = false;
	bool allowLocal_ = false; // only when a test points the check at a local server
	QUrl apiUrl_;
	QFile out_;
	QCryptographicHash hash_{QCryptographicHash::Sha256};
	quint64 generation_ = 0; // makes answers from an earlier, cancelled request harmless
};

} // namespace laycue::update
