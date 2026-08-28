#include "engine/runtime/shader.hpp"

#include <algorithm>
#include <cmath>

namespace hyperlite {

int CpuShader::Compile(const ShadeInst* insts, const int count) {
	if (insts == nullptr || count <= 0) {
		return -1;
	}
	shaders_.emplace_back(insts, insts + count);
	return static_cast<int>(shaders_.size()) - 1;
}

Vec3 CpuShader::Evaluate(const int shader_id, const ShadeInput& in) const {
	if (shader_id < 0 || static_cast<std::size_t>(shader_id) >= shaders_.size()) {
		return in.albedo;
	}
	float stack[16];
	int sp = 0;
	Vec3 out = in.albedo;
	const auto push = [&](const float v) {
		if (sp < 16) {
			stack[sp++] = v;
		}
	};
	const auto pop = [&]() {
		return sp > 0 ? stack[--sp] : 0.0f;
	};
	for (const ShadeInst& op : shaders_[static_cast<std::size_t>(shader_id)]) {
		switch (op.op) {
		case ShadeOp::PushConst:
			push(op.imm);
			break;
		case ShadeOp::PushUv:
			push(in.uv.x);
			push(in.uv.y);
			break;
		case ShadeOp::PushNdotL:
			push(std::max(0.0f, Dot(Normalize(in.normal), Normalize(in.light_dir))));
			break;
		case ShadeOp::PushColor:
			push(in.albedo.x);
			push(in.albedo.y);
			push(in.albedo.z);
			break;
		case ShadeOp::Mul: {
			const float b = pop();
			const float a = pop();
			push(a * b);
			break;
		}
		case ShadeOp::Add: {
			const float b = pop();
			const float a = pop();
			push(a + b);
			break;
		}
		case ShadeOp::Saturate:
			push(Clamp(pop(), 0.0f, 1.0f));
			break;
		case ShadeOp::SampleNearest: {
			float v = pop();
			float u = pop();
			if (in.atlas != nullptr && in.atlas_w > 0 && in.atlas_h > 0) {
				u = Clamp(u, 0.0f, 1.0f);
				v = Clamp(v, 0.0f, 1.0f);
				const int x = std::min(in.atlas_w - 1, static_cast<int>(u * static_cast<float>(in.atlas_w)));
				const int y = std::min(in.atlas_h - 1, static_cast<int>(v * static_cast<float>(in.atlas_h)));
				const std::uint32_t p = in.atlas[static_cast<std::size_t>(y) * static_cast<std::size_t>(in.atlas_w) + static_cast<std::size_t>(x)];
				push(static_cast<float>(p & 255u) / 255.0f);
				push(static_cast<float>((p >> 8) & 255u) / 255.0f);
				push(static_cast<float>((p >> 16) & 255u) / 255.0f);
			} else {
				push(1.0f);
				push(1.0f);
				push(1.0f);
			}
			break;
		}
		case ShadeOp::Output: {
			const float b = pop();
			const float g = pop();
			const float r = pop();
			out = {r, g, b};
			break;
		}
		default:
			break;
		}
	}
	return out;
}

void CpuShader::EvaluateBatch(const int shader_id, const ShadeInput* in, const std::size_t count, float* out_rgb) const {
	if (in == nullptr || out_rgb == nullptr) {
		return;
	}
	for (std::size_t i = 0; i < count; ++i) {
		const Vec3 c = Evaluate(shader_id, in[i]);
		out_rgb[i * 3U] = c.x;
		out_rgb[i * 3U + 1U] = c.y;
		out_rgb[i * 3U + 2U] = c.z;
	}
}

} // namespace hyperlite
