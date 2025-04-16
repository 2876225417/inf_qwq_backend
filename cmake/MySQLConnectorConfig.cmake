# MySQLConnectorConfig.cmake - 配置自定义安装的 MySQL Connector/C++
# 使用方法: include(${CMAKE_CURRENT_LIST_DIR}/cmake/MySQLConnectorConfig.cmake)

# 防止重复包含
if(DEFINED _CUSTOM_MYSQL_CONNECTOR_CMAKE_INCLUDED)
  return()
endif()
set(_CUSTOM_MYSQL_CONNECTOR_CMAKE_INCLUDED TRUE)

# 设置 MySQL Connector/C++ 根目录 - 用户可以在包含此文件前设置此变量
if(NOT DEFINED CUSTOM_MYSQL_CONNECTOR_ROOT)
  # 默认使用此项目的 3rdparty/mysql_connector 目录
  get_filename_component(CUSTOM_MYSQL_CONNECTOR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/mysql_connector" ABSOLUTE)
endif()

# 检查 MySQL Connector/C++ 安装
if(NOT EXISTS "${CUSTOM_MYSQL_CONNECTOR_ROOT}/include/mysqlx")
  message(FATAL_ERROR "未找到 MySQL Connector/C++ 库: ${CUSTOM_MYSQL_CONNECTOR_ROOT}/include/mysqlx 不存在")
endif()

# 检查库文件目录
set(MYSQL_CONNECTOR_LIB_DIR "${CUSTOM_MYSQL_CONNECTOR_ROOT}/lib64")
if(NOT EXISTS "${MYSQL_CONNECTOR_LIB_DIR}")
  set(MYSQL_CONNECTOR_LIB_DIR "${CUSTOM_MYSQL_CONNECTOR_ROOT}/lib")
  if(NOT EXISTS "${MYSQL_CONNECTOR_LIB_DIR}")
    message(FATAL_ERROR "未找到 MySQL Connector/C++ 库目录: 既不存在 lib64 也不存在 lib")
  endif()
endif()

# 获取 MySQL Connector/C++ 版本
if(EXISTS "${CUSTOM_MYSQL_CONNECTOR_ROOT}/INFO_SRC")
  file(READ "${CUSTOM_MYSQL_CONNECTOR_ROOT}/INFO_SRC" _mysql_info_src)
  string(REGEX MATCH "MYSQL_CONNECTOR_CPP_VERSION[ \t]*=[ \t]*([0-9]+\\.[0-9]+\\.[0-9]+)" _mysql_version_match "${_mysql_info_src}")
  if(_mysql_version_match)
    set(CUSTOM_MYSQL_CONNECTOR_VERSION "${CMAKE_MATCH_1}")
  else()
    set(CUSTOM_MYSQL_CONNECTOR_VERSION "unknown")
  endif()
else()
  set(CUSTOM_MYSQL_CONNECTOR_VERSION "unknown")
endif()

# 设置 MySQL Connector/C++ 路径
set(MySQLConnectorCPP_DIR "${CUSTOM_MYSQL_CONNECTOR_ROOT}")
set(MySQLConnectorCPP_INCLUDE_DIRS "${CUSTOM_MYSQL_CONNECTOR_ROOT}/include")
set(MySQLConnectorCPP_LIBRARY_DIRS "${MYSQL_CONNECTOR_LIB_DIR}")

# 查找 MySQL Connector/C++ 库
find_library(MySQLConnectorCPP_JDBC_LIBRARY
  NAMES mysqlcppconn
  PATHS "${MySQLConnectorCPP_LIBRARY_DIRS}"
  NO_DEFAULT_PATH
)

find_library(MySQLConnectorCPP_X_LIBRARY
  NAMES mysqlcppconn8
  PATHS "${MySQLConnectorCPP_LIBRARY_DIRS}"
  NO_DEFAULT_PATH
)

# 检查库是否找到
if(NOT MySQLConnectorCPP_JDBC_LIBRARY AND NOT MySQLConnectorCPP_X_LIBRARY)
  message(FATAL_ERROR "未找到 MySQL Connector/C++ 库文件，请确保库已正确安装")
endif()

# 创建导入目标以便于使用
if(MySQLConnectorCPP_JDBC_LIBRARY AND NOT TARGET MySQLConnector::JDBC)
  add_library(MySQLConnector::JDBC UNKNOWN IMPORTED)
  set_target_properties(MySQLConnector::JDBC PROPERTIES
    IMPORTED_LOCATION "${MySQLConnectorCPP_JDBC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MySQLConnectorCPP_INCLUDE_DIRS}"
  )
  message(STATUS "找到 MySQL Connector/C++ JDBC 库: ${MySQLConnectorCPP_JDBC_LIBRARY}")
endif()

if(MySQLConnectorCPP_X_LIBRARY AND NOT TARGET MySQLConnector::X)
  add_library(MySQLConnector::X UNKNOWN IMPORTED)
  set_target_properties(MySQLConnector::X PROPERTIES
    IMPORTED_LOCATION "${MySQLConnectorCPP_X_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MySQLConnectorCPP_INCLUDE_DIRS}"
  )
  message(STATUS "找到 MySQL Connector/C++ X DevAPI 库: ${MySQLConnectorCPP_X_LIBRARY}")
endif()

# 创建一个统一的目标
if(NOT TARGET MySQLConnector::Connector)
  add_library(MySQLConnector::Connector INTERFACE IMPORTED)
  if(TARGET MySQLConnector::X)
    set_target_properties(MySQLConnector::Connector PROPERTIES
      INTERFACE_LINK_LIBRARIES "MySQLConnector::X"
    )
  elseif(TARGET MySQLConnector::JDBC)
    set_target_properties(MySQLConnector::Connector PROPERTIES
      INTERFACE_LINK_LIBRARIES "MySQLConnector::JDBC"
    )
  endif()
endif()

# 定义一个函数来链接 MySQL Connector/C++ 库
function(target_link_mysql_connector target)
  if(${ARGC} LESS 2)
    # 如果没有指定 API，默认使用 X DevAPI
    if(TARGET MySQLConnector::X)
      target_link_libraries(${target} PRIVATE MySQLConnector::X)
    else()
      target_link_libraries(${target} PRIVATE MySQLConnector::JDBC)
    endif()
    return()
  endif()
  
  # 链接请求的 API
  foreach(api ${ARGN})
    if(api STREQUAL "x" OR api STREQUAL "X" OR api STREQUAL "xdevapi")
      if(TARGET MySQLConnector::X)
        target_link_libraries(${target} PRIVATE MySQLConnector::X)
      else()
        message(WARNING "未找到 MySQL Connector/C++ X DevAPI 库")
      endif()
    elseif(api STREQUAL "jdbc" OR api STREQUAL "JDBC")
      if(TARGET MySQLConnector::JDBC)
        target_link_libraries(${target} PRIVATE MySQLConnector::JDBC)
      else()
        message(WARNING "未找到 MySQL Connector/C++ JDBC 库")
      endif()
    else()
      message(WARNING "未知的 MySQL Connector/C++ API: ${api}")
    endif()
  endforeach()
endfunction()

# 创建一个环境变量设置函数
function(use_mysql_connector_env)
  set(ENV{MYSQL_CONNECTOR_CPP_DIR} "${CUSTOM_MYSQL_CONNECTOR_ROOT}")
  set(ENV{CPATH} "${MySQLConnectorCPP_INCLUDE_DIRS}:$ENV{CPATH}")
  set(ENV{LIBRARY_PATH} "${MySQLConnectorCPP_LIBRARY_DIRS}:$ENV{LIBRARY_PATH}")
  set(ENV{LD_LIBRARY_PATH} "${MySQLConnectorCPP_LIBRARY_DIRS}:$ENV{LD_LIBRARY_PATH}")
endfunction()

# 输出配置信息
message(STATUS "已配置自定义 MySQL Connector/C++ ${CUSTOM_MYSQL_CONNECTOR_VERSION}")
message(STATUS "MySQL Connector/C++ 根目录: ${CUSTOM_MYSQL_CONNECTOR_ROOT}")
message(STATUS "MySQL Connector/C++ 库目录: ${MySQLConnectorCPP_LIBRARY_DIRS}")

# 显示可用的 API
set(_available_apis)
if(TARGET MySQLConnector::X)
  list(APPEND _available_apis "X DevAPI")
endif()
if(TARGET MySQLConnector::JDBC)
  list(APPEND _available_apis "JDBC API")
endif()
message(STATUS "可用的 API: ${_available_apis}")

message(STATUS "使用说明:")
message(STATUS "  - 默认链接 MySQL Connector/C++: target_link_mysql_connector(your_target)")
message(STATUS "  - 链接特定 API: target_link_mysql_connector(your_target x jdbc)")
