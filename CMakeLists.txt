cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MySQL Cpp Connector
set(MySQL-ConCpp_DIR ${CMAKE_SOURCE_DIR}/3rdparty/mysqlconn)

project(inf_qwq_backend LANGUAGES CXX)


set(DATABASE "MYSQL" CACHE STRING "Database backend to use (MYSQL | PGSQL)")
set_property(CACHE DATABASE PROPERTY STRINGS MYSQL PGSQL)

string(TOUPPER ${DATABASE} DATABASE)
if(NOT DATABASE MATCHES "^(MYSQL|PGSQL)$")
    message(FATAL_ERROR "Invalid database option '${DATABASE}'. Must be MYSQL or PGSQL.")
endif()

if (DATABASE STREQUAL "MYSQL")
    find_package(MySQL-ConCpp REQUIRED)
    add_definitions(-DUSE_MYSQL)
    message(STATUS "Building with MYSQL support")
elseif (DATABASE STREQUAL "PGSQL")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBPQXX REQUIRED libpqxx)
    add_definitions(-DUSE_PGSQL)
    message(STATUS "Building with PGSQL support")
endif()



include("${CMAKE_SOURCE_DIR}/cmake/DownloadONNXRuntime.cmake")
download_onnxruntime()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(ONNXRuntime REQUIRED)

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${ONNXRUNTIME_INCLUDE_DIR}
)

add_subdirectory(src)
