cmake_minimum_required(VERSION 3.22)

project(DistributedStorage)

set(CMAKE_CXX_STANDARD 20)
# 设置项目可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
# 设置项目动态库文件输出的路径
set(LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
# 生成 compile_commands.json，供 clangd 等 IDE 工具使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(COMMON_INCLUDE ${PROJECT_SOURCE_DIR}/src/common/include)
set(RPC_INCLUDE ${PROJECT_SOURCE_DIR}/src/rpc/include)
set(THIRD_PARTY_INCLUDE ${PROJECT_SOURCE_DIR}/third-party)


aux_source_directory(${PROJECT_SOURCE_DIR}/src/rpc RPC_SRC)
find_package(Boost REQUIRED COMPONENTS serialization)
find_package(Protobuf REQUIRED)
include_directories(${RPC_INCLUDE})
include_directories(${THIRD_PARTY_INCLUDE})
# 添加 vcpkg 管理的 Boost 库路径，使 -lboost_serialization 等能被找到
# Boost_LIBRARY_DIRS 在 vcpkg config 模式下不会被设置，改用 vcpkg 内置变量
# if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
#   link_directories(${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib)
# endif()
# add_library(skip_list INTERFACE)
# target_include_directories(skip_list INTERFACE
#     ${COMMON_INCLUDE}
#     ${Boost_INCLUDE_DIRS}
# )
# target_link_libraries(skip_list INTERFACE Boost::serialization)
add_library(myrpc STATIC ${RPC_SRC})
target_include_directories(myrpc PRIVATE ${COMMON_INCLUDE})
target_link_libraries(myrpc PUBLIC
    protobuf::libprotobuf
    zookeeper_mt
    pthread
    muduo_net
    muduo_base
)

add_executable(skip_list_test test/skip_list_test.cpp)
target_link_libraries(skip_list_test PRIVATE Boost::serialization)
target_include_directories(skip_list_test PRIVATE ${Boost_INCLUDE_DIRS} ${COMMON_INCLUDE})

add_executable(myrpc_connect_pool_test test/myrpc_connect_pool_test.cpp)
target_include_directories(myrpc_connect_pool_test PRIVATE ${COMMON_INCLUDE} ${RPC_INCLUDE})
target_link_libraries(myrpc_connect_pool_test PRIVATE myrpc)

add_executable(echo_server
    example/rpc_demo/echo_server.cc
    example/rpc_demo/rpc_demo.pb.cc
)
target_include_directories(echo_server PRIVATE ${PROJECT_SOURCE_DIR}/example/rpc_demo ${RPC_INCLUDE} ${COMMON_INCLUDE})
target_link_libraries(echo_server PRIVATE myrpc)

add_executable(echo_client
    example/rpc_demo/echo_client.cc
    example/rpc_demo/rpc_demo.pb.cc
)
target_include_directories(echo_client PRIVATE ${PROJECT_SOURCE_DIR}/example/rpc_demo ${RPC_INCLUDE} ${COMMON_INCLUDE})
target_link_libraries(echo_client PRIVATE myrpc)
