cmake_minimum_required(VERSION 3.14)
project(mempool-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 静态库
add_library(mempool STATIC src/mempool.cpp)
target_include_directories(mempool PUBLIC include)

# 单元测试
enable_testing()
add_executable(test_pool tests/test_pool.cpp)
target_link_libraries(test_pool PRIVATE mempool)
target_link_libraries(test_pool PRIVATE pthread)
add_test(NAME test_pool COMMAND test_pool)
