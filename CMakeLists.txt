cmake_minimum_required(VERSION 3.16)

project(sleep LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

find_package(Threads REQUIRED)

# Build both library variants, including the C thread-pool exports.
set(SLEEP_RUNTIME_SOURCES
    src/runtime.cpp
    lib_export/thread_pool_export.cpp
)
add_library(sleep_runtime STATIC ${SLEEP_RUNTIME_SOURCES})
add_library(sleep_runtime_shared SHARED ${SLEEP_RUNTIME_SOURCES})
set_target_properties(sleep_runtime_shared PROPERTIES
    OUTPUT_NAME sleep_runtime
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
if(WIN32)
    # Keep the DLL import library distinct from the static .lib.
    set_target_properties(sleep_runtime_shared PROPERTIES
        ARCHIVE_OUTPUT_NAME sleep_runtime_import
    )
endif()

foreach(runtime_target IN ITEMS sleep_runtime sleep_runtime_shared)
    target_include_directories(${runtime_target}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(${runtime_target} PUBLIC Threads::Threads)
endforeach()

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
    add_test(NAME resize_demo COMMAND sleep)
    set_tests_properties(resize_demo PROPERTIES TIMEOUT 15)
endif()
