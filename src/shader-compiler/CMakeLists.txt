file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(AUTO_LOCATE_VULKAN "AUTO_LOCATE_VULKAN" ON)
if(AUTO_LOCATE_VULKAN)
    find_package(Vulkan)
endif()

add_library(ocarina-shader-compiler SHARED
    shader_compiler.cpp
    ${CMAKE_SOURCE_DIR}/src/backends/vulkan/dxc_compiler.cpp
)

target_compile_definitions(ocarina-shader-compiler PRIVATE OC_SHADER_COMPILER_EXPORT_DLL=1)

target_include_directories(ocarina-shader-compiler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/backends/vulkan
        ${CMAKE_SOURCE_DIR}/src
)

# DXC + SPIRV-Cross are part of the current shader toolchain.
if(MSVC)
    set(DXC_PATH "${CMAKE_SOURCE_DIR}/src/ext/dxc/msvc")
    set(SPIRV_PATH "${CMAKE_SOURCE_DIR}/src/ext/SPIRV-Cross")
    target_include_directories(ocarina-shader-compiler PRIVATE ${DXC_PATH}/inc ${SPIRV_PATH})

    set(DXC_LIB ${DXC_PATH}/lib/x64)
    set(DXC_BIN ${DXC_PATH}/bin/x64)
    if (NOT CMAKE_CL_64)
        set(DXC_LIB ${DXC_PATH}/lib/x86)
        set(DXC_BIN ${DXC_PATH}/bin/x86)
    endif()

    link_directories(${DXC_LIB})
    target_link_libraries(ocarina-shader-compiler PRIVATE dxcompiler.lib spirv-cross-core spirv-cross-hlsl)

    set(BUILTIN_DST "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${CMAKE_CFG_INTDIR}")
    file(GLOB DXC_BIN_FILES "${DXC_BIN}/*.*")
    foreach(FILE ${DXC_BIN_FILES})
        add_custom_command(TARGET ocarina-shader-compiler POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "${FILE}"
                "${BUILTIN_DST}"
        )
    endforeach()
else()
    set(DXC_PATH "${CMAKE_SOURCE_DIR}/src/ext/dxc/clang")
    target_include_directories(ocarina-shader-compiler PRIVATE ${DXC_PATH}/inc)

    add_library(dxc SHARED IMPORTED)
    target_link_libraries(ocarina-shader-compiler PRIVATE dxc)
endif()

# RHIContext is used for shader rebuild/reuse.
target_link_libraries(ocarina-shader-compiler PRIVATE ocarina-rhi ocarina-core)
if(Vulkan_FOUND)
    target_link_libraries(ocarina-shader-compiler PRIVATE Vulkan::Vulkan)
endif()

set_target_properties(ocarina-shader-compiler PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    UNITY_BUILD OFF
    FOLDER "ocarina"
)

