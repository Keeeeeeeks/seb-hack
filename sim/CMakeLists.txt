cmake_minimum_required(VERSION 3.20)
project(track_b_host_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../tests
)

add_library(audio_engine_sim audio_engine_sim.c)
add_library(wav wav.c)
add_executable(track_b_harness harness.c)
target_link_libraries(track_b_harness PRIVATE audio_engine_sim wav m)

enable_testing()
