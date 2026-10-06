#include "update/updater.hpp"

#include "core/update-info.hpp"

#include <obs-module.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSslSocket>
#include <QStandardPaths>

#include <functional>
#include <memory>
#include <vector>

namespace laycue::update {

namespace {

// The public repository the installers are published to (the source repository stays private).
constexpr const char *kRepo = "wotgcls/laycue-releases";
constexpr int kMaxRedirects = 5;
constexpr int kTimeoutMs = 20000;

Platform thisPlatform()
{
#if defined(_WIN32)
	return Platform::Windows;
#elif defined(__APPLE__)
	return Platform::MacOS;
#else
	return Platform::Linux;
#endif
}

QString updateFolder()
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
		.filePath(QStringLiteral("laycue-update"));
}

} // namespace

Updater::Updater(const QString &currentVersion, QObject *parent) : QObject(parent), current_(currentVersion)
{
	// Tests (and only tests) can point the check at a local server.
	const QByteArray override = qgetenv("LAYCUE_UPDATE_API");
	if (!override.isEmpty()) {
		apiUrl_ = QUrl(QString::fromUtf8(override));
		allowLocal_ = true;
	} else {
		apiUrl_ = QUrl(
			QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(kRepo)));
	}
}

Updater::~Updater()
{
	++generation_;
	stopReply();
}

bool Updater::canInstallOnExit()
{
#if defined(_WIN32)
	return true;
#else
	return false;
#endif
}

bool Updater::allowed(const QUrl &url) const
{
	return isAllowedDownloadUrl(url.toString().toStdString(), allowLocal_);
}

void Updater::setState(State state)
{
	state_ = state;
	emit changed();
}

void Updater::fail(Error error, const QString &detail)
{
	error_ = error;
	detail_ = detail;
	if (out_.isOpen())
		out_.close();
	if (!file_.isEmpty() && error != Error::None) { // a half-written or damaged download is never kept
		QFile::remove(file_);
		file_.clear();
	}
	blog(LOG_WARNING, "[laycue-update] %d %s", int(error), detail.toUtf8().constData());
	setState(State::Failed);
}

void Updater::stopReply()
{
	if (reply_) {
		reply_->disconnect(this);
		reply_->abort();
		reply_->deleteLater();
		reply_.clear();
	}
}

// One request; redirects are followed by hand so every address can be checked against the allowed hosts.
QNetworkReply *Updater::get(const QUrl &url, int hops)
{
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Laycue-Updater"));
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setTransferTimeout(kTimeoutMs * 6);
	(void)hops;
	return manager_.get(request);
}

void Updater::check()
{
	if (state_ == State::Checking || state_ == State::Downloading)
		return;
	++generation_;
	stopReply();
	error_ = Error::None;
	detail_.clear();
	scheduled_ = false;
	if (apiUrl_.scheme() == QLatin1String("https") && !QSslSocket::supportsSsl()) {
		fail(Error::NoSecure, QStringLiteral("no TLS backend"));
		return;
	}
	setState(State::Checking);
	const quint64 gen = generation_;
	reply_ = get(apiUrl_);
	QNetworkReply *reply = reply_;
	connect(reply, &QNetworkReply::finished, this, [this, reply, gen]() {
		reply->deleteLater();
		if (gen != generation_)
			return;
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (status == 404) {
			fail(Error::NoReleases);
			return;
		}
		if (reply->error() != QNetworkReply::NoError || status != 200) {
			fail(Error::NoConnection, reply->errorString());
			return;
		}
		readRelease(reply->readAll());
	});
}

void Updater::readRelease(const QByteArray &json)
{
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
	if (!doc.isObject()) {
		fail(Error::BadAnswer, parseError.errorString());
		return;
	}
	const QJsonObject release = doc.object();
	latest_ = release.value(QStringLiteral("tag_name")).toString();
	notes_ = release.value(QStringLiteral("body")).toString().trimmed();
	if (notes_.size() > 1200)
		notes_ = notes_.left(1200) + QStringLiteral("…");
	pageUrl_ = release.value(QStringLiteral("html_url")).toString();
	const Version latest = parseVersion(latest_.toStdString());
	const Version current = parseVersion(current_.toStdString());
	if (!latest.valid) {
		fail(Error::BadAnswer, QStringLiteral("tag %1").arg(latest_));
		return;
	}
	if (!isNewer(latest, current)) {
		setState(State::UpToDate);
		return;
	}
	std::vector<std::string> names;
	QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
	for (const QJsonValue a : assets)
		names.push_back(a.toObject().value(QStringLiteral("name")).toString().toStdString());
	const std::string installer = pickInstaller(names, thisPlatform());
	installerUrl_.clear();
	checksumUrl_.clear();
	installerName_.clear();
	for (const QJsonValue a : assets) {
		const QJsonObject o = a.toObject();
		const QString name = o.value(QStringLiteral("name")).toString();
		const QUrl url(o.value(QStringLiteral("browser_download_url")).toString());
		if (!installer.empty() && name.toStdString() == installer) {
			installerName_ = name;
			installerUrl_ = url;
		} else if (!installer.empty() && name.toStdString() == installer + ".sha256") {
			checksumUrl_ = url;
		}
	}
	setState(State::Available);
}

void Updater::download()
{
	if (state_ != State::Available && state_ != State::Failed)
		return;
	if (installerUrl_.isEmpty()) {
		fail(Error::NoInstaller);
		return;
	}
	if (checksumUrl_.isEmpty()) {
		fail(Error::NoChecksum);
		return;
	}
	if (!allowed(installerUrl_) || !allowed(checksumUrl_)) {
		fail(Error::Blocked, installerUrl_.host());
		return;
	}
	++generation_;
	stopReply();
	error_ = Error::None;
	detail_.clear();
	progress_ = 0.0;
	setState(State::Downloading);
	fetchChecksum();
}

void Updater::fetchChecksum()
{
	const quint64 gen = generation_;
	auto step = std::make_shared<std::function<void(QUrl, int)>>();
	*step = [this, gen, step](QUrl url, int hops) {
		stopReply();
		reply_ = get(url);
		QNetworkReply *reply = reply_;
		connect(reply, &QNetworkReply::finished, this, [this, reply, gen, step, hops]() {
			reply->deleteLater();
			if (gen != generation_)
				return;
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status >= 300 && status < 400 && hops < kMaxRedirects) {
				const QUrl next =
					reply->url().resolved(reply->header(QNetworkRequest::LocationHeader).toUrl());
				if (!allowed(next)) {
					fail(Error::Blocked, next.host());
					return;
				}
				(*step)(next, hops + 1);
				return;
			}
			if (reply->error() != QNetworkReply::NoError || status != 200) {
				fail(Error::NoConnection, reply->errorString());
				return;
			}
			expectedHash_ = QString::fromStdString(expectedSha256(
				QString::fromUtf8(reply->readAll()).toStdString(), installerName_.toStdString()));
			if (expectedHash_.isEmpty()) {
				fail(Error::NoChecksum);
				return;
			}
			fetchInstaller();
		});
	};
	(*step)(checksumUrl_, 0);
}

void Updater::fetchInstaller()
{
	const quint64 gen = generation_;
	QDir dir(updateFolder());
	dir.removeRecursively(); // older downloads are not kept
	if (!QDir().mkpath(dir.absolutePath())) {
		fail(Error::Disk, dir.absolutePath());
		return;
	}
	file_ = dir.filePath(QString::fromStdString(safeFileName(installerName_.toStdString())));
	out_.setFileName(file_);
	if (!out_.open(QIODevice::WriteOnly)) {
		fail(Error::Disk, out_.errorString());
		return;
	}
	hash_.reset();

	auto step = std::make_shared<std::function<void(QUrl, int)>>();
	*step = [this, gen, step](QUrl url, int hops) {
		stopReply();
		reply_ = get(url);
		QNetworkReply *reply = reply_;
		connect(reply, &QNetworkReply::readyRead, this, [this, reply, gen]() {
			if (gen != generation_)
				return;
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status != 200)
				return; // a redirect's body is not the file
			const QByteArray chunk = reply->readAll();
			if (out_.write(chunk) != chunk.size()) {
				fail(Error::Disk, out_.errorString());
				return;
			}
			hash_.addData(chunk);
		});
		connect(reply, &QNetworkReply::downloadProgress, this, [this, gen](qint64 got, qint64 total) {
			if (gen != generation_ || total <= 0)
				return;
			progress_ = double(got) / double(total);
			emit changed();
		});
		connect(reply, &QNetworkReply::finished, this, [this, reply, gen, step, hops]() {
			reply->deleteLater();
			if (gen != generation_)
				return;
			const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (status >= 300 && status < 400 && hops < kMaxRedirects) {
				const QUrl next =
					reply->url().resolved(reply->header(QNetworkRequest::LocationHeader).toUrl());
				if (!allowed(next)) {
					fail(Error::Blocked, next.host());
					return;
				}
				(*step)(next, hops + 1);
				return;
			}
			if (reply->error() != QNetworkReply::NoError || status != 200) {
				fail(Error::NoConnection, reply->errorString());
				return;
			}
			const QByteArray rest = reply->readAll();
			if (!rest.isEmpty()) {
				out_.write(rest);
				hash_.addData(rest);
			}
			out_.close();
			const QString actual = QString::fromLatin1(hash_.result().toHex());
			if (actual.compare(expectedHash_, Qt::CaseInsensitive) != 0) {
				fail(Error::ChecksumMismatch, actual);
				return;
			}
			progress_ = 1.0;
			setState(State::Ready);
		});
	};
	(*step)(installerUrl_, 0);
}

bool Updater::installOnExit()
{
#if defined(_WIN32)
	if (state_ != State::Ready || file_.isEmpty() || scheduled_)
		return false;
	// A small hidden helper waits until this OBS has closed, then runs the installer quietly. It asks Windows for
	// permission (the usual "do you want to allow this app to make changes" box) as the installer starts.
	auto quote = [](QString s) {
		return QStringLiteral("'") + s.replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\'');
	};
	const QString script =
		QStringLiteral("Wait-Process -Id %1 -ErrorAction SilentlyContinue; Start-Sleep -Seconds 2; "
			       "Start-Process -FilePath %2 -ArgumentList '/SILENT','/SUPPRESSMSGBOXES','/NORESTART'")
			.arg(QCoreApplication::applicationPid())
			.arg(quote(QDir::toNativeSeparators(file_)));
	const bool started = QProcess::startDetached(QStringLiteral("powershell.exe"),
						     {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
						      QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
						      QStringLiteral("Hidden"), QStringLiteral("-Command"), script});
	if (!started) {
		fail(Error::Disk, QStringLiteral("could not start the installer helper"));
		return false;
	}
	scheduled_ = true;
	emit changed();
	return true;
#else
	return false;
#endif
}

void Updater::showFile()
{
	if (!file_.isEmpty())
		QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file_).absolutePath()));
}

void Updater::openReleasePage()
{
	QString url = pageUrl_;
	if (url.isEmpty() || !allowed(QUrl(url)))
		url = QStringLiteral("https://github.com/%1/releases/latest").arg(QLatin1String(kRepo));
	QDesktopServices::openUrl(QUrl(url));
}

} // namespace laycue::update
