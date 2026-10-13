cmake_minimum_required(VERSION 3.16)

project(sleep LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Set BUILD_SHARED_LIBS=ON to build the runtime as a shared library. By default,
# the runtime is built as a static library.
add_library(sleep_runtime
    src/runtime.cpp
)

target_include_directories(sleep_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(sleep
    src/main.cpp
)

target_link_libraries(sleep
    PRIVATE sleep_runtime
)

include(CTest)
if(BUILD_TESTING)
    add_executable(thread_pool_test tests/thread_pool_test.cpp)
    target_link_libraries(thread_pool_test PRIVATE sleep_runtime)
    add_test(NAME thread_pool_test COMMAND thread_pool_test)
    set_tests_properties(thread_pool_test PROPERTIES TIMEOUT 15)
endif()
