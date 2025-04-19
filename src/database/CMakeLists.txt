cmake_minimum_required(VERSION 3.16)

FILE(GLOB DB_HEADER ${CMAKE_SOURCE_DIR}/include/database/*.h)
FILE(GLOB DB_TEMPLATE ${CMAKE_SOURCE_DIR}/include/database/*.hpp)
FILE(GLOB DB_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

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



