file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS *.h)
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS *.cpp)

add_library(ocarina-voxel SHARED ${HEADER_FILES} ${SOURCE_FILES})

set_target_properties(ocarina-voxel PROPERTIES
        PUBLIC_HEADER "${HEADER_FILES}"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        FOLDER "ocarina")

target_include_directories(ocarina-voxel PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ext/enkiTS/src)

target_compile_definitions(ocarina-voxel PRIVATE OC_VOXEL_EXPORT_DLL=1)
target_link_libraries(ocarina-voxel PUBLIC ocarina-framework)
