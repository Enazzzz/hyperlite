cmake_minimum_required(VERSION 3.24)

# Must be set before project() so Xcode/Clang pick up the deployment target.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
	if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
		set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
	endif()
	# Enable OBJCXX at project() time so .mm files are not compiled as CXX.
	project(hyperlite_engine LANGUAGES CXX OBJCXX)
else()
	project(hyperlite_engine LANGUAGES CXX)
endif()

# Keep release builds focused on runtime performance.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HYPERLITE_BUILD_PYTHON_BINDINGS "Build Python extension module" ON)
option(HYPERLITE_ENABLE_CUDA "Build CUDA backend when toolkit is available" ON)
option(HYPERLITE_BUILD_BENCHMARKS "Build benchmark binaries" ON)
option(HYPERLITE_BUILD_TESTS "Build test binaries" ON)
option(HYPERLITE_ENABLE_X11 "Enable X11 windowed present on Linux when libs are found" ON)
option(HYPERLITE_ENABLE_COCOA "Enable Cocoa windowed present on macOS" ON)
set(HYPERLITE_MARCH "native" CACHE STRING "Release -march value on non-MSVC (empty disables; use x86-64 for portable)")
set(HYPERLITE_CUDA_ENABLED OFF)
set(HYPERLITE_HAS_X11 0)

if(APPLE)
	if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		message(FATAL_ERROR
			"macOS builds require Apple Clang (clang++). "
			"Homebrew GCC cannot compile the Cocoa .mm sources. Unset CXX/CC and reconfigure.")
	endif()
	set(CMAKE_OBJCXX_STANDARD 20)
	set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
	set(CMAKE_OBJCXX_EXTENSIONS OFF)
	# python.org installers are universal2. Building both slices with -march=native
	# (or mixing arm64 host flags into x86_64) fails on Apple Silicon. Default to host.
	if(NOT CMAKE_OSX_ARCHITECTURES)
		if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
			set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Hyperlite host architecture (not universal)")
		elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64")
			set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Hyperlite host architecture (not universal)")
		endif()
	endif()
endif()

if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	add_compile_options(
		$<$<COMPILE_LANGUAGE:CXX>:/W4>
		$<$<COMPILE_LANGUAGE:CXX>:/permissive->
		$<$<COMPILE_LANGUAGE:CXX>:/EHsc>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Oi>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Ot>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Gw>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Gy>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/fp:fast>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/arch:AVX2>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/openmp>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/GL>
	)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
	add_link_options(
		$<$<CONFIG:Release>:/LTCG>
		$<$<CONFIG:Release>:/OPT:REF>
		$<$<CONFIG:Release>:/OPT:ICF>
	)
else()
	add_compile_options(
		$<$<COMPILE_LANGUAGE:CXX>:-Wall>
		$<$<COMPILE_LANGUAGE:CXX>:-Wextra>
		$<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
		$<$<COMPILE_LANGUAGE:OBJCXX>:-Wall>
		$<$<COMPILE_LANGUAGE:OBJCXX>:-Wextra>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-O3>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-ffast-math>
		$<$<AND:$<COMPILE_LANGUAGE:OBJCXX>,$<CONFIG:Release>>:-O3>
		$<$<AND:$<COMPILE_LANGUAGE:OBJCXX>,$<CONFIG:Release>>:-ffast-math>
	)
	# -march=native is rejected on many Apple Silicon Xcodes ("unsupported option
	# '-march=' for target 'arm64-apple-darwin'"). Raster on ARM is scalar anyway.
	set(HYPERLITE_MARCH_EFFECTIVE "${HYPERLITE_MARCH}")
	if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
		message(STATUS "Apple Silicon: not passing -march=${HYPERLITE_MARCH} (host Clang defaults are used).")
		set(HYPERLITE_MARCH_EFFECTIVE "")
	endif()
	if(NOT HYPERLITE_MARCH_EFFECTIVE STREQUAL "")
		include(CheckCXXCompilerFlag)
		check_cxx_compiler_flag("-march=${HYPERLITE_MARCH_EFFECTIVE}" HYPERLITE_HAVE_MARCH)
		if(NOT HYPERLITE_HAVE_MARCH)
			message(STATUS "Compiler rejects -march=${HYPERLITE_MARCH_EFFECTIVE}; building without it.")
			set(HYPERLITE_MARCH_EFFECTIVE "")
		endif()
	endif()
	if(HYPERLITE_MARCH_EFFECTIVE STREQUAL "")
		message(STATUS "HYPERLITE_MARCH empty: portable scalar/SSE codegen (no -march).")
	else()
		add_compile_options(
			$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-march=${HYPERLITE_MARCH_EFFECTIVE}>
			$<$<AND:$<COMPILE_LANGUAGE:OBJCXX>,$<CONFIG:Release>>:-march=${HYPERLITE_MARCH_EFFECTIVE}>
		)
		message(STATUS "HYPERLITE_MARCH=${HYPERLITE_MARCH_EFFECTIVE} (AVX2 / AVX-512VL opaque fill only when the arch enables those macros).")
	endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT HYPERLITE_IPO_OK OUTPUT HYPERLITE_IPO_MSG)
if(NOT HYPERLITE_IPO_OK)
	message(STATUS "IPO/LTO not used: ${HYPERLITE_IPO_MSG}")
endif()

function(hyperlite_maybe_ipo target)
	if(HYPERLITE_IPO_OK)
		set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	endif()
endfunction()

set(HYPERLITE_ENGINE_SOURCES
	engine/src/core/frame_loop.cpp
	engine/src/core/atlas_store.cpp
	engine/src/core/mesh_store.cpp
	engine/src/core/retained_layer.cpp
	engine/src/core/command_buffer_sort.cpp
	engine/src/platform/platform_window.cpp
	engine/src/platform/headless_window.cpp
	engine/src/backend/cpu_backend.cpp
	engine/src/backend/backend_selector.cpp
	engine/src/backend/cuda_backend.cpp
	engine/src/raster/rasterizer.cpp
	engine/src/runtime/job_system.cpp
	engine/src/runtime/events.cpp
	engine/src/runtime/input_runtime.cpp
	engine/src/runtime/transform.cpp
	engine/src/runtime/camera.cpp
	engine/src/runtime/world.cpp
	engine/src/runtime/resources.cpp
	engine/src/runtime/culling.cpp
	engine/src/runtime/lighting.cpp
	engine/src/runtime/particles.cpp
	engine/src/runtime/animation.cpp
	engine/src/runtime/collision.cpp
	engine/src/runtime/physics.cpp
	engine/src/runtime/spatial.cpp
	engine/src/runtime/streaming.cpp
	engine/src/runtime/nav.cpp
	engine/src/runtime/ai.cpp
	engine/src/runtime/audio.cpp
	engine/src/runtime/text.cpp
	engine/src/runtime/ui.cpp
	engine/src/runtime/save.cpp
	engine/src/runtime/assets.cpp
	engine/src/runtime/profiler.cpp
	engine/src/runtime/debug_draw.cpp
	engine/src/runtime/shader.cpp
	engine/src/runtime/game.cpp
)

if(WIN32)
	list(APPEND HYPERLITE_ENGINE_SOURCES
		engine/src/platform/win32_window.cpp
		engine/src/platform/dxgi_presenter.cpp
	)
elseif(APPLE)
	if(HYPERLITE_ENABLE_COCOA)
		list(APPEND HYPERLITE_ENGINE_SOURCES
			engine/src/platform/macos_window.mm
			engine/src/runtime/macos_gamepad.mm
			engine/src/runtime/macos_audio.mm
		)
		set_source_files_properties(
			engine/src/platform/macos_window.mm
			engine/src/runtime/macos_gamepad.mm
			engine/src/runtime/macos_audio.mm
			PROPERTIES
				COMPILE_FLAGS "-fobjc-arc"
				LANGUAGE OBJCXX
		)
		message(STATUS "Cocoa present enabled (AppKit blit of host RGBA8; no Metal raster).")
	else()
		message(STATUS "HYPERLITE_ENABLE_COCOA=OFF; building headless-only present path.")
	endif()
else()
	if(HYPERLITE_ENABLE_X11)
		find_package(X11)
		if(X11_FOUND AND X11_Xext_FOUND)
			set(HYPERLITE_HAS_X11 1)
			list(APPEND HYPERLITE_ENGINE_SOURCES engine/src/platform/x11_window.cpp)
			message(STATUS "X11 present enabled (libX11 + libXext).")
		else()
			message(STATUS "X11 / Xext not found; building headless-only present path.")
		endif()
	else()
		message(STATUS "HYPERLITE_ENABLE_X11=OFF; building headless-only present path.")
	endif()
endif()

add_library(hyperlite_engine STATIC ${HYPERLITE_ENGINE_SOURCES})

target_include_directories(hyperlite_engine PUBLIC engine/include)
target_compile_definitions(hyperlite_engine PUBLIC HYPERLITE_HAS_X11=${HYPERLITE_HAS_X11})
if(APPLE AND HYPERLITE_ENABLE_COCOA)
	target_compile_definitions(hyperlite_engine PUBLIC HYPERLITE_HAS_COCOA=1)
else()
	target_compile_definitions(hyperlite_engine PUBLIC HYPERLITE_HAS_COCOA=0)
endif()
# Required so the static archive can link into the Python extension (.so / .pyd).
set_property(TARGET hyperlite_engine PROPERTY POSITION_INDEPENDENT_CODE ON)
hyperlite_maybe_ipo(hyperlite_engine)

if(NOT MSVC)
	# Homebrew libomp is often found with flags that then fail to compile/link
	# under Apple Clang. Probe before attaching the target.
	if(APPLE AND NOT OpenMP_ROOT)
		execute_process(
			COMMAND brew --prefix libomp
			OUTPUT_VARIABLE _hyperlite_libomp
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET
		)
		if(_hyperlite_libomp AND EXISTS "${_hyperlite_libomp}")
			set(OpenMP_ROOT "${_hyperlite_libomp}")
		endif()
	endif()
	find_package(OpenMP)
	if(OpenMP_CXX_FOUND)
		include(CheckCXXSourceCompiles)
		set(_hyperlite_omp_saved_flags "${CMAKE_REQUIRED_FLAGS}")
		set(_hyperlite_omp_saved_libs "${CMAKE_REQUIRED_LIBRARIES}")
		set(CMAKE_REQUIRED_FLAGS "${OpenMP_CXX_FLAGS}")
		set(CMAKE_REQUIRED_LIBRARIES OpenMP::OpenMP_CXX)
		check_cxx_source_compiles("#include <omp.h>\nint main() { return omp_get_max_threads(); }\n" HYPERLITE_OPENMP_COMPILES)
		set(CMAKE_REQUIRED_FLAGS "${_hyperlite_omp_saved_flags}")
		set(CMAKE_REQUIRED_LIBRARIES "${_hyperlite_omp_saved_libs}")
		if(HYPERLITE_OPENMP_COMPILES)
			target_link_libraries(hyperlite_engine PUBLIC OpenMP::OpenMP_CXX)
		else()
			message(STATUS "OpenMP was found but does not compile; CPU line batches stay serial.")
		endif()
	else()
		if(APPLE)
			message(STATUS "OpenMP not found (typical with Apple Clang). CPU line batches stay serial. Optional: brew install libomp")
		else()
			message(WARNING "OpenMP not found; CPU line batches will not parallelize.")
		endif()
	endif()
endif()

if(APPLE AND HYPERLITE_ENABLE_COCOA)
	target_link_libraries(hyperlite_engine PUBLIC
		"-framework Cocoa"
		"-framework QuartzCore"
		"-framework CoreVideo"
		"-framework CoreGraphics"
		"-framework GameController"
		"-framework AudioToolbox"
		"-framework CoreAudio"
		"-framework ApplicationServices"
	)
endif()

if(HYPERLITE_HAS_X11)
	target_include_directories(hyperlite_engine PRIVATE ${X11_INCLUDE_DIR})
	target_link_libraries(hyperlite_engine PUBLIC ${X11_LIBRARIES} ${X11_Xext_LIB})
endif()

if(HYPERLITE_ENABLE_CUDA)
	include(CheckLanguage)
	check_language(CUDA)
	if(CMAKE_CUDA_COMPILER)
		enable_language(CUDA)
		set(HYPERLITE_CUDA_ENABLED ON)
		find_package(CUDAToolkit REQUIRED)
		target_sources(hyperlite_engine PRIVATE
			engine/src/backend/cuda_context.cu
			engine/src/backend/cuda_spiro_scene.cu
		)
		target_compile_definitions(hyperlite_engine PUBLIC HYPERLITE_HAS_CUDA=1)
		# Propagate the CUDA runtime to every consumer of the static library so
		# host objects that call cudaMalloc/cudaMemcpy/etc resolve at link time.
		target_link_libraries(hyperlite_engine PUBLIC CUDA::cudart)
		set_target_properties(hyperlite_engine PROPERTIES
			CUDA_STANDARD 17
			CUDA_STANDARD_REQUIRED ON
			CUDA_SEPARABLE_COMPILATION OFF
			CUDA_RUNTIME_LIBRARY Hybrid
			CUDA_ARCHITECTURES native
		)
	else()
		message(STATUS "CUDA toolkit not found; building CPU-only backend.")
		target_compile_definitions(hyperlite_engine PUBLIC HYPERLITE_HAS_CUDA=0)
	endif()
else()
	target_compile_definitions(hyperlite_engine PUBLIC HYPERLITE_HAS_CUDA=0)
endif()

if(HYPERLITE_BUILD_PYTHON_BINDINGS)
	find_package(Python3 COMPONENTS Interpreter Development.Module)
	if(NOT Python3_Development.Module_FOUND)
		message(FATAL_ERROR
			"Python development headers (Python.h) not found for '${Python3_EXECUTABLE}'. "
			"On macOS, /usr/bin/python3 has no headers — use Homebrew (`brew install python`) "
			"or python.org, then: python3 -m venv .venv && .venv/bin/pip install . "
			"Or configure with -DHYPERLITE_BUILD_PYTHON_BINDINGS=OFF for C++-only.")
	endif()
	Python3_add_library(hyperlite_python MODULE bindings/python/src/module.cpp)
	target_include_directories(hyperlite_python PRIVATE engine/include)
	target_link_libraries(hyperlite_python PRIVATE hyperlite_engine)
	target_compile_definitions(hyperlite_python PRIVATE PY_SSIZE_T_CLEAN)
	hyperlite_maybe_ipo(hyperlite_python)
	set_target_properties(hyperlite_python PROPERTIES OUTPUT_NAME hyperlite PREFIX "")
	# Ensure a proper extension suffix (.pyd / .cpython-*.so) for import and setuptools.
	if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
		set_target_properties(hyperlite_python PROPERTIES SUFFIX ".pyd")
	elseif(DEFINED Python3_SOABI AND NOT Python3_SOABI STREQUAL "")
		set_target_properties(hyperlite_python PROPERTIES SUFFIX ".${Python3_SOABI}${CMAKE_SHARED_MODULE_SUFFIX}")
	endif()
endif()

if(HYPERLITE_BUILD_BENCHMARKS)
	add_executable(primitive_bench benchmarks/primitive_bench.cpp)
	target_link_libraries(primitive_bench PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(primitive_bench)

	add_executable(gpu_scene_bench benchmarks/gpu_scene_bench.cpp)
	target_link_libraries(gpu_scene_bench PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(gpu_scene_bench)

	add_executable(gpu_blit_bench benchmarks/gpu_blit_bench.cpp)
	target_link_libraries(gpu_blit_bench PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(gpu_blit_bench)

	add_executable(cpu_line_bench benchmarks/cpu_line_bench.cpp)
	target_link_libraries(cpu_line_bench PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(cpu_line_bench)

	add_executable(cpu_line_3d_bench benchmarks/cpu_line_3d_bench.cpp)
	target_link_libraries(cpu_line_3d_bench PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(cpu_line_3d_bench)

	add_executable(cpu_tri_bench benchmarks/cpu_tri_bench.cpp)
	target_link_libraries(cpu_tri_bench PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(cpu_tri_bench)

	add_executable(cpu_mesh_bench benchmarks/cpu_mesh_bench.cpp)
	target_link_libraries(cpu_mesh_bench PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(cpu_mesh_bench)
endif()

if(HYPERLITE_BUILD_TESTS)
	enable_testing()

	add_executable(reference_render_tests tests/reference_render_tests.cpp)
	target_link_libraries(reference_render_tests PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(reference_render_tests)
	add_test(NAME reference_render_tests COMMAND reference_render_tests)

	add_executable(headless_smoke_test tests/headless_smoke_test.cpp)
	target_link_libraries(headless_smoke_test PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(headless_smoke_test)
	add_test(NAME headless_smoke_test COMMAND headless_smoke_test)

	add_executable(depth_wireframe_tests tests/depth_wireframe_tests.cpp)
	target_link_libraries(depth_wireframe_tests PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(depth_wireframe_tests)
	add_test(NAME depth_wireframe_tests COMMAND depth_wireframe_tests)

	add_executable(depth_tri_tests tests/depth_tri_tests.cpp)
	target_link_libraries(depth_tri_tests PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(depth_tri_tests)
	add_test(NAME depth_tri_tests COMMAND depth_tri_tests)

	add_executable(mesh_layer_tests tests/mesh_layer_tests.cpp)
	target_link_libraries(mesh_layer_tests PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(mesh_layer_tests)
	add_test(NAME mesh_layer_tests COMMAND mesh_layer_tests)

	add_executable(mesh_textured_tests tests/mesh_textured_tests.cpp)
	target_link_libraries(mesh_textured_tests PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(mesh_textured_tests)
	add_test(NAME mesh_textured_tests COMMAND mesh_textured_tests)

	add_executable(game_runtime_tests tests/game_runtime_tests.cpp)
	target_link_libraries(game_runtime_tests PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(game_runtime_tests)
	add_test(NAME game_runtime_tests COMMAND game_runtime_tests)

	add_executable(platform_window_tests tests/platform_window_tests.cpp)
	target_link_libraries(platform_window_tests PRIVATE hyperlite_engine)
	hyperlite_maybe_ipo(platform_window_tests)
	add_test(NAME platform_window_tests COMMAND platform_window_tests)
endif()
