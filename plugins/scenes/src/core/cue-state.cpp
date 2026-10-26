#include "core/cue-state.hpp"

namespace laycue {

PickResult pickShot(CueState &state, const std::string &shot)
{
	if (!shot.empty() && shot == state.live)
		return PickResult::AlreadyLive;
	if (shot == state.next) {
		state.next.clear();
		return PickResult::Uncued;
	}
	state.next = shot;
	return PickResult::Cued;
}

bool canTake(const CueState &state)
{
	return !state.next.empty() && state.next != state.live;
}

void take(CueState &state, const std::string &requeue)
{
	if (!canTake(state))
		return;
	state.live = state.next;
	if (!requeue.empty() && requeue != state.live)
		state.next = requeue;
	else
		state.next.clear();
}

void liveChanged(CueState &state, const std::string &live)
{
	state.live = live;
	if (state.next == live)
		state.next.clear();
}

void shotRemoved(CueState &state, const std::string &shot)
{
	if (shot.empty())
		return;
	if (state.next == shot)
		state.next.clear();
	if (state.live == shot)
		state.live.clear();
}

} // namespace laycue
