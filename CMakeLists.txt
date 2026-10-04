cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/laycue-plugin.cmake")

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# The Laycue pack: three plugins that are installed (and can be removed) separately, plus a small
# static library with the shared look and feel (brand tokens, controls, fonts).
add_subdirectory(plugins/common)
add_subdirectory(plugins/scenes)

if(OS_MACOS)
  # One installer package holding every plugin of the pack.
  set(LAYCUE_PLUGIN_LIST laycue-scenes)
  configure_file(cmake/macos/resources/distribution.in "${CMAKE_CURRENT_BINARY_DIR}/distribution" @ONLY)
  configure_file(
    cmake/macos/resources/create-package.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/create-package.cmake"
    @ONLY
  )
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/create-package.cmake")
endif()
