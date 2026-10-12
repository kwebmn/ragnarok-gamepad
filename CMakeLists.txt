cmake_minimum_required(VERSION 3.20)
project(uaro_pad LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Portable core: protocol, world model, controller logic. No Windows headers.
file(GLOB_RECURSE UAPAD_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(uapad_core STATIC ${UAPAD_CORE_SOURCES})
target_include_directories(uapad_core PUBLIC src)
target_compile_options(uapad_core PRIVATE -Wall -Wextra)

if(NOT WIN32)
    enable_testing()
    file(GLOB UAPAD_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(uapad_tests ${UAPAD_TEST_SOURCES})
    target_include_directories(uapad_tests PRIVATE tests)
    target_link_libraries(uapad_tests PRIVATE uapad_core)
    add_test(NAME uapad_tests COMMAND uapad_tests)
endif()
