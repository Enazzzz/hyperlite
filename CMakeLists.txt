cmake_minimum_required(VERSION 3.24)

project(hyperlite_engine LANGUAGES CXX)

# Keep release builds focused on runtime performance.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HYPERLITE_BUILD_PYTHON_BINDINGS "Build Python extension module" ON)
option(HYPERLITE_ENABLE_CUDA "Build CUDA backend when toolkit is available" ON)
option(HYPERLITE_BUILD_BENCHMARKS "Build benchmark binaries" ON)
option(HYPERLITE_BUILD_TESTS "Build test binaries" ON)
set(HYPERLITE_CUDA_ENABLED OFF)

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
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-O3>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-march=native>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-ffast-math>
	)
endif()

add_library(hyperlite_engine STATIC
	engine/src/core/frame_loop.cpp
	engine/src/platform/win32_window.cpp
	engine/src/backend/cpu_backend.cpp
	engine/src/backend/backend_selector.cpp
	engine/src/backend/cuda_backend.cpp
	engine/src/raster/rasterizer.cpp
)

target_include_directories(hyperlite_engine PUBLIC engine/include)
set_property(TARGET hyperlite_engine PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

if(HYPERLITE_ENABLE_CUDA)
	enable_language(CUDA OPTIONAL)
	if(CMAKE_CUDA_COMPILER)
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
	if(Python3_FOUND)
		Python3_add_library(hyperlite_python MODULE bindings/python/src/module.cpp)
		target_include_directories(hyperlite_python PRIVATE engine/include)
		target_link_libraries(hyperlite_python PRIVATE hyperlite_engine)
		target_compile_definitions(hyperlite_python PRIVATE PY_SSIZE_T_CLEAN)
		set_target_properties(hyperlite_python PROPERTIES OUTPUT_NAME hyperlite PREFIX "")
	else()
		message(WARNING "Python3 development headers not found; skipping bindings.")
	endif()
endif()

if(HYPERLITE_BUILD_BENCHMARKS)
	add_executable(primitive_bench benchmarks/primitive_bench.cpp)
	target_link_libraries(primitive_bench PRIVATE hyperlite_engine)
	set_property(TARGET primitive_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

	add_executable(gpu_scene_bench benchmarks/gpu_scene_bench.cpp)
	target_link_libraries(gpu_scene_bench PRIVATE hyperlite_engine)
	set_property(TARGET gpu_scene_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(HYPERLITE_BUILD_TESTS)
	add_executable(reference_render_tests tests/reference_render_tests.cpp)
	target_link_libraries(reference_render_tests PRIVATE hyperlite_engine)
	set_property(TARGET reference_render_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
