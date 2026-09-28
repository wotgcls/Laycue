# Unit tests for src/core. Standalone: needs no OBS, no Qt.
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests --output-on-failure
cmake_minimum_required(VERSION 3.20)

project(laycue-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(LAYCUE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_executable(
  laycue-tests
  test-main.cpp
  test-source-kinds.cpp
  test-cue-state.cpp
  "${LAYCUE_SRC}/core/cue-state.cpp"
  "${LAYCUE_SRC}/core/scene-model.cpp"
  "${LAYCUE_SRC}/core/source-kinds.cpp"
)

target_include_directories(laycue-tests PRIVATE "${LAYCUE_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
  target_compile_options(laycue-tests PRIVATE /W4 /WX)
else()
  target_compile_options(laycue-tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME laycue-tests COMMAND laycue-tests)
