#include "net/http.hpp"

#include <util/base.h>

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>

namespace laycue::net {

namespace {

// Wraps a QNetworkReply (the normal route).
class QtReply : public Reply {
public:
	QtReply(QObject *parent, QNetworkReply *reply) : Reply(reply->url(), parent), reply_(reply)
	{
		reply->setParent(this);
		connect(reply, &QNetworkReply::readyRead, this, [this]() {
			pull();
			emit readyRead();
		});
		connect(reply, &QNetworkReply::downloadProgress, this, &Reply::downloadProgress);
		connect(reply, &QNetworkReply::uploadProgress, this, &Reply::uploadProgress);
		connect(reply, &QNetworkReply::finished, this, [this]() {
			pull();
			// Transport trouble (no answer) is a failure; an HTTP error status is an answer, read from status().
			failed_ = reply_->error() != QNetworkReply::NoError &&
				  reply_->error() < QNetworkReply::ContentAccessDenied;
			if (reply_->error() != QNetworkReply::NoError)
				error_ = reply_->errorString();
			emit finished();
		});
	}

	void abort() override { reply_->abort(); }

private:
	void pull()
	{
		status_ = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QUrl where = reply_->header(QNetworkRequest::LocationHeader).toUrl();
		if (where.isValid() && !where.isEmpty())
			location_ = where;
		buffer_.append(reply_->readAll());
	}

	QNetworkReply *reply_;
};

bool needsWinHttp(const QUrl &url)
{
#if defined(_WIN32)
	return url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0 && !QSslSocket::supportsSsl();
#else
	(void)url;
	return false;
#endif
}

} // namespace

Http::Http(QObject *parent) : QObject(parent) {}

bool Http::secureAvailable()
{
#if defined(_WIN32)
	return true; // WinHTTP covers it when Qt cannot
#else
	return QSslSocket::supportsSsl();
#endif
}

Reply *Http::get(const QUrl &url, const Reply::Headers &headers, int timeoutMs)
{
#if defined(_WIN32)
	if (needsWinHttp(url)) {
		blog(LOG_INFO, "[laycue-net] this OBS has no Qt TLS part: using Windows (WinHTTP) for %s",
		     url.host().toUtf8().constData());
		return makeWinHttpReply(this, url, "GET", QByteArray(), headers, timeoutMs);
	}
#endif
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	for (const auto &h : headers)
		request.setRawHeader(h.first, h.second);
	request.setTransferTimeout(timeoutMs);
	return new QtReply(this, manager_.get(request));
}

Reply *Http::post(const QUrl &url, const QByteArray &body, const QByteArray &contentType, int timeoutMs)
{
#if defined(_WIN32)
	if (needsWinHttp(url)) {
		Reply::Headers headers;
		headers.append({"Content-Type", contentType});
		return makeWinHttpReply(this, url, "POST", body, headers, timeoutMs);
	}
#endif
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setRawHeader("Content-Type", contentType);
	request.setTransferTimeout(timeoutMs);
	return new QtReply(this, manager_.post(request, body));
}

} // namespace laycue::net
