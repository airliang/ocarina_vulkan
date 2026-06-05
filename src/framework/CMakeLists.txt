file(GLOB_RECURSE HEADER_FILES *.h*)
file(GLOB_RECURSE SOURCE_FILES *.c*)

add_library(ocarina-framework SHARED ${HEADER_FILES} ${SOURCE_FILES})

set_target_properties(ocarina-framework PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

target_include_directories(ocarina-framework PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ext/enkiTS/src
    ${CMAKE_SOURCE_DIR}/src/ext/imgui
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends)

target_sources(ocarina-framework PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ext/enkiTS/src/TaskScheduler.cpp)

file(GLOB IMGUI_CORE_FILES "${CMAKE_SOURCE_DIR}/src/ext/imgui/*.cpp")
target_sources(ocarina-framework PRIVATE ${IMGUI_CORE_FILES})
target_sources(ocarina-framework PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_sdl3.cpp
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_vulkan.cpp)

target_compile_definitions(ocarina-framework PRIVATE OC_FRAMEWORK_EXPORT_DLL=1)
target_link_libraries(ocarina-framework PUBLIC ocarina-rhi ocarina-core PRIVATE SDL3::SDL3)

option(AUTO_LOCATE_VULKAN "AUTO_LOCATE_VULKAN" ON)
if(AUTO_LOCATE_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
        target_link_libraries(ocarina-framework PRIVATE Vulkan::Vulkan)
    endif()
endif()
