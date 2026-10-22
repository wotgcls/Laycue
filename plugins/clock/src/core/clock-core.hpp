// The pure rules of Laycue Clock: a timer that counts down, up, or down to a time of day, drift-free, with a warning
// and an overtime phase, plus reading and printing times. No OBS, no Qt: fully unit tested.
#pragma once

#include <string>

namespace laycue::clock {

enum class Mode { Countdown = 0, CountUp = 1, Until = 2 }; // Until: a countdown to a time of day ("10:00")
constexpr int kModeCount = 3;

enum class Level { Normal, Warning, Over }; // Warning: little time left. Over: zero reached.

constexpr double kMaxSeconds = 99.0 * 3600.0 + 59.0 * 60.0 + 59.0;

// What the clock shows right now.
struct Reading {
	double seconds = 0.0;  // the number to print: time left (countdown), time passed (count up), or overtime
	bool overtime = false; // a countdown that went past zero and keeps counting up ("+00:12")
	bool finished = false; // a countdown that reached zero
	double fraction = 0.0; // 0..1 of the countdown used up (for a progress ring or bar); 0 for count up
	Level level = Level::Normal;
};

// All times are seconds on a steady clock, passed in so tests are exact. One Timer is one clock.
class Timer {
public:
	void configure(Mode mode, double durationSeconds);
	void setWarning(double seconds) { warnSeconds_ = seconds < 0.0 ? 0.0 : seconds; }
	void setOvertime(bool on) { overtime_ = on; }

	void start(double now);
	void pause(double now);
	void toggle(double now); // start or pause
	void reset();
	// Moves the time by `delta` seconds: a countdown gets longer or shorter, a count up jumps.
	void adjust(double delta, double now);

	bool running() const { return running_; }
	Mode mode() const { return mode_; }
	double duration() const { return duration_; }
	Reading read(double now) const;

private:
	double elapsed(double now) const;

	Mode mode_ = Mode::Countdown;
	double duration_ = 300.0;
	double warnSeconds_ = 60.0;
	bool overtime_ = false;
	bool running_ = false;
	double startedAt_ = 0.0;
	double accumulated_ = 0.0;
};

// "05:00" or "1:05:00" for what is on screen. A countdown rounds up, so 00:00 appears exactly at zero.
std::string formatTime(double seconds, bool countdown, bool forceHours = false);

// Reads what the operator types: "5" (minutes), "5:30", "1:30:00", "90s", "5m". False if it is not a time.
bool parseDuration(const std::string &text, double &seconds);

// Reads a time of day: "10:30", "9", "9:05 pm", "21:00". Gives seconds since midnight.
bool parseTimeOfDay(const std::string &text, int &secondOfDay);

// How long until the next time the clock shows `targetSecondOfDay`, from `nowSecondOfDay` (0 to 86399). Tomorrow if it
// has already passed today; 0 if it is exactly now.
double secondsUntilTimeOfDay(int nowSecondOfDay, int targetSecondOfDay);

// A time of day for people: "5:00 PM". `secondOfDay` is seconds since midnight (wraps around a day).
std::string formatTimeOfDay(int secondOfDay);
// The same time in the form that is saved and read back by parseTimeOfDay: "17:00".
std::string formatTimeOfDay24(int secondOfDay);
// Moves a time of day by `deltaSeconds`, staying inside one day (23:58 + 5 minutes = 00:03).
int shiftTimeOfDay(int secondOfDay, int deltaSeconds);
// A wait in a few plain words: "under 1 min", "42 min", "1 h 05 min", "5 h".
std::string describeWait(double seconds);

} // namespace laycue::clock
