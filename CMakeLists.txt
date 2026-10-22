cmake_minimum_required(VERSION 3.16)
project(Design3DistributedBloomFilter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DESIGN3_BUILD_TESTS "Build tests" ON)

add_library(design3_lib
  src/BloomFilter.cpp
  src/DistributedSimulation.cpp
  src/Workload.cpp
)

target_include_directories(design3_lib PUBLIC include)

target_compile_options(design3_lib PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

add_executable(design3_demo src/main.cpp)
target_link_libraries(design3_demo PRIVATE design3_lib)

if(DESIGN3_BUILD_TESTS)
  enable_testing()
  add_executable(design3_tests tests/test_main.cpp)
  target_link_libraries(design3_tests PRIVATE design3_lib)
  add_test(NAME design3_tests COMMAND design3_tests)
endif()
