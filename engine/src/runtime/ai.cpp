#include "engine/runtime/ai.hpp"

#include <cmath>

namespace hyperlite {

int StateMachine::AddState(const char* name) {
	names_.push_back(name != nullptr ? name : "");
	return static_cast<int>(names_.size()) - 1;
}

void StateMachine::AddTransition(const int from, const int to, const int event_id) {
	trans_.push_back({from, to, event_id});
}

void StateMachine::SetState(const int state) {
	if (state >= 0 && static_cast<std::size_t>(state) < names_.size()) {
		state_ = state;
	}
}

bool StateMachine::Fire(const int event_id) {
	for (const Trans& t : trans_) {
		if (t.from == state_ && t.event == event_id) {
			state_ = t.to;
			return true;
		}
	}
	return false;
}

const char* StateMachine::StateName() const {
	if (state_ < 0 || static_cast<std::size_t>(state_) >= names_.size()) {
		return "";
	}
	return names_[static_cast<std::size_t>(state_)].c_str();
}

int BehaviorTree::AddAction(BtStatus (*fn)(void*)) {
	BtNode n{};
	n.kind = BtNode::Kind::Action;
	n.action = fn;
	nodes_.push_back(n);
	return static_cast<int>(nodes_.size()) - 1;
}

int BehaviorTree::AddSequence(const int* children, const int count) {
	BtNode n{};
	n.kind = BtNode::Kind::Sequence;
	for (int i = 0; i < count; ++i) {
		n.children.push_back(children[i]);
	}
	nodes_.push_back(n);
	return static_cast<int>(nodes_.size()) - 1;
}

int BehaviorTree::AddSelector(const int* children, const int count) {
	BtNode n{};
	n.kind = BtNode::Kind::Selector;
	for (int i = 0; i < count; ++i) {
		n.children.push_back(children[i]);
	}
	nodes_.push_back(n);
	return static_cast<int>(nodes_.size()) - 1;
}

void BehaviorTree::SetRoot(const int node) {
	root_ = node;
}

BtStatus BehaviorTree::TickNode(const int node, void* user) {
	if (node < 0 || static_cast<std::size_t>(node) >= nodes_.size()) {
		return BtStatus::Failure;
	}
	const BtNode& n = nodes_[static_cast<std::size_t>(node)];
	if (n.kind == BtNode::Kind::Action) {
		return n.action != nullptr ? n.action(user) : BtStatus::Failure;
	}
	if (n.kind == BtNode::Kind::Sequence) {
		for (const int c : n.children) {
			const BtStatus st = TickNode(c, user);
			if (st != BtStatus::Success) {
				return st;
			}
		}
		return BtStatus::Success;
	}
	for (const int c : n.children) {
		const BtStatus st = TickNode(c, user);
		if (st != BtStatus::Failure) {
			return st;
		}
	}
	return BtStatus::Failure;
}

BtStatus BehaviorTree::Tick(void* user) {
	return TickNode(root_, user);
}

Vec3 SteerSeek(const Vec3 position, const Vec3 target, const float max_speed, const Vec3 velocity) {
	const Vec3 desired = Normalize(target - position) * max_speed;
	return desired - velocity;
}

Vec3 SteerFlee(const Vec3 position, const Vec3 threat, const float max_speed, const Vec3 velocity) {
	return SteerSeek(threat, position, max_speed, velocity);
}

Vec3 SteerArrive(
	const Vec3 position,
	const Vec3 target,
	const float max_speed,
	const float slow_radius,
	const Vec3 velocity) {
	const Vec3 to = target - position;
	const float dist = Length(to);
	const float speed = dist < slow_radius ? max_speed * (dist / std::max(slow_radius, 1.0e-4f)) : max_speed;
	const Vec3 desired = (dist > 1.0e-6f ? to * (1.0f / dist) : Vec3{}) * speed;
	return desired - velocity;
}

int PerceiveInRange(
	const Vec3 origin,
	const float radius,
	const Vec3* positions,
	const int count,
	int* out_ids,
	const int cap) {
	if (positions == nullptr || out_ids == nullptr) {
		return 0;
	}
	const float r2 = radius * radius;
	int written = 0;
	for (int i = 0; i < count && written < cap; ++i) {
		if (LengthSq(positions[i] - origin) <= r2) {
			out_ids[written++] = i;
		}
	}
	return written;
}

} // namespace hyperlite
