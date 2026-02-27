file(GLOB_RECURSE HEADER_FILES *.h*)
file(GLOB_RECURSE SOURCE_FILES *.c*)

add_library(ocarina-GUI_impl INTERFACE)

function(ocarina_add_window_plugin name)

    cmake_parse_arguments(PLUGIN "" "CATEGORY" "SOURCES" ${ARGN})
    set(lib_name ocarina-${PLUGIN_CATEGORY}-${name})
    add_library(${lib_name} MODULE ${PLUGIN_SOURCES})
    target_link_libraries(${lib_name} PRIVATE ocarina-GUI ocarina-rhi)
    set(category ocarina-${PLUGIN_CATEGORY})
    add_dependencies(${category} ${lib_name})

    set_target_properties(${lib_name} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
            WINDOWS_EXPORT_ALL_SYMBOLS OFF
            UNITY_BUILD OFF
            DEBUG_POSTFIX ""
            FOLDER "ocarina")

    target_include_directories(${lib_name} PRIVATE ${CMAKE_SOURCE_DIR}/src/ext/imgui)

    file(GLOB IMGUI_FILES
    "${CMAKE_SOURCE_DIR}/src/ext/imgui/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/ext/imgui/*.h"
    )

    target_sources(${lib_name} PRIVATE ${IMGUI_FILES})

    target_sources(${lib_name} PRIVATE 
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_opengl3.h
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_opengl3.cpp
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_glfw.h
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_vulkan.h
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_vulkan.cpp
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_SDL3.h
    ${CMAKE_SOURCE_DIR}/src/ext/imgui/backends/imgui_impl_SDL3.cpp
    )

    option(AUTO_LOCATE_VULKAN "AUTO_LOCATE_VULKAN" ON)
    if(AUTO_LOCATE_VULKAN)
        message(STATUS "Attempting auto locate Vulkan using CMake......")
        
        # Find Vulkan Path using CMake's Vulkan Module
        # This will return Boolean 'Vulkan_FOUND' indicating the status of find as success(ON) or fail(OFF).
        # Include directory path - 'Vulkan_INCLUDE_DIRS' and 'Vulkan_LIBRARY' with required libraries.
        find_package(Vulkan)
        
        # Try extracting VulkanSDK path from ${Vulkan_INCLUDE_DIRS}
        if (NOT ${Vulkan_INCLUDE_DIRS} STREQUAL "")
            set(VULKAN_PATH ${Vulkan_INCLUDE_DIRS})
            STRING(REGEX REPLACE "/Include" "" VULKAN_PATH ${VULKAN_PATH})
        endif()
            
        if(NOT Vulkan_FOUND)
            # CMake may fail to locate the libraries but could be able to 
            # provide some path in Vulkan SDK include directory variable
            # 'Vulkan_INCLUDE_DIRS', try to extract path from this.
            message(STATUS "Failed to locate Vulkan SDK, retrying again...")
            if(EXISTS "${VULKAN_PATH}")
                message(STATUS "Successfully located the Vulkan SDK: ${VULKAN_PATH}")
            else()
                message("Error: Unable to locate Vulkan SDK. Please turn off auto locate option by specifying 'AUTO_LOCATE_VULKAN' as 'OFF'")
                message("and specify manually path using 'VULKAN_SDK' and 'VULKAN_VERSION' variables in the CMakeLists.txt.")
                return()
            endif()
        endif()
        target_link_libraries(${lib_name} PRIVATE Vulkan::Vulkan)
	endif()
endfunction()

add_subdirectory(imGui)
