
# Aggregate third-party dependencies for core/ast; not a standalone library.
add_library(ocarina-ext INTERFACE)
set_target_properties(ocarina-ext PROPERTIES FOLDER "ext")

add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE xxHash)
target_compile_definitions(xxhash INTERFACE XXH_INLINE_ALL)
set_target_properties(xxhash PROPERTIES
        UNITY_BUILD OFF
        FOLDER "ext")

set(FMT_OS OFF CACHE BOOL "" FORCE)
#add_subdirectory(fmt)
#target_compile_definitions(fmt-header-only INTERFACE
#        FMT_CONSTEVAL=constexpr
#        FMT_EXCEPTIONS=0
#        FMT_HEADER_ONLY=1
#        FMT_USE_NOEXCEPT=1)
#target_include_directories(fmt-header-only INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fmt/include)

#target_link_libraries(ocarina-ext PUBLIC fmt::fmt-header-only)

add_subdirectory(EASTL)
target_include_directories(EASTL PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/EASTL/include)
target_link_libraries(ocarina-ext INTERFACE EASTL)


add_subdirectory(tinyexr)

if(NOT TARGET spdlog)
    add_subdirectory(spdlog)
endif()
set_target_properties(spdlog PROPERTIES
        UNITY_BUILD FALSE
        FOLDER "ext")
#target_link_libraries(spdlog PUBLIC fmt::fmt-header-only)
target_link_libraries(ocarina-ext INTERFACE spdlog)
target_include_directories(ocarina-ext INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include/spdlog)

add_subdirectory(stblib)

add_subdirectory(SDL)
set_property(TARGET SDL_uclibc PROPERTY FOLDER "ext/SDL3")
set_property(TARGET SDL3_test PROPERTY FOLDER "ext/SDL3")
set_property(TARGET SDL3-shared PROPERTY FOLDER "ext/SDL3")

# Shared ImGui core — used by framework (SDL backend) and vulkan backend (Vulkan backend).
# Keeps ocarina-backend-vulkan from linking ocarina-framework.
file(GLOB OCARINA_IMGUI_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/imgui/*.cpp")
add_library(ocarina-imgui SHARED ${OCARINA_IMGUI_CORE_SOURCES})
set_target_properties(ocarina-imgui PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ext")
target_include_directories(ocarina-imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends)
if(WIN32)
    target_compile_definitions(ocarina-imgui
            PRIVATE "IMGUI_API=__declspec(dllexport)"
            INTERFACE "IMGUI_API=__declspec(dllimport)")
endif()

# Tracy profiler — https://github.com/wolfpld/tracy
if(OCARINA_ENABLE_TRACY)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tracy/CMakeLists.txt")
        message(FATAL_ERROR
            "OCARINA_ENABLE_TRACY is ON but Tracy sources are missing.\n"
            "Run: git submodule update --init src/ext/tracy\n"
            "Or see src/ext/tracy/INTEGRATION.md for manual setup.")
    endif()

    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_STATIC OFF CACHE BOOL "" FORCE)
    add_subdirectory(tracy EXCLUDE_FROM_ALL)
    set_target_properties(TracyClient PROPERTIES FOLDER "ext/tracy")
    target_link_libraries(ocarina-ext INTERFACE Tracy::TracyClient)
    target_compile_definitions(ocarina-ext INTERFACE OCARINA_ENABLE_TRACY=1)
endif()

cmake_policy(SET CMP0148 NEW)

if (${OCARINA_PY})
    add_subdirectory(pybind11)
    find_package(Python REQUIRED COMPONENTS Interpreter Development)
    include_directories(${PYTHON_INCLUDE_DIRS})
    link_directories(${PYTHON_LIBRARIES})
    target_include_directories(ocarina-ext INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/pybind11/include/pybind11
            ${CMAKE_CURRENT_SOURCE_DIR}/pybind11/include
            ${PYTHON_INCLUDE_DIRS})
endif ()



add_subdirectory(SPIRV-Cross)
set(_ocarina_spirv_targets
    spirv-cross spirv-cross-c spirv-cross-core spirv-cross-glsl
    spirv-cross-hlsl spirv-cross-msl spirv-cross-cpp spirv-cross-reflect
    spirv-cross-util)
foreach(_t IN LISTS _ocarina_spirv_targets)
    if(TARGET ${_t})
        set_target_properties(${_t} PROPERTIES FOLDER "ext/spirv")
    endif()
endforeach()

# Group remaining third-party targets under ext/ in Visual Studio.
set(_ocarina_ext_targets
    xxhash ocarina-ext EASTL mimalloc-static EABase spdlog)
foreach(_t IN LISTS _ocarina_ext_targets)
    if(TARGET ${_t})
        set_target_properties(${_t} PROPERTIES FOLDER "ext")
    endif()
endforeach()
if(TARGET TracyClient)
    set_target_properties(TracyClient PROPERTIES FOLDER "ext/tracy")
endif()

target_link_libraries(ocarina-ext INTERFACE
        ocarina-ext-stb
        ${PYTHON_LIBRARIES}
        xxhash
        ocarina-ext-tinyexr
        SDL3::SDL3)