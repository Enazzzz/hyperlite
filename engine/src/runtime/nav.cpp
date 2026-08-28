#include "engine/runtime/nav.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace hyperlite {

int NavMesh::Locate(const Vec3 p) const {
	int best = -1;
	float best_d = std::numeric_limits<float>::max();
	for (std::size_t i = 0; i < tris_.size(); ++i) {
		const float d = LengthSq(tris_[i].center - p);
		if (d < best_d) {
			best_d = d;
			best = static_cast<int>(i);
		}
	}
	return best;
}

float NavMesh::Heuristic(const int a, const int b) const {
	return Length(tris_[static_cast<std::size_t>(a)].center - tris_[static_cast<std::size_t>(b)].center);
}

void NavMesh::Build(
	const Vec3* vertices,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count) {
	tris_.clear();
	if (vertices == nullptr || indices == nullptr || index_count < 3U) {
		(void)vertex_count;
		return;
	}
	const std::size_t tri_n = index_count / 3U;
	tris_.resize(tri_n);
	for (std::size_t t = 0; t < tri_n; ++t) {
		const std::uint32_t i0 = indices[t * 3U];
		const std::uint32_t i1 = indices[t * 3U + 1U];
		const std::uint32_t i2 = indices[t * 3U + 2U];
		Tri& tri = tris_[t];
		tri.v0 = vertices[i0];
		tri.v1 = vertices[i1];
		tri.v2 = vertices[i2];
		tri.center = (tri.v0 + tri.v1 + tri.v2) * (1.0f / 3.0f);
		tri.n[0] = tri.n[1] = tri.n[2] = -1;
	}
	auto share_edge = [](const Tri& a, const Tri& b) {
		int shared = 0;
		const Vec3* av[3] = {&a.v0, &a.v1, &a.v2};
		const Vec3* bv[3] = {&b.v0, &b.v1, &b.v2};
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				if (LengthSq(*av[i] - *bv[j]) < 1.0e-6f) {
					++shared;
				}
			}
		}
		return shared >= 2;
	};
	for (std::size_t i = 0; i < tris_.size(); ++i) {
		int filled = 0;
		for (std::size_t j = 0; j < tris_.size() && filled < 3; ++j) {
			if (i == j) {
				continue;
			}
			if (share_edge(tris_[i], tris_[j])) {
				tris_[i].n[filled++] = static_cast<int>(j);
			}
		}
	}
}

int NavMesh::FindPath(const Vec3 start, const Vec3 goal, Vec3* out_points, const int cap) const {
	if (out_points == nullptr || cap <= 0 || tris_.empty()) {
		return 0;
	}
	const int s = Locate(start);
	const int g = Locate(goal);
	if (s < 0 || g < 0) {
		return 0;
	}
	if (s == g) {
		out_points[0] = start;
		if (cap > 1) {
			out_points[1] = goal;
			return 2;
		}
		return 1;
	}
	const int n = static_cast<int>(tris_.size());
	std::vector<float> g_cost(static_cast<std::size_t>(n), 1.0e30f);
	std::vector<int> parent(static_cast<std::size_t>(n), -1);
	std::vector<char> closed(static_cast<std::size_t>(n), 0);
	using Node = std::pair<float, int>;
	std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
	g_cost[static_cast<std::size_t>(s)] = 0.0f;
	open.push({Heuristic(s, g), s});
	while (!open.empty()) {
		const int cur = open.top().second;
		open.pop();
		if (closed[static_cast<std::size_t>(cur)]) {
			continue;
		}
		closed[static_cast<std::size_t>(cur)] = 1;
		if (cur == g) {
			break;
		}
		for (int k = 0; k < 3; ++k) {
			const int nxt = tris_[static_cast<std::size_t>(cur)].n[k];
			if (nxt < 0) {
				continue;
			}
			const float step = Length(tris_[static_cast<std::size_t>(cur)].center - tris_[static_cast<std::size_t>(nxt)].center);
			const float cand = g_cost[static_cast<std::size_t>(cur)] + step;
			if (cand < g_cost[static_cast<std::size_t>(nxt)]) {
				g_cost[static_cast<std::size_t>(nxt)] = cand;
				parent[static_cast<std::size_t>(nxt)] = cur;
				open.push({cand + Heuristic(nxt, g), nxt});
			}
		}
	}
	if (parent[static_cast<std::size_t>(g)] < 0 && s != g) {
		return 0;
	}
	std::vector<int> rev;
	for (int c = g; c >= 0; c = parent[static_cast<std::size_t>(c)]) {
		rev.push_back(c);
		if (c == s) {
			break;
		}
	}
	std::reverse(rev.begin(), rev.end());
	int written = 0;
	if (written < cap) {
		out_points[written++] = start;
	}
	for (const int id : rev) {
		if (written >= cap) {
			break;
		}
		out_points[written++] = tris_[static_cast<std::size_t>(id)].center;
	}
	if (written < cap) {
		out_points[written++] = goal;
	}
	return written;
}

} // namespace hyperlite
