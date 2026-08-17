cmake_minimum_required(VERSION 3.20)
project(OrderMatchingEngine VERSION 1.0.0 LANGUAGES CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Only Linux and macOS are supported.
if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
  message(FATAL_ERROR
    "Unsupported platform: ${CMAKE_SYSTEM_NAME}. This project builds on "
    "Linux and macOS only.")
endif()

# std::expected, std::format, and the <bit> count* intrinsics need a
# genuinely C++23-capable standard library while compiling in C++26 mode.
# Fail loudly here instead of letting an old Linux toolchain die deep in a
# template instantiation.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
  message(FATAL_ERROR
    "GCC ${CMAKE_CXX_COMPILER_VERSION} is too old: this project needs GCC 14+ "
    "for the std::expected/std::format support required by this C++26 build.")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17)
  message(FATAL_ERROR
    "Clang ${CMAKE_CXX_COMPILER_VERSION} is too old: this project needs Clang 17+ "
    "(with a suitable libc++/libstdc++) for the std::expected/std::format support "
    "required by this C++26 build.")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(matching_engine_warnings INTERFACE)
target_compile_options(matching_engine_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion
  -Wold-style-cast
)
target_compile_options(matching_engine_warnings INTERFACE
  "$<$<CONFIG:Release>:-O3>"
)

add_library(matching_lib
  src/Price.cpp
  src/PriceGrid.cpp
  src/Message.cpp
  src/OrderBook.cpp
  src/MatchingEngine.cpp
)
target_include_directories(matching_lib PUBLIC include)
target_link_libraries(matching_lib PRIVATE matching_engine_warnings)

add_library(matching_app
  src/ActivityTable.cpp
  src/Application.cpp
)
target_include_directories(matching_app PUBLIC include)
target_link_libraries(matching_app
  PRIVATE matching_lib matching_engine_warnings
)

add_executable(match_engine src/main.cpp)
target_link_libraries(match_engine PRIVATE matching_app matching_engine_warnings)
add_custom_command(TARGET match_engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo
          "Build complete. The match_engine executable is available at: $<TARGET_FILE:match_engine>"
  VERBATIM
)

option(BUILD_TESTS "Build the GoogleTest suite" ON)
if(BUILD_TESTS)
  set(BUILD_GMOCK OFF)
  set(INSTALL_GTEST OFF)
  add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL SYSTEM)

  enable_testing()
  add_executable(matching_tests
    tests/test_price.cpp
    tests/test_price_grid.cpp
    tests/test_order.cpp
    tests/test_message.cpp
    tests/test_price_bitset.cpp
    tests/test_order_book.cpp
    tests/test_matching_engine.cpp
    tests/test_application.cpp
  )
  target_link_libraries(matching_tests PRIVATE
    matching_app
    matching_engine_warnings
    GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(matching_tests)
endif()
