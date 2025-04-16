cmake_minimum_required(VERSION 3.16)

FILE(GLOB DB_HEADER ${CMAKE_SOURCE_DIR}/include/database/*.h)
FILE(GLOB DB_TEMPLATE ${CMAKE_SOURCE_DIR}/include/database/*.hpp)
FILE(GLOB DB_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)




find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)

find_package(MySQL-ConCpp REQUIRED)



add_library(db
    SHARED
    ${DB_HEADER}
    ${DB_TEMPLATE}
    ${DB_SOURCE}
)

target_link_libraries(db PUBLIC pqxx)


