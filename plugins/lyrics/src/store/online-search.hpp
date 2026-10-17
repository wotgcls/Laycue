// Asks the online lyrics sources for songs matching a name: LRCLIB first, then lyrics.ovh when LRCLIB has few
// matches. Answers arrive as signals; nothing blocks the panel.
#pragma once

#include "core/online.hpp"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <functional>
#include <vector>

namespace laycue::net {
class Http;
class Reply;
} // namespace laycue::net

namespace laycue::lyrics {

class OnlineSearch : public QObject {
	Q_OBJECT

public:
	explicit OnlineSearch(QObject *parent = nullptr);

	// A new search replaces one still running.
	void search(const QString &query);
	// A lyrics.ovh song is listed without its words: this fetches them (wordsReady or wordsFailed).
	void fetchWords(const OnlineSong &song);

signals:
	void found(const QString &query, const std::vector<laycue::lyrics::OnlineSong> &songs);
	void failed(const QString &query, const QString &reason);
	void wordsReady(const laycue::lyrics::OnlineSong &song);
	void wordsFailed(const laycue::lyrics::OnlineSong &song, const QString &reason);

private:
	// One GET; `done(ok, body, error)` runs when it ends, unless a newer request of the same kind replaced it.
	void get(const QUrl &url, QPointer<net::Reply> &slot,
		 std::function<void(bool, const QByteArray &, const QString &)> done);

	net::Http *http_ = nullptr;
	QPointer<net::Reply> searchReply_;
	QPointer<net::Reply> wordsReply_;
	QString query_;
};

} // namespace laycue::lyrics
