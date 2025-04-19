
cmake_minimum_required(VERSION 3.16)

find_package(Boost REQUIRED)

FILE(GLOB HTTP_HEADER ${CMAKE_SOURCE_DIR}/include/http/*.h)
FILE(GLOB HTTP_TEMPLATE ${CMAKE_SOURCE_DIR}/include/http/*.hpp)
FILE(GLOB HTTP_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)


add_library(http 
    SHARED
    ${HTTP_HEADER}
    ${HTTP_TEMPLATE}
    ${HTTP_SOURCE}
    ${CMAKE_SOURCE_DIR}/include/json.hpp
)

target_include_directories(http 
    PUBLIC
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(http
    PRIVATE
    Boost::boost
    ${OpenCV_LIBS}
)



if (DATABASE STREQUAL "MYSQL")
    target_link_libraries(http PUBLIC mysql::concpp)
elseif (DATABASE STREQUAL "PGSQL")
    target_link_libraries(http PUBLIC pqxx)
endif()
