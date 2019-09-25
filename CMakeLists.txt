cmake_minimum_required(VERSION 3.8)
project(beast_issue)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_FLAGS "-ggdb")

find_package(Boost  REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})

message("find boost:" ${Boost_INCLUDE_DIRS})
message("boost root:" ${Boost_VERSION})

add_executable(ws_issue_cli beast_issue_client.cpp)
add_executable(ws_issue_svr beast_issue_server.cpp)