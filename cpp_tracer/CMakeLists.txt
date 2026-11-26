cmake_minimum_required(VERSION 3.16)
project(cpp_instrumenter)

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

add_executable(cpp-instrumenter instrumenter.cpp)

target_link_libraries(cpp-instrumenter
  PRIVATE
  clangTooling
  clangBasic
  clangASTMatchers
  clangRewrite
)
