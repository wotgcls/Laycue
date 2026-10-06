#include "core/clock-core.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace laycue::clock {

void Timer::configure(Mode mode, double durationSeconds)
{
	mode_ = mode;
	duration_ = std::clamp(durationSeconds, 0.0, kMaxSeconds);
}

double Timer::elapsed(double now) const
{
	return accumulated_ + (running_ ? std::max(0.0, now - startedAt_) : 0.0);
}

void Timer::start(double now)
{
	if (running_)
		return;
	running_ = true;
	startedAt_ = now;
}

void Timer::pause(double now)
{
	if (!running_)
		return;
	accumulated_ = elapsed(now);
	running_ = false;
}

void Timer::toggle(double now)
{
	if (running_)
		pause(now);
	else
		start(now);
}

void Timer::reset()
{
	running_ = false;
	accumulated_ = 0.0;
	startedAt_ = 0.0;
}

void Timer::adjust(double delta, double now)
{
	if (mode_ == Mode::CountUp) {
		const double e = elapsed(now) + delta;
		accumulated_ = std::max(0.0, e) - (running_ ? std::max(0.0, now - startedAt_) : 0.0);
		return;
	}
	duration_ = std::clamp(duration_ + delta, 0.0, kMaxSeconds);
}

Reading Timer::read(double now) const
{
	Reading r;
	const double e = elapsed(now);
	if (mode_ == Mode::CountUp) {
		r.seconds = std::min(e, kMaxSeconds);
		return r;
	}
	const double left = duration_ - e;
	r.fraction = duration_ > 0.0 ? std::clamp(e / duration_, 0.0, 1.0) : 1.0;
	if (left > 0.0) {
		r.seconds = left;
		r.level = left <= warnSeconds_ ? Level::Warning : Level::Normal;
		return r;
	}
	r.finished = true;
	r.level = Level::Over;
	if (overtime_) {
		r.overtime = true;
		r.seconds = std::min(-left, kMaxSeconds);
	} else {
		r.seconds = 0.0;
	}
	return r;
}

std::string formatTime(double seconds, bool countdown, bool forceHours)
{
	seconds = std::clamp(seconds, 0.0, kMaxSeconds);
	const long total = countdown ? long(std::ceil(seconds - 1e-9)) : long(std::floor(seconds + 1e-9));
	const long h = total / 3600, m = (total % 3600) / 60, s = total % 60;
	char buf[32];
	if (h > 0 || forceHours)
		std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", h, m, s);
	else
		std::snprintf(buf, sizeof(buf), "%02ld:%02ld", m, s);
	return buf;
}

namespace {

std::string trimmed(const std::string &text)
{
	size_t a = 0, b = text.size();
	while (a < b && std::isspace(static_cast<unsigned char>(text[a])))
		++a;
	while (b > a && std::isspace(static_cast<unsigned char>(text[b - 1])))
		--b;
	std::string t = text.substr(a, b - a);
	for (char &c : t)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return t;
}

// Reads digits starting at `i`; returns false if there are none or too many.
bool readNumber(const std::string &t, size_t &i, long &out)
{
	if (i >= t.size() || !std::isdigit(static_cast<unsigned char>(t[i])))
		return false;
	long n = 0;
	int digits = 0;
	while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
		n = n * 10 + (t[i] - '0');
		if (++digits > 5)
			return false;
		++i;
	}
	out = n;
	return true;
}

} // namespace

bool parseDuration(const std::string &text, double &seconds)
{
	const std::string t = trimmed(text);
	if (t.empty())
		return false;
	size_t i = 0;
	long a = 0;
	if (!readNumber(t, i, a))
		return false;
	double result = 0.0;
	if (i == t.size()) {
		result = double(a) * 60.0; // "5" means five minutes
	} else if (t[i] == ':') {
		long b = 0, c = -1;
		++i;
		if (!readNumber(t, i, b) || b > 59)
			return false;
		if (i < t.size() && t[i] == ':') {
			++i;
			if (!readNumber(t, i, c) || c > 59)
				return false;
		}
		if (i != t.size())
			return false;
		result = c < 0 ? double(a) * 60.0 + double(b) : double(a) * 3600.0 + double(b) * 60.0 + double(c);
	} else {
		while (i < t.size() && t[i] == ' ')
			++i; // "10 min"
		const std::string rest = t.substr(i);
		if (rest == "s" || rest == "sec" || rest == "secs")
			result = double(a);
		else if (rest == "m" || rest == "min" || rest == "mins")
			result = double(a) * 60.0;
		else if (rest == "h" || rest == "hr" || rest == "hour" || rest == "hours")
			result = double(a) * 3600.0;
		else
			return false;
	}
	if (result > kMaxSeconds)
		return false;
	seconds = result;
	return true;
}

bool parseTimeOfDay(const std::string &text, int &secondOfDay)
{
	std::string t = trimmed(text);
	bool pm = false, am = false;
	if (t.size() >= 2 && t.compare(t.size() - 2, 2, "pm") == 0) {
		pm = true;
		t = trimmed(t.substr(0, t.size() - 2));
	} else if (t.size() >= 2 && t.compare(t.size() - 2, 2, "am") == 0) {
		am = true;
		t = trimmed(t.substr(0, t.size() - 2));
	}
	size_t i = 0;
	long h = 0, m = 0, s = 0;
	if (!readNumber(t, i, h))
		return false;
	if (i < t.size() && t[i] == ':') {
		++i;
		if (!readNumber(t, i, m) || m > 59)
			return false;
		if (i < t.size() && t[i] == ':') {
			++i;
			if (!readNumber(t, i, s) || s > 59)
				return false;
		}
	}
	if (i != t.size())
		return false;
	if (pm || am) {
		if (h < 1 || h > 12)
			return false;
		h = (h % 12) + (pm ? 12 : 0);
	}
	if (h > 23)
		return false;
	secondOfDay = int(h * 3600 + m * 60 + s);
	return true;
}

double secondsUntilTimeOfDay(int nowSecondOfDay, int targetSecondOfDay)
{
	int diff = targetSecondOfDay - nowSecondOfDay;
	while (diff < 0)
		diff += 86400;
	return double(diff % 86400);
}

} // namespace laycue::clock
