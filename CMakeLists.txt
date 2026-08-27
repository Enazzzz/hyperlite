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
option(HYPERLITE_ENABLE_X11 "Enable X11 windowed present on Linux when libs are found" ON)
set(HYPERLITE_MARCH "native" CACHE STRING "Release -march value on non-MSVC (empty disables; use x86-64 for portable)")
set(HYPERLITE_CUDA_ENABLED OFF)
set(HYPERLITE_HAS_X11 0)

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
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-O3>
		$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-ffast-math>
	)
	if(HYPERLITE_MARCH STREQUAL "")
		message(STATUS "HYPERLITE_MARCH empty: portable scalar/SSE codegen (no -march).")
	else()
		add_compile_options(
			$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:-march=${HYPERLITE_MARCH}>
		)
		message(STATUS "HYPERLITE_MARCH=${HYPERLITE_MARCH} (AVX2 / AVX-512VL opaque fill only when the arch enables those macros).")
	endif()
endif()

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
)

if(WIN32)
	list(APPEND HYPERLITE_ENGINE_SOURCES
		engine/src/platform/win32_window.cpp
		engine/src/platform/dxgi_presenter.cpp
	)
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
# Required so the static archive can link into the Python extension (.so / .pyd).
set_property(TARGET hyperlite_engine PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET hyperlite_engine PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

if(NOT MSVC)
	find_package(OpenMP)
	if(OpenMP_CXX_FOUND)
		target_link_libraries(hyperlite_engine PUBLIC OpenMP::OpenMP_CXX)
	else()
		message(WARNING "OpenMP not found; CPU line batches will not parallelize.")
	endif()
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
	if(Python3_FOUND)
		Python3_add_library(hyperlite_python MODULE bindings/python/src/module.cpp)
		target_include_directories(hyperlite_python PRIVATE engine/include)
		target_link_libraries(hyperlite_python PRIVATE hyperlite_engine)
		target_compile_definitions(hyperlite_python PRIVATE PY_SSIZE_T_CLEAN)
		set_target_properties(hyperlite_python PROPERTIES OUTPUT_NAME hyperlite PREFIX "")
		# Ensure a proper extension suffix (.pyd / .cpython-*.so) for import and setuptools.
		if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
			set_target_properties(hyperlite_python PROPERTIES SUFFIX ".pyd")
		elseif(DEFINED Python3_SOABI AND NOT Python3_SOABI STREQUAL "")
			set_target_properties(hyperlite_python PROPERTIES SUFFIX ".${Python3_SOABI}${CMAKE_SHARED_MODULE_SUFFIX}")
		endif()
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

	add_executable(gpu_blit_bench benchmarks/gpu_blit_bench.cpp)
	target_link_libraries(gpu_blit_bench PRIVATE hyperlite_engine)
	set_property(TARGET gpu_blit_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

	add_executable(cpu_line_bench benchmarks/cpu_line_bench.cpp)
	target_link_libraries(cpu_line_bench PRIVATE hyperlite_engine)
	set_property(TARGET cpu_line_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

	add_executable(cpu_line_3d_bench benchmarks/cpu_line_3d_bench.cpp)
	target_link_libraries(cpu_line_3d_bench PRIVATE hyperlite_engine)
	set_property(TARGET cpu_line_3d_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

	add_executable(cpu_tri_bench benchmarks/cpu_tri_bench.cpp)
	target_link_libraries(cpu_tri_bench PRIVATE hyperlite_engine)
	set_property(TARGET cpu_tri_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

	add_executable(cpu_mesh_bench benchmarks/cpu_mesh_bench.cpp)
	target_link_libraries(cpu_mesh_bench PRIVATE hyperlite_engine)
	set_property(TARGET cpu_mesh_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(HYPERLITE_BUILD_TESTS)
	enable_testing()

	add_executable(reference_render_tests tests/reference_render_tests.cpp)
	target_link_libraries(reference_render_tests PRIVATE hyperlite_engine)
	set_property(TARGET reference_render_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	add_test(NAME reference_render_tests COMMAND reference_render_tests)

	add_executable(headless_smoke_test tests/headless_smoke_test.cpp)
	target_link_libraries(headless_smoke_test PRIVATE hyperlite_engine)
	set_property(TARGET headless_smoke_test PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	add_test(NAME headless_smoke_test COMMAND headless_smoke_test)

	add_executable(depth_wireframe_tests tests/depth_wireframe_tests.cpp)
	target_link_libraries(depth_wireframe_tests PRIVATE hyperlite_engine)
	set_property(TARGET depth_wireframe_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	add_test(NAME depth_wireframe_tests COMMAND depth_wireframe_tests)

	add_executable(depth_tri_tests tests/depth_tri_tests.cpp)
	target_link_libraries(depth_tri_tests PRIVATE hyperlite_engine)
	set_property(TARGET depth_tri_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	add_test(NAME depth_tri_tests COMMAND depth_tri_tests)

	add_executable(mesh_layer_tests tests/mesh_layer_tests.cpp)
	target_link_libraries(mesh_layer_tests PRIVATE hyperlite_engine)
	set_property(TARGET mesh_layer_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	add_test(NAME mesh_layer_tests COMMAND mesh_layer_tests)

	add_executable(mesh_textured_tests tests/mesh_textured_tests.cpp)
	target_link_libraries(mesh_textured_tests PRIVATE hyperlite_engine)
	set_property(TARGET mesh_textured_tests PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	add_test(NAME mesh_textured_tests COMMAND mesh_textured_tests)
endif()
