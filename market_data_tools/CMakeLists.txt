cmake_minimum_required(VERSION 3.20)
project(MarketDataTools VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(trade_id_filter src/trade_id_filter.cpp)
add_executable(udp_feed_merger src/udp_feed_merger.cpp)
add_executable(quote_change_detector src/quote_change_detector.cpp)
