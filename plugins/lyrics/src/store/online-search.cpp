#include "store/online-search.hpp"

#include "net/http.hpp"

#include <plugin-support.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

namespace laycue::lyrics {

namespace {

constexpr int kTimeoutMs = 15000;

std::vector<OnlineSong> parseLrclib(const QByteArray &json)
{
	std::vector<OnlineSong> out;
	const QJsonDocument doc = QJsonDocument::fromJson(json);
	if (!doc.isArray())
		return out;
	for (const QJsonValue value : doc.array()) {
		const QJsonObject o = value.toObject();
		OnlineSong s;
		s.source = OnlineSource::Lrclib;
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

// lyrics.ovh lists songs (from Deezer) without words: {"data":[{"title":..,"artist":{"name":..}}]}.
std::vector<OnlineSong> parseOvhSuggest(const QByteArray &json)
{
	std::vector<OnlineSong> out;
	const QJsonArray data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toArray();
	for (const QJsonValue value : data) {
		const QJsonObject o = value.toObject();
		OnlineSong s;
		s.source = OnlineSource::LyricsOvh;
		s.needsWords = true;
		s.id = o.value(QStringLiteral("id")).toVariant().toLongLong();
		s.title = o.value(QStringLiteral("title")).toString().toStdString();
		s.artist = o.value(QStringLiteral("artist"))
				   .toObject()
				   .value(QStringLiteral("name"))
				   .toString()
				   .toStdString();
		s.album = o.value(QStringLiteral("album"))
				  .toObject()
				  .value(QStringLiteral("title"))
				  .toString()
				  .toStdString();
		s.durationSeconds = o.value(QStringLiteral("duration")).toInt();
		if (!s.title.empty() && !s.artist.empty())
			out.push_back(s);
	}
	return out;
}

} // namespace

OnlineSearch::OnlineSearch(QObject *parent) : QObject(parent), http_(new net::Http(this)) {}

void OnlineSearch::get(const QUrl &url, QPointer<net::Reply> &slot,
		       std::function<void(bool, const QByteArray &, const QString &)> done)
{
	if (slot) {
		slot->abort();
		slot->deleteLater();
	}
	const net::Reply::Headers headers = {
		{"User-Agent", QByteArray("Laycue Lyrics ") + PLUGIN_VERSION + " (https://github.com/wotgcls/Laycue)"},
		{"Accept", "application/json"},
	};
	net::Reply *reply = http_->get(url, headers, kTimeoutMs);
	slot = reply;
	auto body = std::make_shared<QByteArray>();
	connect(reply, &net::Reply::readyRead, this, [reply, body]() { body->append(reply->readAll()); });
	QPointer<net::Reply> *slotPtr = &slot;
	connect(reply, &net::Reply::finished, this, [this, reply, body, slotPtr, done]() {
		reply->deleteLater();
		if (*slotPtr != reply)
			return; // a newer request took over
		slotPtr->clear();
		body->append(reply->readAll());
		const bool ok = reply->ok() && reply->status() == 200;
		const QString error = reply->failed() ? reply->errorString()
						      : QStringLiteral("HTTP %1").arg(reply->status());
		done(ok, *body, error);
	});
}

void OnlineSearch::search(const QString &query)
{
	query_ = query;
	if (!net::Http::secureAvailable()) {
		emit failed(query, QStringLiteral("secure"));
		return;
	}
	get(QUrl(QString::fromStdString(onlineSearchUrl(query.toStdString()))), searchReply_,
	    [this, query](bool ok, const QByteArray &body, const QString &error) {
		    if (query != query_)
			    return;
		    std::vector<OnlineSong> songs = ok ? parseLrclib(body) : std::vector<OnlineSong>();
		    // Always ask lyrics.ovh too: it lists more songs (their words come when one is picked).
		    const QString firstError = ok ? QString() : error;
		    get(QUrl(QString::fromStdString(ovhSuggestUrl(query.toStdString()))), searchReply_,
			[this, query, songs, firstError](bool ok2, const QByteArray &body2,
							 const QString &error2) mutable {
				if (query != query_)
					return;
				if (ok2) {
					for (const OnlineSong &s : parseOvhSuggest(body2))
						songs.push_back(s);
				}
				if (songs.empty() && !firstError.isEmpty() && !ok2) {
					emit failed(query, firstError);
					return;
				}
				(void)error2;
				emit found(query, listResults(songs));
			});
	    });
}

void OnlineSearch::fetchWords(const OnlineSong &song)
{
	// Every spelling worth trying, on both sites, until one has words: the title as listed, then without
	// "(Live)" and "feat. ...", with and without the singer.
	const std::string clean = cleanSongTitle(song.title);
	std::vector<WordsTry> tries;
	tries.push_back({true, song.artist, song.title});
	if (clean != song.title)
		tries.push_back({true, song.artist, clean});
	for (const std::string &text : {song.artist + " " + song.title, song.artist + " " + clean, clean, song.title}) {
		bool seen = false;
		for (const WordsTry &t : tries)
			seen = seen || (!t.ovh && t.text == text);
		if (!seen)
			tries.push_back({false, std::string(), text});
	}
	tryWords(song, tries, 0, QStringLiteral("no words"));
}

void OnlineSearch::tryWords(const OnlineSong &song, std::vector<WordsTry> tries, size_t index, const QString &reason)
{
	if (index >= tries.size()) {
		emit wordsFailed(song, reason);
		return;
	}
	const WordsTry attempt = tries[index];
	const QUrl url(QString::fromStdString(attempt.ovh ? ovhLyricsUrl(attempt.artist, attempt.text)
							  : onlineSearchUrl(attempt.text)));
	get(url, wordsReply_, [this, song, tries, index, attempt](bool ok, const QByteArray &body, const QString &error) {
		if (ok && attempt.ovh) {
			const QString words =
				QJsonDocument::fromJson(body).object().value(QStringLiteral("lyrics")).toString();
			if (!words.trimmed().isEmpty()) {
				OnlineSong withWords = song;
				withWords.needsWords = false;
				withWords.plainLyrics = cleanOvhLyrics(words.toStdString());
				emit wordsReady(withWords);
				return;
			}
		} else if (ok) {
			for (const OnlineSong &candidate : parseLrclib(body)) {
				if (songFromOnline(candidate).sections.empty())
					continue;
				OnlineSong withWords = candidate;
				withWords.title = song.title;
				withWords.artist = song.artist;
				withWords.needsWords = false;
				emit wordsReady(withWords);
				return;
			}
		}
		tryWords(song, tries, index + 1, ok ? QStringLiteral("no words") : error);
	});
}

} // namespace laycue::lyrics
