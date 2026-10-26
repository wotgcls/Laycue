# Unit tests for src/core. Standalone: needs no OBS, no Qt.
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests --output-on-failure
cmake_minimum_required(VERSION 3.20)

project(laycue-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(LAYCUE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/scenes/src")
set(TITLES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/titles/src")
set(NETWORK_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/network/src")
set(CLOCK_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/clock/src")
set(COMMON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/common/src")
set(SCRIPTURE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/scripture/src")
set(LYRICS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../plugins/lyrics/src")

add_executable(
  laycue-tests
  test-main.cpp
  test-source-kinds.cpp
  test-cue-state.cpp
  test-watchdog.cpp
  test-detect.cpp
  test-geometry.cpp
  test-exclusivity.cpp
  test-config.cpp
  test-locale.cpp
  test-layouts.cpp
  test-frame-style.cpp
  test-titles.cpp
  test-network.cpp
  test-update.cpp
  test-clock.cpp
  test-scripture.cpp
  test-lyrics.cpp
  test-slides.cpp
  "${LAYCUE_SRC}/core/cue-state.cpp"
  "${LAYCUE_SRC}/core/watchdog.cpp"
  "${LAYCUE_SRC}/core/detect.cpp"
  "${LAYCUE_SRC}/core/geometry.cpp"
  "${LAYCUE_SRC}/core/autotag.cpp"
  "${LAYCUE_SRC}/core/exclusivity.cpp"
  "${LAYCUE_SRC}/core/config.cpp"
  "${LAYCUE_SRC}/core/frame-style.cpp"
  "${LAYCUE_SRC}/core/layouts.cpp"
  "${COMMON_SRC}/core/update-info.cpp"
  "${COMMON_SRC}/core/xml-lite.cpp"
  "${COMMON_SRC}/slides/slide-style.cpp"
  "${SCRIPTURE_SRC}/core/templates.cpp"
  "${LYRICS_SRC}/core/templates.cpp"
  "${SCRIPTURE_SRC}/core/books.cpp"
  "${SCRIPTURE_SRC}/core/reference.cpp"
  "${SCRIPTURE_SRC}/core/bible.cpp"
  "${LYRICS_SRC}/core/song.cpp"
  "${LYRICS_SRC}/core/online.cpp"
  "${CLOCK_SRC}/core/clock-core.cpp"
  "${NETWORK_SRC}/core/stats.cpp"
  "${NETWORK_SRC}/core/speed-analysis.cpp"
  "${NETWORK_SRC}/core/recommend.cpp"
  "${NETWORK_SRC}/core/health.cpp"
  "${NETWORK_SRC}/core/adaptive.cpp"
  "${NETWORK_SRC}/core/report.cpp"
  "${NETWORK_SRC}/core/network-settings.cpp"
  "${TITLES_SRC}/core/easing.cpp"
  "${TITLES_SRC}/core/qr.cpp"
  "${TITLES_SRC}/core/library.cpp"
  "${TITLES_SRC}/core/title-clock.cpp"
  "${TITLES_SRC}/core/ticker.cpp"
  "${TITLES_SRC}/core/title-templates.cpp"
  "${LAYCUE_SRC}/core/scene-model.cpp"
  "${LAYCUE_SRC}/core/source-kinds.cpp"
)

target_include_directories(
  laycue-tests
  PRIVATE
    "${LAYCUE_SRC}"
    "${TITLES_SRC}"
    "${NETWORK_SRC}"
    "${COMMON_SRC}"
    "${CLOCK_SRC}"
    "${SCRIPTURE_SRC}"
    "${LYRICS_SRC}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(laycue-tests PRIVATE LAYCUE_PLUGINS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../plugins")

add_executable(laycue-fixture-tests test-fixtures.cpp "${LAYCUE_SRC}/core/detect.cpp")
target_include_directories(laycue-fixture-tests PRIVATE "${LAYCUE_SRC}")

foreach(target laycue-tests laycue-fixture-tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  endif()
endforeach()

enable_testing()
add_test(NAME laycue-tests COMMAND laycue-tests)

# Real pushback graphics: converted to PAM with ImageMagick, then checked. Skipped when
# ImageMagick isn't installed. ("convert" is only trusted off Windows, where it's a disk tool.)
set(LAYCUE_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pushbacks")
find_program(LAYCUE_MAGICK magick)
if(NOT LAYCUE_MAGICK AND NOT WIN32)
  find_program(LAYCUE_MAGICK convert)
endif()
if(LAYCUE_MAGICK AND EXISTS "${LAYCUE_FIXTURES}/give-cheerfully-green.jpg")
  set(pam_files)
  foreach(fixture give-cheerfully-green.jpg daily-prayers-green.jpg announcement-transparent.webp)
    get_filename_component(stem "${fixture}" NAME_WE)
    set(pam "${CMAKE_CURRENT_BINARY_DIR}/fixtures/${stem}.pam")
    add_custom_command(
      OUTPUT "${pam}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
      COMMAND "${LAYCUE_MAGICK}" "${LAYCUE_FIXTURES}/${fixture}" -alpha on "PAM:${pam}"
      DEPENDS "${LAYCUE_FIXTURES}/${fixture}"
      VERBATIM
    )
    list(APPEND pam_files "${pam}")
  endforeach()
  add_custom_target(laycue-fixtures ALL DEPENDS ${pam_files})
  add_test(NAME laycue-fixture-tests COMMAND laycue-fixture-tests "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
else()
  message(STATUS "ImageMagick not found: skipping the real pushback fixture test")
endif()
