#include "store/online-search.hpp"

#include "net/http.hpp"

#include <plugin-support.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace laycue::lyrics {

namespace {

constexpr int kTimeoutMs = 15000;

std::vector<OnlineSong> parseAnswer(const QByteArray &json)
{
	std::vector<OnlineSong> out;
	const QJsonDocument doc = QJsonDocument::fromJson(json);
	if (!doc.isArray())
		return out;
	for (const QJsonValue value : doc.array()) {
		const QJsonObject o = value.toObject();
		OnlineSong s;
		s.id = o.value(QStringLiteral("id")).toVariant().toLongLong();
		s.title = o.value(QStringLiteral("trackName")).toString().toStdString();
		if (s.title.empty())
			s.title = o.value(QStringLiteral("name")).toString().toStdString();
		s.artist = o.value(QStringLiteral("artistName")).toString().toStdString();
		s.album = o.value(QStringLiteral("albumName")).toString().toStdString();
		s.durationSeconds = int(o.value(QStringLiteral("duration")).toDouble());
		s.instrumental = o.value(QStringLiteral("instrumental")).toBool();
		s.plainLyrics = o.value(QStringLiteral("plainLyrics")).toString().toStdString();
		s.syncedLyrics = o.value(QStringLiteral("syncedLyrics")).toString().toStdString();
		out.push_back(s);
	}
	return out;
}

} // namespace

OnlineSearch::OnlineSearch(QObject *parent) : QObject(parent), http_(new net::Http(this)) {}

void OnlineSearch::search(const QString &query)
{
	if (reply_) {
		reply_->abort();
		reply_->deleteLater();
	}
	query_ = query;
	if (!net::Http::secureAvailable()) {
		emit failed(query, QStringLiteral("secure"));
		return;
	}
	const QUrl url(QString::fromStdString(onlineSearchUrl(query.toStdString())));
	const net::Reply::Headers headers = {
		{"User-Agent", QByteArray("Laycue Lyrics ") + PLUGIN_VERSION + " (https://github.com/wotgcls/Laycue)"},
		{"Accept", "application/json"},
	};
	net::Reply *reply = http_->get(url, headers, kTimeoutMs);
	reply_ = reply;
	buffer_.clear();
	connect(reply, &net::Reply::readyRead, this, [this, reply]() {
		if (reply == reply_)
			buffer_.append(reply->readAll());
	});
	connect(reply, &net::Reply::finished, this, [this, reply, query]() {
		if (reply != reply_) { // a newer search took over
			reply->deleteLater();
			return;
		}
		buffer_.append(reply->readAll());
		const QByteArray answer = buffer_;
		buffer_.clear();
		const bool ok = reply->ok() && reply->status() == 200;
		const QString error = reply->failed() ? reply->errorString()
						      : QStringLiteral("HTTP %1").arg(reply->status());
		reply_.clear();
		reply->deleteLater();
		if (!ok) {
			emit failed(query, error);
			return;
		}
		emit found(query, usefulResults(parseAnswer(answer)));
	});
}

} // namespace laycue::lyrics
