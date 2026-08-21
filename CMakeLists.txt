cmake_minimum_required(VERSION 3.16)
project(VxThesis)


set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sources)
set(DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(IMGUI_SOURCE_DIR ${SOURCE_DIR}/3rdparty/imgui)
set(VMA_SOURCE_DIR ${SOURCE_DIR}/3rdparty/VulkanMemoryAllocator)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(TESTS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)



set(imgui_sources
    ${IMGUI_SOURCE_DIR}/imgui.cpp
    ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
    ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
    ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
    ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
source_group(ImGui FILES ${imgui_sources})


set(sources
    ${SOURCE_DIR}/main.cpp
    ${SOURCE_DIR}/Base/Debug.cpp
    ${SOURCE_DIR}/Base/Log.h
    ${SOURCE_DIR}/Base/Log.cpp
    ${SOURCE_DIR}/Demo/Application.h
    ${SOURCE_DIR}/Demo/Application.cpp
    ${SOURCE_DIR}/Demo/Scene.h
    ${SOURCE_DIR}/Demo/Scene.cpp
    ${SOURCE_DIR}/Demo/MainScene.h
    ${SOURCE_DIR}/Demo/MainScene.cpp
    ${SOURCE_DIR}/Demo/imgfx.h
    ${SOURCE_DIR}/Demo/imgfx.cpp
    ${SOURCE_DIR}/Renderer/RenderDevice.h
    ${SOURCE_DIR}/Renderer/RenderDevice.cpp
    ${SOURCE_DIR}/Renderer/SwapChain.h
    ${SOURCE_DIR}/Renderer/SwapChain.cpp
    ${SOURCE_DIR}/Renderer/Pipeline.h
    ${SOURCE_DIR}/Renderer/Pipeline.cpp
    ${SOURCE_DIR}/Renderer/Image.h
    ${SOURCE_DIR}/Renderer/Image.cpp
    ${SOURCE_DIR}/Renderer/DescriptorSet.h
    ${SOURCE_DIR}/Renderer/DescriptorSet.cpp
    ${SOURCE_DIR}/Renderer/Buffer.h
    ${SOURCE_DIR}/Renderer/Buffer.cpp
    ${SOURCE_DIR}/Renderer/CommandBuffer.h
    ${SOURCE_DIR}/Renderer/CommandBuffer.cpp
    ${SOURCE_DIR}/Voxel/SVTree.h
    ${SOURCE_DIR}/Voxel/SVTree.cpp
    ${SOURCE_DIR}/Voxel/SVBuilder.h
    ${SOURCE_DIR}/Voxel/SVBuilder.cpp
    ${SOURCE_DIR}/Voxel/VoxLoader.h
    ${SOURCE_DIR}/Voxel/VoxLoader.cpp
    ${SOURCE_DIR}/Voxel/RayTrace.h
    ${SOURCE_DIR}/Voxel/RayTrace.cpp
)


#======================================== VxDemo ========================================#
add_executable(VxDemo ${sources} ${imgui_sources} ${includes})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VxDemo)
set_property(TARGET VxDemo PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")


set_target_properties(VxDemo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED OFF
    CXX_EXTENSIONS OFF
)

target_include_directories(VxDemo PRIVATE
    ${SOURCE_DIR}
    ${IMGUI_SOURCE_DIR}
    ${VMA_SOURCE_DIR}
)


#==================================== Compile shaders ===================================#


set(SHADER_DIR ${SOURCE_DIR}/Shaders)
file(GLOB SHADER_FILES ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag ${SHADER_DIR}/*.comp)

set(SHADER_OUTPUT_DIR "${DATA_DIR}/shaders")
set(SHADER_OUTPUT_FILES "")

foreach(SHADER_INPUT_FILE IN LISTS SHADER_FILES)
    get_filename_component(FILE_NAME ${SHADER_INPUT_FILE} NAME)
    set(SHADER_OUTPUT_FILE "${SHADER_OUTPUT_DIR}/${FILE_NAME}.spv")

    add_custom_command(
        OUTPUT  ${SHADER_OUTPUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V ${SHADER_INPUT_FILE} -o ${SHADER_OUTPUT_FILE}
        DEPENDS ${SHADER_INPUT_FILE}
        COMMENT "Compiling shader: ${FILE_NAME}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUT_FILES ${SHADER_OUTPUT_FILE})
endForeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_OUTPUT_FILES})
add_dependencies(VxDemo Shaders)

#======================================== Tests =========================================#
add_executable(VxTests
    ${SOURCE_DIR}/Base/Debug.cpp
    ${SOURCE_DIR}/Base/Log.cpp
    ${SOURCE_DIR}/Voxel/SVTree.h
    ${SOURCE_DIR}/Voxel/SVTree.cpp
    ${TESTS_SOURCE_DIR}/catch.cpp
    ${TESTS_SOURCE_DIR}/test_svtree.cpp
)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VxTests)
set_property(TARGET VxTests PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")


set_target_properties(VxTests PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED OFF
    CXX_EXTENSIONS OFF
)

target_include_directories(VxTests PRIVATE
    ${SOURCE_DIR}
)
#========================================================================================#


#===================== Dependencies =====================#

# Vulkan
find_package(Vulkan REQUIRED)
target_include_directories(VxDemo PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(VxDemo PRIVATE ${Vulkan_LIBRARIES})


# GLFW
include(${CMAKE_DIR}/LinkGLFW.cmake)
LinkGLFW(VxDemo PRIVATE)


# GLM
include(${CMAKE_DIR}/LinkGLM.cmake)
LinkGLM(VxDemo PRIVATE)
LinkGLM(VxTests PRIVATE)

