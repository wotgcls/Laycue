# The Titles preview tool: renders every template to PNG pictures without OBS, so designs can be
# checked (and compared after a change) on any computer.
#   cmake -S plugins/titles/tools -B build_titles_tools && cmake --build build_titles_tools
#   QT_QPA_PLATFORM=offscreen build_titles_tools/titles-preview <out folder>
cmake_minimum_required(VERSION 3.20)
project(titles-preview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
set(TITLES_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_executable(
  titles-preview
  preview.cpp
  "${TITLES_SRC}/core/easing.cpp"
  "${TITLES_SRC}/core/ticker.cpp"
  "${TITLES_SRC}/core/title-clock.cpp"
  "${TITLES_SRC}/core/title-templates.cpp"
  "${TITLES_SRC}/render/brand-icons.cpp"
  "${TITLES_SRC}/render/font-set.cpp"
  "${TITLES_SRC}/render/title-renderer.cpp"
)
target_include_directories(titles-preview PRIVATE "${TITLES_SRC}")
target_link_libraries(titles-preview PRIVATE Qt6::Core Qt6::Gui)
target_compile_definitions(titles-preview PRIVATE TITLES_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../data")
