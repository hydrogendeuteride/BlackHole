cmake_minimum_required(VERSION 3.20)

project(blackhole LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VULKAN_ENGINE_OUTPUT_TO_SOURCE_ROOT "Place executables and shared libraries under the source tree bin/ and lib/ directories" ON)
option(VULKAN_ENGINE_QUIET_BUILD "Reduce per-file build progress messages where the CMake generator supports it" ON)

if(VULKAN_ENGINE_QUIET_BUILD)
    set(CMAKE_RULE_MESSAGES OFF CACHE BOOL "Report per-rule build messages" FORCE)
    set_property(GLOBAL PROPERTY RULE_MESSAGES OFF)
endif()

if(WIN32)
    if(DEFINED ENV{VULKAN_SDK})
        file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" _VULKAN_SDK)
        set(Vulkan_INCLUDE_DIR "${_VULKAN_SDK}/Include")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(Vulkan_LIBRARY "${_VULKAN_SDK}/Lib/vulkan-1.lib")
        else()
            set(Vulkan_LIBRARY "${_VULKAN_SDK}/Lib32/vulkan-1.lib")
        endif()
    elseif(EXISTS "C:/VulkanSDK/1.3.296.0/Include")
        # Fallback for a common SDK install path; update if your SDK version differs.
        set(Vulkan_INCLUDE_DIR "C:/VulkanSDK/1.3.296.0/Include")
        set(Vulkan_LIBRARY "C:/VulkanSDK/1.3.296.0/Lib/vulkan-1.lib")
    endif()
endif()

find_package(Vulkan REQUIRED COMPONENTS glslangValidator)

# By default, keep runtime artifacts in the legacy source-root bin/ layout.
# This matches the previous workflow and keeps run paths stable, while still
# allowing per-build-tree outputs when needed.
if(VULKAN_ENGINE_OUTPUT_TO_SOURCE_ROOT)
    set(_vulkan_engine_out_root "${PROJECT_SOURCE_DIR}")
else()
    set(_vulkan_engine_out_root "${PROJECT_BINARY_DIR}")
endif()
if(CMAKE_CONFIGURATION_TYPES)
    set(_vulkan_engine_cfg_dir "/$<CONFIG>")
else()
    set(_vulkan_engine_cfg_dir "")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_vulkan_engine_out_root}/bin${_vulkan_engine_cfg_dir}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_vulkan_engine_out_root}/bin${_vulkan_engine_cfg_dir}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_vulkan_engine_out_root}/lib${_vulkan_engine_cfg_dir}")
unset(_vulkan_engine_out_root)
unset(_vulkan_engine_cfg_dir)

# Third-party deps are vendored; keep builds offline-friendly by default.
# BVH2's CMake enables tests by default, which would FetchContent googletest.
set(BVH2_ENABLE_TESTS OFF CACHE BOOL "Disable BVH2 tests (offline builds)" FORCE)

option(VULKAN_ENGINE_USE_JOLT "Build with Jolt Physics (vendored in third_party/JoltPhysics)" ON)
option(VULKAN_ENGINE_JOLT_DOUBLE_PRECISION "Build Jolt with double precision math (JPH_DOUBLE_PRECISION)" ON)
option(VULKAN_ENGINE_BUILD_APP "Include the blackhole sample executable in the default build" ON)
option(VULKAN_ENGINE_BUILD_TESTS "Build unit tests" OFF)

option(VULKAN_ENGINE_NATIVE_CPU "Optimize x86 code for the build host instead of the AVX2 target" OFF)

add_library(vulkan_engine_cpu_options INTERFACE)
add_library(vulkan_engine::cpu_options ALIAS vulkan_engine_cpu_options)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$")
    if(MSVC)
        if(VULKAN_ENGINE_NATIVE_CPU AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(vulkan_engine_cpu_options INTERFACE /clang:-march=native)
        else()
            target_compile_options(vulkan_engine_cpu_options INTERFACE /arch:AVX2)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        if(VULKAN_ENGINE_NATIVE_CPU)
            target_compile_options(vulkan_engine_cpu_options INTERFACE -march=native)
        else()
            target_compile_options(vulkan_engine_cpu_options INTERFACE
                -mavx2 -mbmi -mpopcnt -mlzcnt -mf16c -mfma)
        endif()
    endif()
endif()

if(VULKAN_ENGINE_USE_JOLT)
    # The shared CPU options let Jolt detect the enabled ISA from compiler macros.
    foreach(_JOLT_FEATURE SSE4_1 SSE4_2 AVX AVX2 AVX512 LZCNT TZCNT F16C FMADD)
        set(USE_${_JOLT_FEATURE} OFF CACHE BOOL "Let compiler flags select Jolt ${_JOLT_FEATURE}" FORCE)
    endforeach()
endif()

add_subdirectory(third_party)

if(VULKAN_ENGINE_USE_JOLT AND TARGET Jolt AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86)$" AND
   CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Jolt's AVX double-precision types can emit aligned 32-byte stack stores in
    # shape casts. SysV callers only guarantee 16-byte stack alignment.
    target_compile_options(Jolt PUBLIC -mstackrealign)
endif()

if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    foreach(_SDL_TARGET IN ITEMS SDL2 SDL2-static)
        if(TARGET ${_SDL_TARGET})
            target_link_options(${_SDL_TARGET} PRIVATE /DEFAULTLIB:ucrt.lib /DEFAULTLIB:vcruntime.lib)
            # clang-cl can conflict on SDL_endian.h _m_prefetch shim; mark pseudo-header as present for consumers.
            target_compile_definitions(${_SDL_TARGET} INTERFACE __PRFCHWINTRIN_H=1)
        endif()
    endforeach()
endif()

add_subdirectory(src)

if (VULKAN_ENGINE_BUILD_TESTS AND EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

option(VULKAN_ENGINE_BUILD_SHADERS "Compile GLSL shaders to SPIR-V at build time" ON)
set(VULKAN_ENGINE_SHADER_OUTPUT_DIR "" CACHE PATH "Directory for generated SPIR-V shaders")

if(VULKAN_ENGINE_BUILD_SHADERS)
    if(NOT VULKAN_ENGINE_SHADER_OUTPUT_DIR)
        set(VULKAN_ENGINE_SHADER_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    elseif(NOT IS_ABSOLUTE "${VULKAN_ENGINE_SHADER_OUTPUT_DIR}")
        set(VULKAN_ENGINE_SHADER_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/${VULKAN_ENGINE_SHADER_OUTPUT_DIR}")
    endif()

    set(GLSL_VALIDATOR "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
    if(NOT GLSL_VALIDATOR)
        find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin)
    endif()

    if(NOT GLSL_VALIDATOR)
        message(FATAL_ERROR "glslangValidator not found. Install the Vulkan SDK or disable shader compilation with -DVULKAN_ENGINE_BUILD_SHADERS=OFF.")
    endif()

    file(GLOB_RECURSE GLSL_SOURCE_FILES
        "${PROJECT_SOURCE_DIR}/shaders/*.frag"
        "${PROJECT_SOURCE_DIR}/shaders/*.vert"
        "${PROJECT_SOURCE_DIR}/shaders/*.comp"
    )
    file(GLOB_RECURSE GLSL_INCLUDE_FILES
        "${PROJECT_SOURCE_DIR}/shaders/*.glsl"
    )

    set(SPIRV_BINARY_FILES "")
    foreach(GLSL IN LISTS GLSL_SOURCE_FILES)
        file(RELATIVE_PATH GLSL_REL "${PROJECT_SOURCE_DIR}/shaders" "${GLSL}")
        set(SPIRV "${VULKAN_ENGINE_SHADER_OUTPUT_DIR}/${GLSL_REL}.spv")
        get_filename_component(SPIRV_DIR "${SPIRV}" DIRECTORY)
        add_custom_command(
            OUTPUT "${SPIRV}"
            # Force SPIR-V 1.5 output for broader driver compatibility (some drivers crash on SPIR-V 1.6 modules).
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPIRV_DIR}"
            COMMAND "${GLSL_VALIDATOR}" -V --target-env vulkan1.2 --target-env spirv1.5
                    "-I${PROJECT_SOURCE_DIR}/shaders"
                    "${GLSL}" -o "${SPIRV}"
            DEPENDS "${GLSL}" ${GLSL_INCLUDE_FILES}
            VERBATIM
        )
        list(APPEND SPIRV_BINARY_FILES "${SPIRV}")
    endforeach()

    add_custom_target(shaders_spirv DEPENDS ${SPIRV_BINARY_FILES})
    if (TARGET blackhole)
        add_dependencies(blackhole shaders_spirv)
    endif()
endif()
