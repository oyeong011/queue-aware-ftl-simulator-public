cmake_minimum_required(VERSION 3.16)
project(queue_aware_ftl_simulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(include)

add_library(ftlsim src/Ftl.cc)

add_executable(ftlsim_cli src/main.cc)
target_link_libraries(ftlsim_cli ftlsim)

enable_testing()
add_executable(test_invariants tests/test_invariants.cc)
target_link_libraries(test_invariants ftlsim)
add_test(NAME invariants COMMAND test_invariants)
