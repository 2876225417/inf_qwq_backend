cmake_minimum_required(VERSION 3.16)


find_package(Boost REQUIRED)

add_subdirectory(inferer)
add_subdirectory(http)
add_subdirectory(database)

add_executable(inf_qwq_backend main.cpp)


target_link_libraries(inf_qwq_backend PRIVATE Boost::boost)
