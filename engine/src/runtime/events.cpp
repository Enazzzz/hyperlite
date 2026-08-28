#include "engine/runtime/events.hpp"

namespace hyperlite {

void EventQueue::Push(const Event event) {
	events_.push_back(event);
}

bool EventQueue::Poll(Event& out) {
	if (events_.empty()) {
		return false;
	}
	out = events_.front();
	events_.erase(events_.begin());
	return true;
}

void EventQueue::Clear() {
	events_.clear();
}

int EventQueue::Listen(const EventKind kind, EventListenerFn fn, void* user) {
	if (fn == nullptr) {
		return -1;
	}
	Listener l{};
	l.handle = next_handle_++;
	l.kind = kind;
	l.fn = fn;
	l.user = user;
	l.alive = true;
	listeners_.push_back(l);
	return l.handle;
}

void EventQueue::Unlisten(const int handle) {
	for (auto& l : listeners_) {
		if (l.handle == handle) {
			l.alive = false;
			l.fn = nullptr;
		}
	}
}

void EventQueue::Dispatch() {
	for (const Event& e : events_) {
		for (const auto& l : listeners_) {
			if (l.alive && l.fn != nullptr && l.kind == e.kind) {
				l.fn(e, l.user);
			}
		}
	}
	events_.clear();
}

} // namespace hyperlite
