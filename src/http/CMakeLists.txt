
cmake_minimum_required(VERSION 3.16)

find_package(Boost REQUIRED)

FILE(GLOB HTTP_HEADER ${CMAKE_SOURCE_DIR}/include/http/*.h)
FILE(GLOB HTTP_TEMPLATE ${CMAKE_SOURCE_DIR}/include/http/*.hpp)

FILE(GLOB API_RESOLVER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/api_resolver/*.cpp)
FILE(GLOB DB_UTILS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/db_utils/*.cpp)
FILE(GLOB SERVER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/server/*.cpp)
FILE(GLOB CURRENT_DIR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.cp)

add_library(http 
    SHARED
    ${HTTP_HEADER}
    ${API_RESOLVER_SRC}
    ${DB_UTILS_SRC}
    ${SERVER_SRC}
    ${CURRENT_DIR_SRC}
)

target_link_libraries(http
    PRIVATE

)
