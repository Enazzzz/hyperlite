#include "engine/engine.hpp"

#include <stdexcept>

namespace hyperlite {

Engine::Engine(const int width, const int height, const BackendKind backend_kind, std::string title)
	: framebuffer_(width, height),
	  backend_(CreateBackend(backend_kind)) {
	command_buffer_.Reserve(1U << 14U);
	if (!backend_) {
		throw std::runtime_error("Failed to create rendering backend.");
	}
	backend_->EnsureSized(width, height);
#ifdef _WIN32
	window_ = std::make_unique<Win32Window>(width, height, std::move(title));
#else
	(void)title;
#endif
}

void Engine::BeginFrame() {
	command_buffer_.Reset();
}

void Engine::PushCommand(const DrawCommand command) {
	command_buffer_.Push(command);
}

void Engine::EndFrame() {
	backend_->Render(command_buffer_, framebuffer_);
}

void Engine::Present() {
	if (pipelined_) {
		const std::uint8_t* ready = backend_->PresentPipelined(framebuffer_.SizeBytes());
#ifdef _WIN32
		if (ready != nullptr && window_) {
			window_->PresentRaw(ready, framebuffer_.Width(), framebuffer_.Height());
		}
#else
		(void)ready;
#endif
		return;
	}
	backend_->ReadbackToHost(framebuffer_);
#ifdef _WIN32
	if (window_) {
		window_->Present(framebuffer_);
	}
#endif
}

bool Engine::SupportsGpuScene() const {
	return backend_->SupportsGpuScene();
}

void Engine::SetPipelined(const bool enabled) {
	pipelined_ = enabled;
	backend_->SetPipelined(enabled);
}

void Engine::ClearGpu(const std::uint32_t packed_color) {
	backend_->ClearDevice(packed_color);
}

int Engine::SpiroSceneGpu(const int width, const int height, const int instances, const int segments, const double phase, const double dt) {
	return backend_->SpiroSceneDevice(width, height, instances, segments, phase, dt);
}

int Engine::SpiroSceneFrameGpu(const int width, const int height, const int instances, const int segments, const double phase, const double dt, const std::uint32_t clear_packed) {
	return backend_->SpiroSceneFrameGraphed(width, height, instances, segments, phase, dt, clear_packed);
}

int Engine::SpiroSceneFrameDirectGpu(
	const int width,
	const int height,
	const int instances,
	const int segments,
	const double phase,
	const double dt,
	const std::uint32_t clear_packed) {
	return backend_->SpiroSceneFrameDirect(width, height, instances, segments, phase, dt, clear_packed);
}

int Engine::TickGpuSpiro(
	const int width,
	const int height,
	const int instances,
	const int segments,
	const double phase,
	const double dt,
	const std::uint32_t clear_packed) {
	PollEvents();
	const int draws = backend_->SpiroSceneFrameDirect(width, height, instances, segments, phase, dt, clear_packed);
	Present();
	return draws;
}

GpuTimings Engine::GpuTimingsLast() const {
	return backend_->LastTimings();
}

void Engine::PollEvents() {
#ifdef _WIN32
	if (window_) {
		window_->PollEvents(input_state_);
	}
#endif
}

const InputState& Engine::GetInputState() const {
	return input_state_;
}

std::string_view Engine::BackendName() const {
	return backend_->Name();
}

bool Engine::IsRunning() const {
#ifdef _WIN32
	return window_ && window_->IsAlive() && !input_state_.quit_requested;
#else
	return !input_state_.quit_requested;
#endif
}

FrameBuffer& Engine::MutableFrameBuffer() {
	return framebuffer_;
}

} // namespace hyperlite
