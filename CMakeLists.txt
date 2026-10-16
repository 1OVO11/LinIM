cmake_minimum_required(VERSION 3.16)
project(LinIM LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(/usr/include/mysql)
include_directories(src)
include_directories(common)

add_executable(linim_server
    src/main.cpp
    src/data/MySQL_dao.cpp
    src/network/network.cpp
    src/network/threadpool.cpp
    src/network/client_manager.cpp
    common/protocol.cpp
    src/service/service.cpp
)

target_link_libraries(linim_server mysqlclient pthread)

add_executable(linim_client
    client/client.cpp
    common/protocol.cpp
)

target_link_libraries(linim_client pthread)
