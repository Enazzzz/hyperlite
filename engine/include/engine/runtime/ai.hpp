#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

/**
 * Finite-state machine (native). Transition table is data, not Python.
 */
class StateMachine {
public:
	int AddState(const char* name);
	void AddTransition(const int from, const int to, const int event_id);
	void SetState(const int state);
	int State() const { return state_; }
	bool Fire(const int event_id);
	const char* StateName() const;

private:
	struct Trans {
		int from = 0;
		int to = 0;
		int event = 0;
	};
	std::vector<std::string> names_{};
	std::vector<Trans> trans_{};
	int state_ = 0;
};

enum class BtStatus : int { Failure = 0, Success = 1, Running = 2 };

/**
 * Tiny behavior-tree node: action callback or composite (sequence/selector).
 */
struct BtNode {
	enum class Kind { Action, Sequence, Selector } kind = Kind::Action;
	BtStatus (*action)(void*) = nullptr;
	std::vector<int> children{};
};

class BehaviorTree {
public:
	int AddAction(BtStatus (*fn)(void*));
	int AddSequence(const int* children, const int count);
	int AddSelector(const int* children, const int count);
	void SetRoot(const int node);
	BtStatus Tick(void* user);

private:
	BtStatus TickNode(const int node, void* user);
	std::vector<BtNode> nodes_{};
	int root_ = -1;
};

/**
 * Steering: seek / flee / arrive. Writes acceleration into out.
 */
Vec3 SteerSeek(const Vec3 position, const Vec3 target, const float max_speed, const Vec3 velocity);
Vec3 SteerFlee(const Vec3 position, const Vec3 threat, const float max_speed, const Vec3 velocity);
Vec3 SteerArrive(const Vec3 position, const Vec3 target, const float max_speed, const float slow_radius, const Vec3 velocity);

/**
 * Range query helper for perception (uses provided positions).
 */
int PerceiveInRange(
	const Vec3 origin,
	const float radius,
	const Vec3* positions,
	const int count,
	int* out_ids,
	const int cap);

} // namespace hyperlite
