#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * CPU material bytecode. Python (or C++) emits opcodes; Game evaluates them natively.
 *
 * No GPU API. SIMD-friendly scalar lanes; callers can evaluate 8 vertices in a loop.
 */
enum class ShadeOp : std::uint8_t {
	PushConst = 1,
	PushUv = 2,
	PushNdotL = 3,
	PushColor = 4,
	Mul = 5,
	Add = 6,
	Saturate = 7,
	SampleNearest = 8,
	Output = 9,
};

struct ShadeInst {
	ShadeOp op = ShadeOp::PushConst;
	float imm = 0.0f;
};

struct ShadeInput {
	Vec3 position{};
	Vec3 normal{0.0f, 1.0f, 0.0f};
	Vec2 uv{};
	Vec3 light_dir{0.0f, 1.0f, 0.0f};
	Vec3 albedo{1.0f, 1.0f, 1.0f};
	const std::uint32_t* atlas = nullptr;
	int atlas_w = 0;
	int atlas_h = 0;
};

/**
 * Compile a small stack VM and evaluate RGB in [0,1].
 */
class CpuShader {
public:
	int Compile(const ShadeInst* insts, const int count);
	Vec3 Evaluate(const int shader_id, const ShadeInput& in) const;

	/**
	 * Batch Evaluate into out_rgb (count * 3).
	 */
	void EvaluateBatch(const int shader_id, const ShadeInput* in, const std::size_t count, float* out_rgb) const;

	int Count() const { return static_cast<int>(shaders_.size()); }

private:
	std::vector<std::vector<ShadeInst>> shaders_{};
};

} // namespace hyperlite
