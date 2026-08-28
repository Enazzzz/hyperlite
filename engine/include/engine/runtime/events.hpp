#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

/**
 * Native event kinds. Python callbacks fire only when a listener is registered.
 */
enum class EventKind : std::uint32_t {
	WindowClose = 1,
	WindowResize = 2,
	Key = 3,
	MouseMove = 4,
	MouseButton = 5,
	Gamepad = 6,
	Custom = 100,
};

/**
 * Discriminated native event. Payload is small and copyable.
 */
struct Event {
	EventKind kind = EventKind::Custom;
	std::int32_t a = 0;
	std::int32_t b = 0;
	std::int32_t c = 0;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	std::uint64_t user = 0;
};

/**
 * C callback for native listeners (no Python).
 */
using EventListenerFn = void (*)(const Event&, void*);

/**
 * FIFO event queue. Push from input/window; drain explicitly or during Game::Step.
 */
class EventQueue {
public:
	/** Push one event onto the queue. */
	void Push(const Event event);

	/** Pop one event; returns false when empty. */
	bool Poll(Event& out);

	/** Discard all queued events. */
	void Clear();

	/** Number of queued events. */
	std::size_t Size() const { return events_.size(); }

	/**
	 * Register a native listener. Returns a handle (>=0) for Unlisten.
	 */
	int Listen(const EventKind kind, EventListenerFn fn, void* user);

	/** Remove a listener by handle. */
	void Unlisten(const int handle);

	/** Dispatch queued events to listeners, then clear the queue. */
	void Dispatch();

private:
	struct Listener {
		int handle = -1;
		EventKind kind = EventKind::Custom;
		EventListenerFn fn = nullptr;
		void* user = nullptr;
		bool alive = false;
	};

	std::vector<Event> events_{};
	std::vector<Listener> listeners_{};
	int next_handle_ = 1;
};

} // namespace hyperlite
