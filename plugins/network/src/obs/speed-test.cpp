#include "obs/speed-test.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>

#include <algorithm>

namespace laycue::network {

namespace {
constexpr int kPayloadBytes = 8 * 1024 * 1024;
constexpr int kLatencyRequests = 5;
constexpr int kSampleMs = 500;
} // namespace

SpeedTest::SpeedTest(QObject *parent) : QObject(parent)
{
	sampleTimer_.setInterval(kSampleMs);
	connect(&sampleTimer_, &QTimer::timeout, this, &SpeedTest::sample);
}

SpeedTest::~SpeedTest()
{
	teardown();
}

void SpeedTest::start(const QUrl &server, int seconds, int connections)
{
	if (running_)
		return;
	if (!server.isValid() || server.scheme().isEmpty()) {
		emit failed(Failure::BadAddress);
		return;
	}
	server_ = server;
	seconds_ = std::clamp(seconds, 4, 30);
	connections_ = std::clamp(connections, 1, 6);
	running_ = true;
	slices_.clear();
	rtts_.clear();
	requests_ = failedRequests_ = 0;
	finishedBytes_ = lastTotal_ = lastMs_ = 0;

	if (payload_.size() != kPayloadBytes) {
		payload_.resize(kPayloadBytes);
		// Random, so nothing along the way can compress it into looking faster than it is.
		auto *data = reinterpret_cast<quint32 *>(payload_.data());
		QRandomGenerator rng(0xC0FFEE);
		for (int i = 0; i < kPayloadBytes / 4; ++i)
			data[i] = rng.generate();
	}
	latencyLeft_ = kLatencyRequests;
	measureLatency();
}

void SpeedTest::cancel()
{
	if (!running_)
		return;
	teardown();
	emit failed(Failure::Cancelled);
}

void SpeedTest::teardown()
{
	running_ = false;
	sampleTimer_.stop();
	const QList<QNetworkReply *> replies = uploads_;
	uploads_.clear();
	sentPerReply_.clear();
	for (QNetworkReply *reply : replies) {
		reply->disconnect(this);
		reply->abort();
		reply->deleteLater();
	}
}

// Round-trip time: a few tiny requests, one after the other.
void SpeedTest::measureLatency()
{
	if (!running_)
		return;
	if (latencyLeft_ <= 0) {
		startUpload();
		return;
	}
	--latencyLeft_;
	++requests_;
	QNetworkRequest request(server_);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
	request.setTransferTimeout(5000);
	latencyClock_.start();
	QNetworkReply *reply = manager_.post(request, QByteArray(1, 'x'));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		if (!running_)
			return;
		if (reply->error() == QNetworkReply::NoError || reply->error() >= QNetworkReply::ContentAccessDenied)
			rtts_.push_back(double(latencyClock_.nsecsElapsed()) / 1.0e6);
		else
			++failedRequests_;
		// Nothing answered at all in the first requests: no point going on.
		if (failedRequests_ >= 3 && rtts_.empty()) {
			fail(Failure::Unreachable);
			return;
		}
		measureLatency();
	});
}

void SpeedTest::startUpload()
{
	clock_.start();
	lastMs_ = 0;
	lastTotal_ = 0;
	for (int i = 0; i < connections_; ++i)
		launchUpload();
	sampleTimer_.start();
}

void SpeedTest::launchUpload()
{
	if (!running_)
		return;
	QNetworkRequest request(server_);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
	request.setTransferTimeout(15000);
	QNetworkReply *reply = manager_.post(request, payload_);
	uploads_.push_back(reply);
	sentPerReply_.push_back(0);
	connect(reply, &QNetworkReply::uploadProgress, this, [this, reply](qint64 sent, qint64) {
		const int index = int(uploads_.indexOf(reply));
		if (index >= 0)
			sentPerReply_[index] = sent;
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const int index = int(uploads_.indexOf(reply));
		if (index >= 0) {
			uploads_.removeAt(index);
			sentPerReply_.removeAt(index);
		}
		const bool ok = reply->error() == QNetworkReply::NoError;
		reply->deleteLater();
		if (!running_)
			return;
		++requests_;
		if (ok) {
			finishedBytes_ += payload_.size();
			if (clock_.elapsed() < qint64(seconds_) * 1000)
				launchUpload(); // keep the pipe full until time is up
		} else {
			++failedRequests_;
			// An upload that was cut short still sent bytes; the sampler already counted them via progress.
		}
	});
}

void SpeedTest::sample()
{
	if (!running_)
		return;
	qint64 total = finishedBytes_;
	for (qint64 v : sentPerReply_)
		total += v;
	const qint64 now = clock_.elapsed();
	const double dt = double(now - lastMs_) / 1000.0;
	if (dt > 0.0) {
		const double bytes = double(std::max<qint64>(total - lastTotal_, 0));
		slices_.push_back({dt, bytes});
		emit progress(bytes * 8.0 / dt / 1.0e6, std::min(1.0, double(now) / (seconds_ * 1000.0)));
	}
	lastTotal_ = total;
	lastMs_ = now;
	if (now >= qint64(seconds_) * 1000)
		finish();
}

void SpeedTest::finish()
{
	const std::vector<UploadSlice> slices = slices_;
	const UploadMeasure measure = analyseUpload(slices, rtts_, requests_, failedRequests_);
	teardown();
	if (!measure.valid) {
		emit failed(Failure::NotEnoughData);
		return;
	}
	emit finished(measure);
}

void SpeedTest::fail(Failure reason)
{
	teardown();
	emit failed(reason);
}

} // namespace laycue::network
