// The "Test my connection" upload test. It sends random bytes to a test server for a few seconds
// and measures how fast they go. Nothing is sent unless the operator presses Test, and the server
// address can be changed. Uses Qt's network classes on the UI thread's event loop; never blocks.
#pragma once

#include "core/speed-analysis.hpp"
#include "net/http.hpp"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QUrl>

namespace laycue::network {

class SpeedTest : public QObject {
	Q_OBJECT

public:
	// Why a test did not finish (the panel turns these into plain-language text).
	enum class Failure { BadAddress, Unreachable, NotEnoughData, Cancelled, NoSecure };
	Q_ENUM(Failure)

	// What the network layer said about the last failed request ("Connection refused", "TLS initialization failed"), for the log and the panel.
	QString detail() const { return detail_; }

	explicit SpeedTest(QObject *parent = nullptr);
	~SpeedTest() override;

	// `seconds` of uploading over `connections` parallel requests. Does nothing if already running.
	void start(const QUrl &server, int seconds = 8, int connections = 3);
	void cancel();
	bool running() const { return running_; }

signals:
	void progress(double mbps, double fraction); // the current speed, and how far through the test (0..1)
	void finished(const laycue::network::UploadMeasure &measure);
	void failed(laycue::network::SpeedTest::Failure reason);

private:
	void measureLatency();
	void startUpload();
	void launchUpload();
	void sample();
	void finish();
	void fail(Failure reason);
	QString detail_;
	void teardown();

	net::Http http_;
	QUrl server_;
	int seconds_ = 8;
	int connections_ = 3;
	bool running_ = false;

	QByteArray payload_;
	QList<net::Reply *> uploads_;
	QList<qint64> sentPerReply_; // bytes sent so far by each active reply
	qint64 finishedBytes_ = 0;   // bytes of requests that completed
	qint64 lastTotal_ = 0;
	qint64 lastMs_ = 0;
	QElapsedTimer clock_;
	QTimer sampleTimer_;
	std::vector<UploadSlice> slices_;

	std::vector<double> rtts_;
	int requests_ = 0;
	int failedRequests_ = 0;
	int latencyLeft_ = 0;
	QElapsedTimer latencyClock_;
};

} // namespace laycue::network
