cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lte_m_asset_tracker VERSION 0.2.0)

add_subdirectory(src/gnss)
add_subdirectory(src/sensors)
add_subdirectory(src/lte)
add_subdirectory(src/mqtt)
add_subdirectory(src/telemetry)

target_sources(app PRIVATE src/main.c)

target_include_directories(app PRIVATE src)
