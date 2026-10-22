// Scrolling text (crawl) maths. Pure logic.
#pragma once

namespace laycue::titles {

// A strip of text that scrolls right to left and repeats. All sizes are in pixels.
class TickerState {
public:
	// How wide the repeated strip is (text plus the gap after it) and how fast it moves.
	void configure(double stripWidth, double pixelsPerSecond);
	void advance(double seconds);
	void reset() { offset_ = 0.0; }

	// How far the strip has moved to the left, always 0 <= offset < stripWidth.
	double offset() const { return offset_; }
	// The x positions (relative to the left edge of the viewport) at which copies of the strip must
	// be drawn so that a viewport of `viewWidth` pixels is always filled.
	int copiesNeeded(double viewWidth) const;
	double copyX(int index) const { return -offset_ + index * stripWidth_; }

private:
	double stripWidth_ = 1.0;
	double speed_ = 120.0;
	double offset_ = 0.0;
};

} // namespace laycue::titles
