cmake_minimum_required(VERSION 3.16)
project(VxThesis)


set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sources)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cmake)


set(sources
    ${SOURCE_DIR}/main.cpp
)


#======================================== VxDemo ========================================#
add_executable(VxDemo ${sources} ${includes})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VxDemo)


set_target_properties(VxDemo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED NO
    CXX_EXTENSIONS NO
)


# Vulkan
find_package(Vulkan REQUIRED)
target_include_directories(VxDemo PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(VxDemo PRIVATE ${Vulkan_LIBRARIES})


# GLFW
include(${CMAKE_DIR}/LinkGLFW.cmake)
LinkGLFW(VxDemo PRIVATE)


#========================================================================================#