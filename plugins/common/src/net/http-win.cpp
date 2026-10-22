// The Windows route: WinHTTP, the secure web client that is part of Windows itself (no extra files, no new
// download). A worker thread does the blocking calls; everything it learns is handed to the UI thread as
// queued events, so the Reply behaves exactly like the Qt one.
#include "net/http.hpp"

#include <util/base.h>

#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

namespace laycue::net {

namespace {

QString winMessage(DWORD code)
{
	wchar_t text[300] = {};
	const DWORD n =
		FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			       GetModuleHandleW(L"winhttp.dll"), code, 0, text, 299, nullptr);
	QString message = n ? QString::fromWCharArray(text, int(n)).trimmed() : QStringLiteral("network error");
	return QStringLiteral("%1 (WinHTTP %2)").arg(message).arg(code);
}

class WinReply : public Reply {
public:
	WinReply(QObject *parent, const QUrl &url, const QByteArray &verb, const QByteArray &body,
		 const Reply::Headers &headers, int timeoutMs)
		: Reply(url, parent)
	{
		thread_ = std::thread([this, url, verb, body, headers, timeoutMs]() {
			try {
				run(url, verb, body, headers, timeoutMs);
			} catch (...) {
				fail(QStringLiteral("unexpected error in the network helper"));
			}
		});
	}

	~WinReply() override
	{
		abort();
		if (thread_.joinable())
			thread_.join();
	}

	void abort() override
	{
		aborted_ = true;
		std::lock_guard<std::mutex> lock(mutex_);
		closeLocked();
	}

private:
	template<class F> void post(F f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }

	void fail(const QString &message)
	{
		if (aborted_)
			return; // an abort you asked for sends nothing
		post([this, message]() {
			failed_ = true;
			error_ = message;
			emit finished();
		});
	}

	void closeLocked()
	{
		if (req_)
			WinHttpCloseHandle(req_);
		if (conn_)
			WinHttpCloseHandle(conn_);
		if (session_)
			WinHttpCloseHandle(session_);
		req_ = conn_ = session_ = nullptr;
	}

	// Keeps a handle so abort() can close it from the UI thread; false if we were aborted in the meantime.
	bool keep(HINTERNET &slot, HINTERNET handle)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (aborted_) {
			if (handle)
				WinHttpCloseHandle(handle);
			return false;
		}
		slot = handle;
		return true;
	}

	void run(const QUrl &url, const QByteArray &verb, const QByteArray &body, const Reply::Headers &headers,
		 int timeoutMs)
	{
		const bool secure = url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0;
		const std::wstring host = url.host().toStdWString();
		const INTERNET_PORT port = INTERNET_PORT(url.port(secure ? 443 : 80));
		QByteArray path = url.path(QUrl::FullyEncoded).toUtf8();
		if (path.isEmpty())
			path = "/";
		if (url.hasQuery())
			path += "?" + url.query(QUrl::FullyEncoded).toUtf8();

		HINTERNET session = WinHttpOpen(L"Laycue", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
						WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session) {
			fail(winMessage(GetLastError()));
			return;
		}
		if (!keep(session_, session))
			return;
		WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

		HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
		if (!conn) {
			fail(winMessage(GetLastError()));
			return;
		}
		if (!keep(conn_, conn))
			return;

		const std::wstring wverb(verb.begin(), verb.end());
		const std::wstring wpath = QString::fromUtf8(path).toStdWString();
		HINTERNET req = WinHttpOpenRequest(conn, wverb.c_str(), wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
						   WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
		if (!req) {
			fail(winMessage(GetLastError()));
			return;
		}
		if (!keep(req_, req))
			return;

		DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER; // the caller checks every address by hand
		WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

		std::wstring headerText;
		for (const auto &h : headers)
			headerText += QString::fromLatin1(h.first + ": " + h.second + "\r\n").toStdWString();
		if (!headerText.empty())
			WinHttpAddRequestHeaders(req, headerText.c_str(), DWORD(-1),
						 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

		const bool hasBody = verb == "POST";
		if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
					hasBody ? DWORD(body.size()) : 0, 0)) {
			fail(winMessage(GetLastError()));
			return;
		}
		if (hasBody) {
			qint64 sent = 0;
			const qint64 total = body.size();
			while (sent < total) {
				if (aborted_)
					return;
				const DWORD chunk = DWORD(std::min<qint64>(32 * 1024, total - sent));
				DWORD wrote = 0;
				if (!WinHttpWriteData(req, body.constData() + sent, chunk, &wrote)) {
					fail(winMessage(GetLastError()));
					return;
				}
				sent += wrote;
				post([this, sent, total]() { emit uploadProgress(sent, total); });
			}
		}
		if (!WinHttpReceiveResponse(req, nullptr)) {
			fail(winMessage(GetLastError()));
			return;
		}

		DWORD code = 0, size = sizeof(code);
		WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				    WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
		qint64 total = -1;
		DWORD length = 0;
		size = sizeof(length);
		if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
					WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX))
			total = length;
		QString where;
		DWORD need = 0;
		WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
				    &need, WINHTTP_NO_HEADER_INDEX);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && need > 0) {
			std::wstring text(need / sizeof(wchar_t) + 1, L'\0');
			if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, text.data(),
						&need, WINHTTP_NO_HEADER_INDEX))
				where = QString::fromWCharArray(text.c_str());
		}
		post([this, code, where]() {
			status_ = int(code);
			if (!where.isEmpty())
				location_ = QUrl(where);
		});

		qint64 received = 0;
		for (;;) {
			if (aborted_)
				return;
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(req, &available)) {
				fail(winMessage(GetLastError()));
				return;
			}
			if (available == 0)
				break;
			QByteArray chunk(int(std::min<DWORD>(available, 64 * 1024)), Qt::Uninitialized);
			DWORD read = 0;
			if (!WinHttpReadData(req, chunk.data(), DWORD(chunk.size()), &read)) {
				fail(winMessage(GetLastError()));
				return;
			}
			if (read == 0)
				break;
			chunk.resize(int(read));
			received += read;
			post([this, chunk, received, total]() {
				buffer_.append(chunk);
				emit readyRead();
				emit downloadProgress(received, total);
			});
		}
		post([this]() { emit finished(); });
	}

	std::atomic<bool> aborted_{false};
	std::mutex mutex_;
	HINTERNET session_ = nullptr, conn_ = nullptr, req_ = nullptr;
	std::thread thread_;
};

} // namespace

Reply *makeWinHttpReply(QObject *parent, const QUrl &url, const QByteArray &verb, const QByteArray &body,
			const Reply::Headers &headers, int timeoutMs)
{
	return new WinReply(parent, url, verb, body, headers, timeoutMs);
}

} // namespace laycue::net
