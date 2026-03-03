cmake_minimum_required(VERSION 3.20)
project(MyGit VERSION 0.1.0 LANGUAGES CXX)

#C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

#Compiler warnings
if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

#Source files
set(CORE_SOURCES
    src/core/object.cpp
    src/storage/object_store.cpp
    src/storage/file_utils.cpp
    src/utils/hash.cpp
    src/utils/compression.cpp
    src/objects/blob.cpp
    src/objects/tree.cpp
    src/objects/commit.cpp
    src/repository/repository.cpp
    src/index/index.cpp
    src/refs/ref_manager.cpp
    src/cli/command.cpp
    src/cli/init_command.cpp
    src/cli/add_command.cpp
    src/cli/commit_command.cpp
    src/cli/status_command.cpp
    src/cli/log_command.cpp
    src/cli/branch_command.cpp
    src/cli/checkout_command.cpp
    src/diff/diff.cpp
    src/utils/gitignore.cpp
    src/cli/diff_command.cpp
)

#Core Library
add_library(mygit_core STATIC ${CORE_SOURCES})
target_include_directories(mygit_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(mygit_core
    PUBLIC
        OpenSSL::Crypto
        ZLIB::ZLIB
)

#Main executable
add_executable(mygit src/cli/main.cpp)
target_include_directories(mygit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mygit PRIVATE mygit_core)

#install
install(TARGETS mygit DESTINATION bin)

#Tests
if (BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(GTest_FOUND)
        add_subdirectory(tests)
    else()
        message(STATUS "GTest not found, skipping tests")
    endif()
endif()
