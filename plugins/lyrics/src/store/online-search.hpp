// Asks LRCLIB for songs matching a name, off the UI thread's back (the answer arrives as a signal).
#pragma once

#include "core/online.hpp"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

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
	bool busy() const { return !reply_.isNull(); }

signals:
	void found(const QString &query, const std::vector<laycue::lyrics::OnlineSong> &songs);
	void failed(const QString &query, const QString &reason);

private:
	net::Http *http_ = nullptr;
	QPointer<net::Reply> reply_;
	QString query_;
	QByteArray buffer_; // the answer of the current search, as it arrives
};

} // namespace laycue::lyrics
