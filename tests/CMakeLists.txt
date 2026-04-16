cmake_minimum_required(VERSION 3.14)
project(aauto-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Placeholder test to verify build system works
add_executable(aauto_core_test
    core/placeholder_test.cpp
)

target_link_libraries(aauto_core_test PRIVATE
    aauto-core
    GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(aauto_core_test)
