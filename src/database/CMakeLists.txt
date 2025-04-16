cmake_minimum_required(VERSION 3.16)

FILE(GLOB DB_HEADER ${CMAKE_SOURCE_DIR}/include/database/*.h)
FILE(GLOB DB_TEMPLATE ${CMAKE_SOURCE_DIR}/include/database/*.hpp)
FILE(GLOB DB_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)


set(DATABASE "MYSQL" CACHE STRING "Database backend to use (MYSQL | PGSQL)")
set_property(CACHE DATABASE PROPERTY STRINGS MYSQL PGSQL)

string(TOUPPER ${DATABASE} DATABASE)
if(NOT DATABASE MATCHES "^(MYSQL|PGSQL)$")
    message(FATAL_ERROR "Invalid database option '${DATABASE}'. Must be MYSQL or PGSQL.")
endif()

if (DATABASE STREQUAL "MYSQL")
    find_package(MySQL-ConCpp REQUIRED)
    add_definitions(-USE_MYSQL)
    message(STATUS "Building with MYSQL support")
elseif(DATABASE STREQUAL "PGSQL")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBPQXX REQUIRED libpqxx)
    add_definitions(-DUSE_PGSQL)
    message(STATUS "Building with PGSQL support")
endif()


add_library(db
    SHARED
    ${DB_HEADER}
    ${DB_TEMPLATE}
    ${DB_SOURCE}
)

if (DATABASE STREQUAL "MYSQL")
    target_link_libraries(db PUBLIC mysql::concpp)
elseif (DATABASE STREQUAL "PGSQL")
    target_link_libraries(db PUBLIC pqxx)
endif()



