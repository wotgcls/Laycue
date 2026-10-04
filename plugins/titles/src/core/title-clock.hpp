// The on-air state of one title: hidden, animating in, held, animating out. Pure logic: the OBS
// source calls advance() once per rendered frame and draws whatever in()/out() say.
#pragma once

namespace laycue::titles {

class TitleClock {
public:
	enum class Phase { Hidden, In, Hold, Out };

	// Seconds for the enter and exit animations, and how long a shown title stays before it hides
	// itself (0 = until hidden by hand).
	void configure(double inSeconds, double outSeconds, double holdSeconds);

	// Start the enter animation. Showing a title that is already shown does nothing; showing one
	// that is leaving turns it around from where it is.
	void show();
	// Start the exit animation. Hiding a title that is still arriving leaves from where it is.
	void hide();
	// Put the title straight into the hidden state (no animation).
	void reset();

	void advance(double seconds);

	Phase phase() const { return phase_; }
	bool visible() const { return phase_ != Phase::Hidden; }
	bool animating() const { return phase_ == Phase::In || phase_ == Phase::Out; }

	// How far the enter animation has run (0..1). Stays at the value reached when the exit starts.
	double in() const { return in_; }
	// How far the exit animation has run (0..1).
	double out() const { return out_; }
	// Seconds left before the title hides itself, or a negative number if it never does.
	double holdRemaining() const;

private:
	Phase phase_ = Phase::Hidden;
	double inSeconds_ = 0.7;
	double outSeconds_ = 0.45;
	double holdSeconds_ = 0.0;
	double in_ = 0.0;
	double out_ = 0.0;
	double held_ = 0.0;
};

} // namespace laycue::titles
