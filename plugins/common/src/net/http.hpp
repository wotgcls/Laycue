// A tiny HTTP client for the Laycue plugins. It uses Qt's network classes where they can make secure (https)
// connections, and Windows' own WinHTTP where they cannot: OBS on Windows does not always carry Qt's TLS part,
// and without it every https request fails. Either way the caller sees the same small Reply.
//
// UI thread only. Nothing here blocks: answers arrive as signals.
#pragma once

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>

namespace laycue::net {

class Reply : public QObject {
	Q_OBJECT

public:
	using Headers = QList<QPair<QByteArray, QByteArray>>;

	int status() const { return status_; }      // the HTTP status, 0 until an answer arrives
	QUrl url() const { return url_; }           // the address that was asked
	QUrl location() const { return location_; } // where a 3xx answer points (may be relative)
	bool failed() const { return failed_; }     // no answer at all: refused, timed out, TLS trouble...
	bool ok() const { return !failed_ && status_ >= 200 && status_ < 400; }
	QString errorString() const { return error_; }
	QByteArray readAll()
	{
		QByteArray all;
		all.swap(buffer_);
		return all;
	} // what arrived since the last call
	virtual void abort() = 0; // stops it; no signals are sent for an abort you asked for

signals:
	void readyRead();
	void downloadProgress(qint64 received, qint64 total); // total is -1 when the server did not say
	void uploadProgress(qint64 sent, qint64 total);
	void finished();

protected:
	Reply(const QUrl &url, QObject *parent) : QObject(parent), url_(url) {}

	int status_ = 0;
	QUrl url_;
	QUrl location_;
	bool failed_ = false;
	QString error_;
	QByteArray buffer_;
};

class Http : public QObject {
	Q_OBJECT

public:
	explicit Http(QObject *parent = nullptr);

	// True when https requests can work here (Qt has TLS, or this is Windows and WinHTTP is used).
	static bool secureAvailable();

	// Redirects are never followed: the caller looks at status() and location(), so it can check every address.
	Reply *get(const QUrl &url, const Reply::Headers &headers, int timeoutMs);
	Reply *post(const QUrl &url, const QByteArray &body, const QByteArray &contentType, int timeoutMs);

private:
	QNetworkAccessManager manager_;
};

#if defined(_WIN32)
// Implemented in http-win.cpp.
Reply *makeWinHttpReply(QObject *parent, const QUrl &url, const QByteArray &verb, const QByteArray &body,
			const Reply::Headers &headers, int timeoutMs);
#endif

} // namespace laycue::net
