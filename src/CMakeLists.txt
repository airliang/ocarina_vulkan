

add_library(ocarina-include INTERFACE)
target_include_directories(ocarina-include INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(ext)
add_subdirectory(core)
add_subdirectory(ast)
add_subdirectory(dsl)
add_subdirectory(generator)
add_subdirectory(rhi)
add_subdirectory(backends)
add_subdirectory(framework)

if (${OCARINA_PY})
    add_subdirectory(pyexporter)
endif ()

add_library(ocarina-native INTERFACE)
target_link_libraries(ocarina-native INTERFACE
        ocarina-ext
        ocarina-dsl
        ocarina-ast
        ocarina-generator
        ocarina-backend
        ocarina-rhi
        ocarina-core
        ocarina-framework)

option(OCARINA_BUILD_TESTS "Build tests" TRUE)

if(${OCARINA_BUILD_TESTS})
    add_subdirectory(tests)
endif()

