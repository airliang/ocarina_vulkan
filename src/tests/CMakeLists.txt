

function(ocarina_add_test name)
    cmake_parse_arguments(TEST "" "" "SOURCES" ${ARGN})
    add_executable(${name}  ${TEST_SOURCES})
    target_link_libraries(${name} ocarina-native)
    set_target_properties(${name} PROPERTIES
            PUBLIC_HEADER "${HEADER_FILES}"
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            FOLDER "test")
endfunction()


ocarina_add_test(test-vulkan-triangle SOURCES test_vulkan_triangle.cpp)
ocarina_add_test(test-vulkan-offscreen SOURCES test_vulkan_offscreen.cpp)
ocarina_add_test(test-vulkan-texture SOURCES test_vulkan_texture.cpp)
ocarina_add_test(test-vulkan-bindless SOURCES test_vulkan_bindless.cpp)
ocarina_add_test(test-enkiTS SOURCES test_enkiTS.cpp)
ocarina_add_test(test-asyncLoadGLTF SOURCES test_load_gltf.cpp)
ocarina_add_test(test-culling SOURCES test_culling.cpp)
