cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(MySQL-ConCpp_DIR ${CMAKE_SOURCE_DIR}/3rdparty/mysqlconn)

project(inf_qwq_backend LANGUAGES CXX)

include("${CMAKE_SOURCE_DIR}/cmake/DownloadONNXRuntime.cmake")
download_onnxruntime()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(ONNXRuntime REQUIRED)
find_package(MySQL-ConCpp REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${ONNXRUNTIME_INCLUDE_DIR}
)

add_subdirectory(src)
