# Minimum CMake version
cmake_minimum_required(VERSION 3.24)

# Project name
set(PROJECT_NAME "Deferred")
project(${PROJECT_NAME})

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB HEADERS "*.h")
file(GLOB SOURCES "*.cpp")
add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES})


#configure OTCV
set(OTCV_SPIRV_CROSS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libraries/SPIRV-Cross")
set(OTCV_GLFW_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libraries/glfw-3.4.bin.WIN64")
set(OTCV_GLFW_LIB_VARIANT "lib-vc2022")
option(OTCV_ENABLE_IMGUI "" ON)
set(OTCV_IMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../imgui")
set(OTCV_ENABLE_SHADER_TOOL ON)
set(OTCV_GLSL_IN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(OTCV_SPIRV_OUT_PATH "${CMAKE_BINARY_DIR}/spirv")
set(OTCV_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../otcv")
add_subdirectory(${OTCV_PATH} ${CMAKE_BINARY_DIR}/otcv)

target_link_libraries(${PROJECT_NAME}
    PRIVATE ${OTCV_BASE_LIB}
    PRIVATE ${OTCV_IMGUI_LIB})
add_dependencies(${PROJECT_NAME} ${OTCV_SHADER_TOOL})

# copy font resource to build directory
#file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fonts DESTINATION ${CMAKE_BINARY_DIR})

# set tinygltf path
set(TINYGLTF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../tinygltf")
message(STATUS "TINYGLTF_PATH = ${TINYGLTF_PATH}")
target_include_directories(${PROJECT_NAME} PUBLIC ${TINYGLTF_PATH})

