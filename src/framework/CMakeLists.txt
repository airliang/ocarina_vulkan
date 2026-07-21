file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)
list(FILTER SOURCE_FILES EXCLUDE REGEX "/ocarina-framework\\.dir/")

add_library(ocarina-framework SHARED ${HEADER_FILES} ${SOURCE_FILES})

set_target_properties(ocarina-framework PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

target_include_directories(ocarina-framework PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ext/enkiTS/src)

target_sources(ocarina-framework PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ext/enkiTS/src/TaskScheduler.cpp)

target_sources(ocarina-framework PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_sdl3.cpp)

target_compile_definitions(ocarina-framework PRIVATE OC_FRAMEWORK_EXPORT_DLL=1)
# imgui_impl_sdl3 is compiled into this DLL; do not inherit IMGUI_API as dllimport for it.
target_compile_definitions(ocarina-framework PRIVATE "IMGUI_IMPL_API=")
target_link_libraries(ocarina-framework PUBLIC ocarina-rhi ocarina-core ocarina-imgui PRIVATE SDL3::SDL3)
